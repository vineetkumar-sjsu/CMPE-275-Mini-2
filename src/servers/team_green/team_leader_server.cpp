#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

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

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using grpc::Channel;
using grpc::ClientContext;

using firequery::FireQueryService;
using firequery::QueryRequest;
using firequery::DelegationRequest;
using firequery::DelegationResponse;
using firequery::HealthRequest;
using firequery::HealthResponse;
using firequery::CancelRequest;
using firequery::CancelResponse;
using firequery::FireRecord;

class TeamLeaderServiceImpl final : public FireQueryService::Service {
public:
    TeamLeaderServiceImpl(const ProcessConfig& config)
        : config_(config), status_mgr_(false), data_loader_(config.data_path) {

        std::cout << "Team Leader Process " << config_.process_id
                  << " (Team " << config_.team << ") starting..." << std::endl;
        std::cout << "Listening on " << config_.listen_host << ":" << config_.listen_port << std::endl;

        // Create gRPC clients to workers
        for (const auto& edge : config_.edges) {
            if (edge.relationship == "worker") {
                std::string target = edge.host + ":" + std::to_string(edge.port);
                auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
                worker_stubs_[edge.to] = FireQueryService::NewStub(channel);

                std::cout << "Connected to worker " << edge.to << " at " << target << std::endl;
            }
        }

        // Print data partition
        std::cout << "Data partition: ";
        for (const auto& date : config_.data_partitioning.owned_dates) {
            std::cout << date << " ";
        }
        std::cout << std::endl;
    }

    Status DelegateQuery(ServerContext* context,
                        const DelegationRequest* request,
                        ServerWriter<DelegationResponse>* writer) override {

        std::cout << "\n[Team Leader " << config_.process_id << "] Received delegation "
                  << request->request_id() << " from " << request->delegating_process() << std::endl;

        // Update status
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            pending_requests_++;
            status_mgr_.updateProcessStatus(config_.process_id, pending_requests_, 1, completed_requests_);
        }

        // Deserialize original query
        QueryRequest original_query;
        if (!original_query.ParseFromString(request->original_query())) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "Failed to parse original query");
        }

        // Determine dates to process (intersection of query range and owned dates)
        std::vector<std::string> dates_to_process = selectDatesToProcess(original_query);

        std::cout << "  Processing " << dates_to_process.size() << " dates locally" << std::endl;

        // Process own data first
        if (!dates_to_process.empty()) {
            processLocalData(original_query, dates_to_process, request->request_id(), writer);
        }

        // Delegate to workers if any
        if (!worker_stubs_.empty()) {
            delegateToWorkers(request, &original_query, writer);
        }

        std::cout << "[Team Leader " << config_.process_id << "] Delegation "
                  << request->request_id() << " complete" << std::endl;

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
        response->set_active_workers(worker_stubs_.size());
        return Status::OK;
    }

    // Not used by team leaders
    Status QueryFire(ServerContext* context,
                    const QueryRequest* request,
                    ServerWriter<firequery::QueryResponse>* writer) override {
        return Status(grpc::StatusCode::UNIMPLEMENTED, "Team leaders don't accept direct queries");
    }

    Status CancelQuery(ServerContext* context,
                      const CancelRequest* request,
                      CancelResponse* response) override {
        std::cout << "[Team Leader " << config_.process_id << "] Cancel request for "
                  << request->request_id() << std::endl;
        response->set_request_id(request->request_id());
        response->set_cancelled(true);
        return Status::OK;
    }

private:
    ProcessConfig config_;
    StatusManager status_mgr_;
    FireDataLoader data_loader_;
    std::map<std::string, std::unique_ptr<FireQueryService::Stub>> worker_stubs_;
    int pending_requests_ = 0;
    int completed_requests_ = 0;
    std::mutex status_mutex_;

    std::vector<std::string> selectDatesToProcess(const QueryRequest& query) {
        std::vector<std::string> result;

        // Simple implementation: filter owned dates by query range
        for (const auto& date : config_.data_partitioning.owned_dates) {
            if (date >= query.date_start() && date <= query.date_end()) {
                result.push_back(date);
            }
        }

        return result;
    }

    void processLocalData(const QueryRequest& query,
                         const std::vector<std::string>& dates,
                         const std::string& request_id,
                         ServerWriter<DelegationResponse>* writer) {

        std::cout << "  [Team Leader " << config_.process_id << "] Loading local data..." << std::endl;

        // Load data
        std::vector<FireDataRecord> records = data_loader_.loadData(
            dates,
            query.pollutant_type(),
            query.latitude_min(),
            query.latitude_max(),
            query.longitude_min(),
            query.longitude_max(),
            query.max_records()
        );

        std::cout << "  [Team Leader " << config_.process_id << "] Loaded "
                  << records.size() << " records" << std::endl;

        // Send in chunks
        int chunk_size = config_.chunk_config.default_chunk_size;
        int chunk_count = 0;

        for (size_t i = 0; i < records.size(); i += chunk_size) {
            DelegationResponse chunk_resp;
            chunk_resp.set_request_id(request_id);
            chunk_resp.set_chunk_number(chunk_count++);
            chunk_resp.set_is_final(false);
            chunk_resp.set_responding_process(config_.process_id);

            size_t end = std::min(i + chunk_size, records.size());
            for (size_t j = i; j < end; j++) {
                auto* rec = chunk_resp.add_records();
                convertToProto(records[j], rec);
            }

            if (!writer->Write(chunk_resp)) {
                std::cerr << "  [Team Leader " << config_.process_id
                          << "] Failed to write chunk" << std::endl;
                return;
            }

            std::cout << "  [Team Leader " << config_.process_id << "] Sent chunk "
                      << chunk_count - 1 << " with " << chunk_resp.records_size() << " records" << std::endl;
        }
    }

    void delegateToWorkers(const DelegationRequest* request,
                          const QueryRequest* original_query,
                          ServerWriter<DelegationResponse>* writer) {

        for (auto& [worker_id, stub] : worker_stubs_) {
            std::cout << "  [Team Leader " << config_.process_id << "] Delegating to worker "
                      << worker_id << std::endl;

            ClientContext client_ctx;
            std::unique_ptr<grpc::ClientReader<DelegationResponse>> reader(
                stub->DelegateQuery(&client_ctx, *request));

            DelegationResponse delegation_resp;
            while (reader->Read(&delegation_resp)) {
                // Forward worker's response to leader
                if (!writer->Write(delegation_resp)) {
                    std::cerr << "  [Team Leader " << config_.process_id
                              << "] Failed to forward worker response" << std::endl;
                    reader->Finish();
                    return;
                }

                std::cout << "  [Team Leader " << config_.process_id << "] Forwarded chunk from "
                          << delegation_resp.responding_process() << " with "
                          << delegation_resp.records_size() << " records" << std::endl;
            }

            Status status = reader->Finish();
            if (!status.ok()) {
                std::cerr << "  [Team Leader " << config_.process_id << "] Worker "
                          << worker_id << " error: " << status.error_message() << std::endl;
            }
        }
    }

    void convertToProto(const FireDataRecord& src, FireRecord* dest) {
        dest->set_latitude(src.latitude);
        dest->set_longitude(src.longitude);
        dest->set_timestamp(src.timestamp);
        dest->set_pollutant(src.pollutant);
        dest->set_concentration(src.concentration);
        dest->set_unit(src.unit);
        dest->set_raw_concentration(src.raw_concentration);
        dest->set_aqi(src.aqi);
        dest->set_aqi_category(src.aqi_category);
        dest->set_site_name(src.site_name);
        dest->set_agency(src.agency);
        dest->set_site_id(src.site_id);
        dest->set_full_site_id(src.full_site_id);
    }
};

void RunTeamLeaderServer(const std::string& config_file) {
    ProcessConfig config = ConfigParser::loadConfig(config_file);

    std::string server_address = config.listen_host + ":" + std::to_string(config.listen_port);
    TeamLeaderServiceImpl service(config);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "\n*** Team Leader server listening on " << server_address << " ***\n" << std::endl;

    server->Wait();
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }

    try {
        RunTeamLeaderServer(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
