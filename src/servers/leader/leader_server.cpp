#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>
#include <queue>
#include <vector>
#include <atomic>
#include <condition_variable>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/create_channel.h>

#include "fire_query.grpc.pb.h"
#include "../../common/config.hpp"
#include "../../common/fire_data_loader.hpp"
#include "../../shmem/status_manager.hpp"
#include "../../common/metrics.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using grpc::Channel;
using grpc::ClientContext;

using firequery::FireQueryService;
using firequery::QueryRequest;
using firequery::QueryResponse;
using firequery::HealthRequest;
using firequery::HealthResponse;
using firequery::DelegationRequest;
using firequery::DelegationResponse;
using firequery::CancelRequest;
using firequery::CancelResponse;
using firequery::FireRecord;

// Thread-safe queue for buffering chunks from team leaders
template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;
    
    // Move constructor
    ThreadSafeQueue(ThreadSafeQueue&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.mutex_);
        queue_ = std::move(other.queue_);
        finished_ = other.finished_;
    }
    
    // Move assignment
    ThreadSafeQueue& operator=(ThreadSafeQueue&& other) noexcept {
        if (this != &other) {
            // lock both mutexes without deadlock
            std::scoped_lock lock(mutex_, other.mutex_);
            queue_ = std::move(other.queue_);
            finished_ = other.finished_;
        }
        return *this;
    }
    
    // Delete copy operations
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    void push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(item);
        condition_.notify_one();
    }

    // move-push
    void push(T&& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(item));
        condition_.notify_one();
    }

    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = queue_.front();
        queue_.pop();
        return true;
    }

    // Wait for an item up to the specified timeout. Returns true if an item was popped.
    template<typename Rep, typename Period>
    bool wait_pop_for(T& item, const std::chrono::duration<Rep, Period>& rel_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!condition_.wait_for(lock, rel_time, [this]() { return !queue_.empty() || finished_; })) {
            return false;
        }

        if (queue_.empty()) {
            return false;
        }

        item = queue_.front();
        queue_.pop();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    void set_finished() {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
        condition_.notify_all();
    }

    bool is_finished() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return finished_ && queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    std::condition_variable condition_;
    bool finished_ = false;
};

// Structure to hold team stream information
struct TeamReader {
    std::string team_name;
    std::string team_leader_id;
    std::unique_ptr<ClientContext> context;
    std::unique_ptr<grpc::ClientReader<DelegationResponse>> reader;
    ThreadSafeQueue<DelegationResponse> buffer;
    std::atomic<bool> finished{false};
    std::atomic<bool> error_occurred{false};
    Status finish_status;

    // Make it movable (default move constructor/assignment should work)
    TeamReader() = default;
    TeamReader(TeamReader&&) = default;
    TeamReader& operator=(TeamReader&&) = default;
    // Delete copy operations since we have unique_ptr members
    TeamReader(const TeamReader&) = delete;
    TeamReader& operator=(const TeamReader&) = delete;
};

class LeaderServiceImpl final : public FireQueryService::Service {
public:
    LeaderServiceImpl(const ProcessConfig& config)
        : config_(config), status_mgr_(true), request_counter_(0) {

        std::cout << "Leader Process " << config_.process_id << " starting..." << std::endl;
        std::cout << "Listening on " << config_.listen_host << ":" << config_.listen_port << std::endl;

        // Create gRPC clients to team leaders
        for (const auto& edge : config_.edges) {
            std::string target = edge.host + ":" + std::to_string(edge.port);
            auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
            team_leader_stubs_[edge.to] = FireQueryService::NewStub(channel);

            std::cout << "Connected to team leader " << edge.to << " (" << edge.team << ") at " << target << std::endl;
        }

        // Initialize metrics logging for this process
        metrics::init_with_dir("logs", config_.process_id, config_.role);
    }

    Status QueryFire(ServerContext* context,
                    const QueryRequest* request,
                    ServerWriter<QueryResponse>* writer) override {

        std::cout << "\n[Leader] Received query " << request->request_id() << std::endl;
        std::cout << "  Date range: " << request->date_start() << " to " << request->date_end() << std::endl;
        std::cout << "  Pollutant: " << request->pollutant_type() << std::endl;

        // Update status
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            pending_requests_++;
            status_mgr_.updateProcessStatus(config_.process_id, pending_requests_, 1, completed_requests_);
        }

        // Metrics: enqueue event
        metrics::log_event("ENQUEUE", request->request_id(), pending_requests_, 1, -1, -1, "received at leader");

        // Determine which team(s) to delegate to based on load balancing
        std::vector<std::string> teams_to_query = selectTeamsForQuery(request);

        std::cout << "  Delegating to teams: ";
        for (const auto& team : teams_to_query) {
            std::cout << team << " ";
        }
        std::cout << std::endl;

        // Collect responses from teams and stream to client using round-robin
        int total_chunk_number = 0;
        int total_records = 0;

        metrics::log_event("START_DELEGATE", request->request_id(), pending_requests_, 1, -1, -1, "delegating to teams");

        // Prepare delegation request
        DelegationRequest delegation_req;
        delegation_req.set_request_id(request->request_id());
        delegation_req.set_delegating_process(config_.process_id);
        std::string serialized_query;
        request->SerializeToString(&serialized_query);
        delegation_req.set_original_query(serialized_query);

        // Open all team streams simultaneously
        std::vector<std::unique_ptr<TeamReader>> team_readers;
        std::vector<std::thread> reader_threads;

        for (const auto& team_name : teams_to_query) {
            std::string team_leader_id = getTeamLeader(team_name);
            if (team_leader_id.empty()) {
                std::cerr << "[Leader] No team leader found for team: " << team_name << std::endl;
                continue;
            }

            auto stub_iter = team_leader_stubs_.find(team_leader_id);
            if (stub_iter == team_leader_stubs_.end()) {
                std::cerr << "[Leader] No stub for team leader: " << team_leader_id << std::endl;
                continue;
            }

            // Create team reader
            auto team_reader = std::make_unique<TeamReader>();
            team_reader->team_name = team_name;
            team_reader->team_leader_id = team_leader_id;
            team_reader->context = std::make_unique<ClientContext>();
            team_reader->reader = stub_iter->second->DelegateQuery(team_reader->context.get(), delegation_req);
            
            team_readers.push_back(std::move(team_reader));
        }

        // Shared cancellation flag for reader threads
        std::atomic<bool> cancel_requested{false};

        // Start reader threads for each team
        for (size_t i = 0; i < team_readers.size(); i++) {
            reader_threads.emplace_back([&team_readers, i, &cancel_requested]() {
                TeamReader& reader = *team_readers[i];
                DelegationResponse chunk;

                // Read loop: break if cancellation requested or read EOF/error
                while (!cancel_requested.load() && reader.reader->Read(&chunk)) {
                    reader.buffer.push(chunk);
                }

                // If cancellation was requested, attempt to finish the reader gracefully
                reader.finished = true;
                reader.finish_status = reader.reader->Finish();
                reader.buffer.set_finished();

                if (!reader.finish_status.ok()) {
                    reader.error_occurred = true;
                    std::cerr << "[Leader] Team leader " << reader.team_leader_id
                              << " returned error: " << reader.finish_status.error_message() << std::endl;
                }
            });
        }

        // Round-robin through team buffers
        bool all_finished = false;
        while (!all_finished) {
            // If server context was cancelled, request cancellation of readers and break
            if (context->IsCancelled()) {
                std::cerr << "[Leader] Server context cancelled by client. Cancelling delegated reads..." << std::endl;
                cancel_requested.store(true);
                for (auto& tr : team_readers) {
                    if (tr->context) tr->context->TryCancel();
                }
                break;
            }

            all_finished = true;
            bool any_data_this_round = false;

            for (size_t i = 0; i < team_readers.size(); i++) {
                TeamReader& team_reader = *team_readers[i];

                // Skip if this team is finished and buffer is empty
                if (team_reader.buffer.is_finished()) {
                    continue;
                }

                all_finished = false;

                // Wait up to a short duration for data to appear to avoid busy-waiting.
                DelegationResponse delegation_resp;
                if (team_reader.buffer.wait_pop_for(delegation_resp, std::chrono::milliseconds(10))) {
                    any_data_this_round = true;

                    QueryResponse query_resp;
                    query_resp.set_request_id(request->request_id());
                    query_resp.set_chunk_number(total_chunk_number++);
                    query_resp.set_total_chunks(-1); // Unknown until final chunk
                    query_resp.set_is_final(false);
                    query_resp.set_source_process(delegation_resp.responding_process());

                    // Copy records
                    for (const auto& record : delegation_resp.records()) {
                        auto* new_record = query_resp.add_records();
                        new_record->CopyFrom(record);
                        total_records++;
                    }

                    // Stream to client
                    if (!writer->Write(query_resp)) {
                        std::cerr << "[Leader] Client disconnected during streaming" << std::endl;
                        metrics::log_event("CLIENT_DISCONNECT", request->request_id(), pending_requests_, 1, query_resp.chunk_number(), query_resp.records_size(), "client disconnected during streaming");

                        // Cancel all readers and wait for threads
                        cancel_requested.store(true);
                        for (auto& tr : team_readers) {
                            if (tr->context) tr->context->TryCancel();
                        }
                        for (auto& thread : reader_threads) {
                            if (thread.joinable()) {
                                thread.join();
                            }
                        }
                        return Status::CANCELLED;
                    }

                    // Metrics: chunk relay (only after successful write)
                    metrics::log_event("CHUNK_RELAY", request->request_id(), pending_requests_, 1, query_resp.chunk_number(), query_resp.records_size(), delegation_resp.responding_process());

                    std::cout << "  Sent chunk " << query_resp.chunk_number()
                              << " with " << query_resp.records_size() << " records from "
                              << query_resp.source_process() << " (team: " << team_reader.team_name << ")" << std::endl;
                }
            }

            // If no data was available this round, yield to avoid busy-waiting
            if (!any_data_this_round && !all_finished) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // Wait for all reader threads to complete
        for (auto& thread : reader_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        // Check for errors
        for (const auto& team_reader : team_readers) {
            if (team_reader->error_occurred && !team_reader->finish_status.ok()) {
                std::cerr << "[Leader] Team leader " << team_reader->team_leader_id
                          << " error: " << team_reader->finish_status.error_message() << std::endl;
            }
        }

        // Send final chunk
        QueryResponse final_resp;
        final_resp.set_request_id(request->request_id());
        final_resp.set_chunk_number(total_chunk_number);
        final_resp.set_total_chunks(total_chunk_number + 1);
        final_resp.set_is_final(true);
        final_resp.set_total_records(total_records);
        final_resp.set_source_process(config_.process_id);
        if (!writer->Write(final_resp)) {
            std::cerr << "[Leader] Client disconnected while sending final chunk" << std::endl;
            metrics::log_event("CLIENT_DISCONNECT_FINAL", request->request_id(), pending_requests_, 1, final_resp.chunk_number(), final_resp.total_records(), "client disconnected on final chunk");
            return Status::CANCELLED;
        }

        metrics::log_event("FINAL_CHUNK", request->request_id(), pending_requests_, 1, final_resp.chunk_number(), final_resp.total_records(), "final from leader");

        metrics::log_event("FINISH", request->request_id(), pending_requests_, 1, -1, total_records, "query complete at leader");

        std::cout << "[Leader] Query " << request->request_id() << " complete. "
                  << "Sent " << (total_chunk_number + 1) << " chunks, "
                  << total_records << " total records" << std::endl;

        // Update status
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            pending_requests_--;
            completed_requests_++;
            status_mgr_.updateProcessStatus(config_.process_id, pending_requests_, 1, completed_requests_);
        }

        return Status::OK;
    }

    Status HealthCheck(ServerContext* context,
                      const HealthRequest* request,
                      HealthResponse* response) override {
        response->set_responding_process(config_.process_id);
        response->set_is_healthy(true);
        response->set_pending_requests(pending_requests_);
        response->set_active_workers(1);
        return Status::OK;
    }

    Status CancelQuery(ServerContext* context,
                      const CancelRequest* request,
                      CancelResponse* response) override {
        std::cout << "[Leader] Received cancel request for " << request->request_id() << std::endl;
        response->set_request_id(request->request_id());
        response->set_cancelled(true);
        response->set_message("Query cancellation acknowledged");
        return Status::OK;
    }

    // DelegateQuery not implemented in leader (only receives queries from clients)
    Status DelegateQuery(ServerContext* context,
                        const DelegationRequest* request,
                        ServerWriter<DelegationResponse>* writer) override {
        return Status(grpc::StatusCode::UNIMPLEMENTED, "Leader does not accept delegations");
    }

private:
    ProcessConfig config_;
    StatusManager status_mgr_;
    std::map<std::string, std::unique_ptr<FireQueryService::Stub>> team_leader_stubs_;
    int request_counter_;
    int pending_requests_ = 0;
    int completed_requests_ = 0;
    std::mutex status_mutex_;

    std::vector<std::string> selectTeamsForQuery(const QueryRequest* request) {
        std::vector<std::string> teams;

        // Simple strategy: Check load balance via shared memory
        std::string least_loaded = status_mgr_.getLeastLoadedTeam();

        // For comprehensive queries, query both teams
        // For demonstration, we'll query both teams to show parallelism
        teams.push_back("green");
        teams.push_back("pink");

        return teams;
    }

    std::string getTeamLeader(const std::string& team_name) {
        for (const auto& edge : config_.edges) {
            if (edge.team == team_name && edge.relationship == "team_leader") {
                return edge.to;
            }
        }
        return "";
    }
};

void RunLeaderServer(const std::string& config_file) {
    ProcessConfig config = ConfigParser::loadConfig(config_file);

    std::string server_address = config.listen_host + ":" + std::to_string(config.listen_port);
    LeaderServiceImpl service(config);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "\n*** Leader server listening on " << server_address << " ***\n" << std::endl;

    server->Wait();
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }

    try {
        RunLeaderServer(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        metrics::shutdown();
        return 1;
    }

    metrics::shutdown();
    return 0;
}
