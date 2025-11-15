#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>

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

        // Collect responses from teams and stream to client
        int total_chunk_number = 0;
        int total_records = 0;

    metrics::log_event("START_DELEGATE", request->request_id(), pending_requests_, 1, -1, -1, "delegating to teams");

        for (const auto& team_name : teams_to_query) {
            // Find the team leader for this team
            std::string team_leader_id = getTeamLeader(team_name);
            if (team_leader_id.empty()) {
                std::cerr << "[Leader] No team leader found for team: " << team_name << std::endl;
                continue;
            }

            // Delegate query to team leader
            DelegationRequest delegation_req;
            delegation_req.set_request_id(request->request_id());
            delegation_req.set_delegating_process(config_.process_id);

            // Serialize original query
            std::string serialized_query;
            request->SerializeToString(&serialized_query);
            delegation_req.set_original_query(serialized_query);

            // Call team leader
            ClientContext client_ctx;
            auto stub_iter = team_leader_stubs_.find(team_leader_id);
            if (stub_iter == team_leader_stubs_.end()) {
                std::cerr << "[Leader] No stub for team leader: " << team_leader_id << std::endl;
                continue;
            }

            std::unique_ptr<grpc::ClientReader<DelegationResponse>> reader(
                stub_iter->second->DelegateQuery(&client_ctx, delegation_req));

            // Stream responses from team leader to client
            DelegationResponse delegation_resp;
            while (reader->Read(&delegation_resp)) {
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
                    reader->Finish();
                    metrics::log_event("CLIENT_DISCONNECT", request->request_id(), pending_requests_, 1, query_resp.chunk_number(), query_resp.records_size(), "client disconnected during streaming");
                    return Status::CANCELLED;
                }

                // Metrics: chunk relay (only after successful write)
                metrics::log_event("CHUNK_RELAY", request->request_id(), pending_requests_, 1, query_resp.chunk_number(), query_resp.records_size(), delegation_resp.responding_process());

                std::cout << "  Sent chunk " << query_resp.chunk_number()
                          << " with " << query_resp.records_size() << " records from "
                          << query_resp.source_process() << std::endl;
            }

            Status delegation_status = reader->Finish();
            if (!delegation_status.ok()) {
                std::cerr << "[Leader] Team leader " << team_leader_id
                          << " returned error: " << delegation_status.error_message() << std::endl;
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
