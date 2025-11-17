#include <iostream>
#include <memory>
#include <string>
#include <algorithm>
#include <thread>
#include <chrono>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>

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

using firequery::FireQueryService;
using firequery::QueryRequest;
using firequery::DelegationRequest;
using firequery::DelegationResponse;
using firequery::HealthRequest;
using firequery::HealthResponse;
using firequery::CancelRequest;
using firequery::CancelResponse;
using firequery::FireRecord;

class WorkerServiceImpl final : public FireQueryService::Service {
public:
    WorkerServiceImpl(const ProcessConfig& config)
        : config_(config), status_mgr_(false), data_loader_(config.data_path) {

        std::cout << "Worker Process " << config_.process_id
                  << " (Team " << config_.team << ") starting..." << std::endl;
        std::cout << "Listening on " << config_.listen_host << ":" << config_.listen_port << std::endl;

        // Print data partition
        std::cout << "Data partition: ";
        for (const auto& date : config_.data_partitioning.owned_dates) {
            std::cout << date << " ";
        }
        std::cout << std::endl;

        // Initialize metrics logging for this process
        metrics::init_with_dir("logs", config_.process_id, config_.role);
    }

    Status DelegateQuery(ServerContext* context,
                        const DelegationRequest* request,
                        ServerWriter<DelegationResponse>* writer) override {

        std::cout << "\n[Worker " << config_.process_id << "] Received delegation "
                  << request->request_id() << " from " << request->delegating_process() << std::endl;

        metrics::log_event("RECEIVED_DELEGATION", request->request_id(), pending_requests_, 1, -1, -1, request->delegating_process());

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

        // Determine dates to process
        std::vector<std::string> dates_to_process = selectDatesToProcess(original_query);

        std::cout << "  [Worker " << config_.process_id << "] Processing "
                  << dates_to_process.size() << " dates" << std::endl;

        if (dates_to_process.empty()) {
            std::cout << "  [Worker " << config_.process_id << "] No matching dates in partition" << std::endl;
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                pending_requests_--;
                completed_requests_++;
                status_mgr_.updateProcessStatus(config_.process_id, pending_requests_, 1, completed_requests_);
            }
            return Status::OK;
        }

        // Load data
        auto start_time = std::chrono::high_resolution_clock::now();

        std::vector<FireDataRecord> records = data_loader_.loadData(
            dates_to_process,
            original_query.pollutant_type(),
            original_query.latitude_min(),
            original_query.latitude_max(),
            original_query.longitude_min(),
            original_query.longitude_max(),
            original_query.max_records()
        );

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "  [Worker " << config_.process_id << "] Loaded " << records.size()
                  << " records in " << duration.count() << "ms" << std::endl;

        metrics::log_event("LOADED_RECORDS", request->request_id(), pending_requests_, 1, -1, records.size(), "loaded by worker");

        // Send in chunks
        int chunk_size = config_.chunk_config.default_chunk_size;
        int chunk_count = 0;

        for (size_t i = 0; i < records.size(); i += chunk_size) {
            DelegationResponse chunk_resp;
            chunk_resp.set_request_id(request->request_id());
            chunk_resp.set_chunk_number(chunk_count++);
            chunk_resp.set_is_final(false);
            chunk_resp.set_responding_process(config_.process_id);

            size_t end = std::min(i + chunk_size, records.size());
            for (size_t j = i; j < end; j++) {
                auto* rec = chunk_resp.add_records();
                convertToProto(records[j], rec);
            }

            if (!writer->Write(chunk_resp)) {
                std::cerr << "  [Worker " << config_.process_id << "] Failed to write chunk" << std::endl;
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    // Metrics: failed to send worker chunk upstream
                    metrics::log_event("WORKER_CHUNK_SEND_ERROR", request->request_id(), pending_requests_, 1, chunk_resp.chunk_number(), chunk_resp.records_size(), config_.process_id);
                    pending_requests_--;
                    status_mgr_.updateProcessStatus(config_.process_id, pending_requests_, 1, completed_requests_);
                }
                return Status::CANCELLED;
            }

            // Metrics: worker chunk sent (only after successful write)
            metrics::log_event("WORKER_CHUNK_SENT", request->request_id(), pending_requests_, 1, chunk_resp.chunk_number(), chunk_resp.records_size(), config_.process_id);

            std::cout << "  [Worker " << config_.process_id << "] Sent chunk " << chunk_count - 1
                      << " with " << chunk_resp.records_size() << " records" << std::endl;

            // Simulate some processing time for realistic demonstration
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        std::cout << "[Worker " << config_.process_id << "] Delegation "
                  << request->request_id() << " complete. Sent " << chunk_count << " chunks" << std::endl;

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

    // Not used by workers
    Status QueryFire(ServerContext* context,
                    const QueryRequest* request,
                    ServerWriter<firequery::QueryResponse>* writer) override {
        return Status(grpc::StatusCode::UNIMPLEMENTED, "Workers don't accept direct queries");
    }

    Status CancelQuery(ServerContext* context,
                      const CancelRequest* request,
                      CancelResponse* response) override {
        std::cout << "[Worker " << config_.process_id << "] Cancel request for "
                  << request->request_id() << std::endl;
        response->set_request_id(request->request_id());
        response->set_cancelled(true);
        return Status::OK;
    }

private:
    ProcessConfig config_;
    StatusManager status_mgr_;
    FireDataLoader data_loader_;
    int pending_requests_ = 0;
    int completed_requests_ = 0;
    std::mutex status_mutex_;

    std::vector<std::string> selectDatesToProcess(const QueryRequest& query) {
        std::vector<std::string> result;

        for (const auto& date : config_.data_partitioning.owned_dates) {
            if (date >= query.date_start() && date <= query.date_end()) {
                result.push_back(date);
            }
        }

        return result;
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

void RunWorkerServer(const std::string& config_file) {
    ProcessConfig config = ConfigParser::loadConfig(config_file);

    std::string server_address = config.listen_host + ":" + std::to_string(config.listen_port);
    WorkerServiceImpl service(config);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "\n*** Worker server listening on " << server_address << " ***\n" << std::endl;

    server->Wait();
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }

    try {
        RunWorkerServer(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        metrics::shutdown();
        return 1;
    }

    metrics::shutdown();
    return 0;
}
