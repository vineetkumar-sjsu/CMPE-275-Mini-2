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
#include <unordered_map>

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

// ==========================
// Bounded, thread-safe queue
// ==========================
template<typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t capacity = 32) : capacity_(capacity) {}

    // Move ctor/assign
    ThreadSafeQueue(ThreadSafeQueue&& other) noexcept {
        std::scoped_lock lk(mutex_, other.mutex_);
        queue_     = std::move(other.queue_);
        finished_  = other.finished_;
        capacity_  = other.capacity_;
    }
    ThreadSafeQueue& operator=(ThreadSafeQueue&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lk(mutex_, other.mutex_);
            queue_     = std::move(other.queue_);
            finished_  = other.finished_;
            capacity_  = other.capacity_;
        }
        return *this;
    }

    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    // Blocking push with backpressure. Returns false if finished while waiting.
    bool push(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_cv_.wait(lock, [this](){ return finished_ || queue_.size() < capacity_; });
        if (finished_) return false;
        queue_.push(item);
        not_empty_cv_.notify_one();
        return true;
    }
    bool push(T&& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_cv_.wait(lock, [this](){ return finished_ || queue_.size() < capacity_; });
        if (finished_) return false;
        queue_.push(std::move(item));
        not_empty_cv_.notify_one();
        return true;
    }

    // Try-pop (non-blocking)
    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        not_full_cv_.notify_one();
        return true;
    }

    // Wait up to rel_time; returns true if popped an item
    template<typename Rep, typename Period>
    bool wait_pop_for(T& item, const std::chrono::duration<Rep, Period>& rel_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!not_empty_cv_.wait_for(lock, rel_time, [this](){ return finished_ || !queue_.empty(); })) {
            return false;
        }
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        not_full_cv_.notify_one();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    void set_finished() {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    bool is_finished() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return finished_ && queue_.empty();
    }

    size_t capacity() const { return capacity_; }

private:
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;
    bool finished_ = false;
    size_t capacity_;
};

// ==========================
// Per-team stream container
// ==========================
struct TeamReader {
    std::string team_name;
    std::string team_leader_id;
    std::unique_ptr<ClientContext> context;
    std::unique_ptr<grpc::ClientReader<DelegationResponse>> reader;
    ThreadSafeQueue<DelegationResponse> buffer{32}; // bounded buffer
    std::atomic<bool> finished{false};
    std::atomic<bool> error_occurred{false};
    Status finish_status;

    TeamReader() = default;
    // TeamReader(TeamReader&&) = default;
    // TeamReader& operator=(TeamReader&&) = default;
    TeamReader(const TeamReader&) = delete;
    TeamReader& operator=(const TeamReader&) = delete;
};

// ==========================
// Leader service
// ==========================
class LeaderServiceImpl final : public FireQueryService::Service {
public:
    LeaderServiceImpl(const ProcessConfig& config)
        : config_(config), status_mgr_(true), request_counter_(0) {

        std::cout << "Leader Process " << config_.process_id << " starting...\n";
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

        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            pending_requests_++;
            status_mgr_.updateProcessStatus(config_.process_id, pending_requests_, 1, completed_requests_);
        }

        metrics::log_event("ENQUEUE", request->request_id(), pending_requests_, 1, -1, -1, "received at leader");

        // Teams to query (kept simple: both teams)
        std::vector<std::string> teams_to_query = selectTeamsForQuery(request);
        std::cout << "  Delegating to teams: ";
        for (const auto& t : teams_to_query) std::cout << t << " ";
        std::cout << std::endl;

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

        // Open all team streams
        std::vector<std::unique_ptr<TeamReader>> team_readers;
        std::vector<std::thread> reader_threads;

        for (const auto& team_name : teams_to_query) {
            std::string team_leader_id = getTeamLeader(team_name);
            if (team_leader_id.empty()) {
                std::cerr << "[Leader] No team leader for team: " << team_name << std::endl;
                continue;
            }
            auto it = team_leader_stubs_.find(team_leader_id);
            if (it == team_leader_stubs_.end()) {
                std::cerr << "[Leader] No stub for TL: " << team_leader_id << std::endl;
                continue;
            }

            auto tr = std::make_unique<TeamReader>();
            tr->team_name = team_name;
            tr->team_leader_id = team_leader_id;
            tr->context = std::make_unique<ClientContext>();
            tr->reader = it->second->DelegateQuery(tr->context.get(), delegation_req);
            team_readers.push_back(std::move(tr));
        }

        // Per-team relay counters (for TEAM_FINISH)
        std::unordered_map<std::string, int> team_chunks_sent;
        std::unordered_map<std::string, long long> team_records_sent;
        std::unordered_map<std::string, bool> team_finish_logged;

        for (auto& tr : team_readers) {
            team_chunks_sent[tr->team_name] = 0;
            team_records_sent[tr->team_name] = 0;
            team_finish_logged[tr->team_name] = false;
        }

        // Shared cancellation
        std::atomic<bool> cancel_requested{false};

        // Reader threads: read -> bounded push
        for (size_t i = 0; i < team_readers.size(); ++i) {
            reader_threads.emplace_back([&team_readers, i, &cancel_requested]() {
                TeamReader& r = *team_readers[i];
                DelegationResponse chunk;
                while (!cancel_requested.load() && r.reader->Read(&chunk)) {
                    // push blocks if buffer full; returns false if finished while waiting
                    if (!r.buffer.push(std::move(chunk))) {
                        break;
                    }
                }
                r.finished = true;
                r.finish_status = r.reader->Finish();
                r.buffer.set_finished();
                if (!r.finish_status.ok()) {
                    r.error_occurred = true;
                    std::cerr << "[Leader] TL " << r.team_leader_id
                              << " returned error: " << r.finish_status.error_message() << std::endl;
                }
            });
        }

        // Leader multiplexer (one-chunk-per-team per scan) + short wait (2 ms)
        bool all_finished = false;
        while (!all_finished) {
            if (context->IsCancelled()) {
                std::cerr << "[Leader] Client cancelled. Cancelling delegated reads..." << std::endl;
                cancel_requested.store(true);
                for (auto& tr : team_readers) if (tr->context) tr->context->TryCancel();
                break;
            }

            all_finished = true;
            bool any_data_this_round = false;

            for (auto& tr_ptr : team_readers) {
                TeamReader& tr = *tr_ptr;

                // If this team has fully finished and its TEAM_FINISH not logged yet,
                // log once using relay-side counters (what the client actually saw).
                if (tr.buffer.is_finished() && !team_finish_logged[tr.team_name]) {
                    std::string extra = tr.team_name + ",chunks=" + std::to_string(team_chunks_sent[tr.team_name]) +
                                        ",records=" + std::to_string(team_records_sent[tr.team_name]);
                    metrics::log_event("TEAM_FINISH", request->request_id(), pending_requests_, 1, -1,
                                       static_cast<int>(team_records_sent[tr.team_name]), extra);
                    team_finish_logged[tr.team_name] = true;
                }

                if (tr.buffer.is_finished()) {
                    continue; // nothing left for this team
                }

                all_finished = false;

                // Pop at most ONE chunk this scan from this team
                DelegationResponse delegation_resp;
                if (tr.buffer.wait_pop_for(delegation_resp, std::chrono::milliseconds(2))) {
                    any_data_this_round = true;

                    QueryResponse query_resp;
                    query_resp.set_request_id(request->request_id());
                    query_resp.set_chunk_number(total_chunk_number++);
                    query_resp.set_total_chunks(-1);
                    query_resp.set_is_final(false);
                    query_resp.set_source_process(delegation_resp.responding_process());

                    // Copy records
                    int sent_this_chunk = 0;
                    for (const auto& record : delegation_resp.records()) {
                        auto* out = query_resp.add_records();
                        out->CopyFrom(record);
                        ++sent_this_chunk;
                    }
                    total_records += sent_this_chunk;

                    // Stream to client
                    if (!writer->Write(query_resp)) {
                        std::cerr << "[Leader] Client disconnected during streaming\n";
                        metrics::log_event("CLIENT_DISCONNECT", request->request_id(), pending_requests_, 1,
                                           query_resp.chunk_number(), query_resp.records_size(),
                                           "client disconnected during streaming");
                        cancel_requested.store(true);
                        for (auto& tr2 : team_readers) if (tr2->context) tr2->context->TryCancel();
                        for (auto& th : reader_threads) if (th.joinable()) th.join();
                        return Status::CANCELLED;
                    }

                    // Update per-team counters and metrics
                    team_chunks_sent[tr.team_name] += 1;
                    team_records_sent[tr.team_name] += sent_this_chunk;

                    metrics::log_event("CHUNK_RELAY", request->request_id(), pending_requests_, 1,
                                       query_resp.chunk_number(), query_resp.records_size(),
                                       delegation_resp.responding_process());

                    std::cout << "  Sent chunk " << query_resp.chunk_number()
                              << " with " << query_resp.records_size() << " records from "
                              << query_resp.source_process()
                              << " (team: " << tr.team_name << ")\n";
                }
                // IMPORTANT: do NOT pop another chunk from this same team in this scan (enforces 1 chunk/team/scan)
            }

            if (!any_data_this_round && !all_finished) {
                // Nothing ready anywhere this round: log and brief sleep
                metrics::log_event("NO_DATA_ROUND", request->request_id(), pending_requests_, 1, -1, -1, "");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // Join readers
        for (auto& th : reader_threads) {
            if (th.joinable()) th.join();
        }

        // Log TEAM_FINISH for any teams not yet logged (edge case: finished after loop exit)
        for (auto& tr_ptr : team_readers) {
            auto& tr = *tr_ptr;
            if (!team_finish_logged[tr.team_name]) {
                std::string extra = tr.team_name + ",chunks=" + std::to_string(team_chunks_sent[tr.team_name]) +
                                    ",records=" + std::to_string(team_records_sent[tr.team_name]);
                metrics::log_event("TEAM_FINISH", request->request_id(), pending_requests_, 1, -1,
                                   static_cast<int>(team_records_sent[tr.team_name]), extra);
                team_finish_logged[tr.team_name] = true;
            }
        }

        // Final chunk
        QueryResponse final_resp;
        final_resp.set_request_id(request->request_id());
        final_resp.set_chunk_number(total_chunk_number);
        final_resp.set_total_chunks(total_chunk_number + 1);
        final_resp.set_is_final(true);
        final_resp.set_total_records(total_records);
        final_resp.set_source_process(config_.process_id);
        if (!writer->Write(final_resp)) {
            std::cerr << "[Leader] Client disconnected while sending final\n";
            metrics::log_event("CLIENT_DISCONNECT_FINAL", request->request_id(), pending_requests_, 1,
                               final_resp.chunk_number(), final_resp.total_records(),
                               "client disconnected on final chunk");
            return Status::CANCELLED;
        }

        metrics::log_event("FINAL_CHUNK", request->request_id(), pending_requests_, 1,
                           final_resp.chunk_number(), final_resp.total_records(), "final from leader");
        metrics::log_event("FINISH", request->request_id(), pending_requests_, 1, -1, total_records,
                           "query complete at leader");

        std::cout << "[Leader] Query " << request->request_id() << " complete. "
                  << "Sent " << (total_chunk_number + 1) << " chunks, "
                  << total_records << " total records\n";

        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            pending_requests_--;
            completed_requests_++;
            status_mgr_.updateProcessStatus(config_.process_id, pending_requests_, 1, completed_requests_);
        }

        return Status::OK;
    }

    Status HealthCheck(ServerContext*,
                       const HealthRequest*,
                       HealthResponse* response) override {
        response->set_responding_process(config_.process_id);
        response->set_is_healthy(true);
        response->set_pending_requests(pending_requests_);
        response->set_active_workers(1);
        return Status::OK;
    }

    Status CancelQuery(ServerContext*,
                       const CancelRequest* request,
                       CancelResponse* response) override {
        std::cout << "[Leader] Received cancel for " << request->request_id() << std::endl;
        response->set_request_id(request->request_id());
        response->set_cancelled(true);
        response->set_message("Query cancellation acknowledged");
        return Status::OK;
    }

    Status DelegateQuery(ServerContext*,
                         const DelegationRequest*,
                         ServerWriter<DelegationResponse>*) override {
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

    std::vector<std::string> selectTeamsForQuery(const QueryRequest*) {
        // Query both teams to demonstrate parallelism
        return {"green", "pink"};
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
        std::cerr << "Usage: " << argv[0] << " <config_file>\n";
        return 1;
    }

    try {
        RunLeaderServer(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        metrics::shutdown();
        return 1;
    }

    metrics::shutdown();
    return 0;
}
