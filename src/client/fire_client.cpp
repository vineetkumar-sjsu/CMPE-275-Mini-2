#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <iomanip>

#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/client_context.h>

#include "fire_query.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::Status;

using firequery::FireQueryService;
using firequery::QueryRequest;
using firequery::QueryResponse;
using firequery::FireRecord;

class FireQueryClient {
public:
    FireQueryClient(std::shared_ptr<Channel> channel)
        : stub_(FireQueryService::NewStub(channel)) {}

    void QueryFire(const std::string& request_id,
                  const std::string& date_start,
                  const std::string& date_end,
                  const std::string& pollutant = "",
                  double lat_min = -90.0,
                  double lat_max = 90.0,
                  double lon_min = -180.0,
                  double lon_max = 180.0,
                  int max_records = -1,
                  int chunk_size = 500) {

        QueryRequest request;
        request.set_request_id(request_id);
        request.set_date_start(date_start);
        request.set_date_end(date_end);
        request.set_pollutant_type(pollutant);
        request.set_latitude_min(lat_min);
        request.set_latitude_max(lat_max);
        request.set_longitude_min(lon_min);
        request.set_longitude_max(lon_max);
        request.set_max_records(max_records);
        request.set_chunk_size(chunk_size);

        std::cout << "\n========================================" << std::endl;
        std::cout << "FIRE QUERY REQUEST" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Request ID:    " << request_id << std::endl;
        std::cout << "Date Range:    " << date_start << " to " << date_end << std::endl;
        std::cout << "Pollutant:     " << (pollutant.empty() ? "ALL" : pollutant) << std::endl;
        std::cout << "Latitude:      " << lat_min << " to " << lat_max << std::endl;
        std::cout << "Longitude:     " << lon_min << " to " << lon_max << std::endl;
        std::cout << "Max Records:   " << (max_records < 0 ? "UNLIMITED" : std::to_string(max_records)) << std::endl;
        std::cout << "Chunk Size:    " << chunk_size << std::endl;
        std::cout << "========================================\n" << std::endl;

        ClientContext context;
        std::unique_ptr<ClientReader<QueryResponse>> reader(
            stub_->QueryFire(&context, request));

        auto start_time = std::chrono::high_resolution_clock::now();

        int chunks_received = 0;
        int total_records = 0;
        std::map<std::string, int> records_by_process;

        QueryResponse response;
        while (reader->Read(&response)) {
            chunks_received++;
            int chunk_records = response.records_size();
            total_records += chunk_records;

            records_by_process[response.source_process()] += chunk_records;

            std::cout << "Chunk " << std::setw(3) << response.chunk_number()
                      << " | Source: " << response.source_process()
                      << " | Records: " << std::setw(4) << chunk_records
                      << " | Total so far: " << std::setw(6) << total_records;

            if (response.is_final()) {
                std::cout << " | FINAL";
            }
            std::cout << std::endl;

            // Display first few records from first chunk for verification
            if (chunks_received == 1 && chunk_records > 0) {
                std::cout << "\n--- Sample Records from Chunk 0 ---" << std::endl;
                int samples = std::min(3, chunk_records);
                for (int i = 0; i < samples; i++) {
                    const FireRecord& rec = response.records(i);
                    std::cout << "  [" << i << "] "
                              << rec.pollutant() << " "
                              << rec.concentration() << " " << rec.unit()
                              << " at (" << rec.latitude() << ", " << rec.longitude() << ") "
                              << rec.timestamp() << " - " << rec.site_name()
                              << std::endl;
                }
                std::cout << "-----------------------------------\n" << std::endl;
            }

            if (response.is_final()) {
                std::cout << "\nReceived final chunk indicator." << std::endl;
                break;
            }
        }

        Status status = reader->Finish();

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "\n========================================" << std::endl;
        std::cout << "QUERY COMPLETE" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Status:        " << (status.ok() ? "SUCCESS" : "FAILED") << std::endl;

        if (!status.ok()) {
            std::cout << "Error Code:    " << status.error_code() << std::endl;
            std::cout << "Error Message: " << status.error_message() << std::endl;
        } else {
            std::cout << "Total Chunks:  " << chunks_received << std::endl;
            std::cout << "Total Records: " << total_records << std::endl;
            std::cout << "Duration:      " << duration.count() << " ms" << std::endl;
            std::cout << "Throughput:    " << (duration.count() > 0 ? (total_records * 1000 / duration.count()) : 0)
                      << " records/sec" << std::endl;

            std::cout << "\nRecords by Process:" << std::endl;
            for (const auto& [process, count] : records_by_process) {
                std::cout << "  " << process << ": " << count << " records" << std::endl;
            }
        }
        std::cout << "========================================\n" << std::endl;
    }

private:
    std::unique_ptr<FireQueryService::Stub> stub_;
};

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <leader_host:port> [options]" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  --start <date>       Start date (YYYYMMDD), default: 20200810" << std::endl;
    std::cout << "  --end <date>         End date (YYYYMMDD), default: 20200815" << std::endl;
    std::cout << "  --pollutant <type>   Pollutant type (PM2.5, PM10, OZONE), default: all" << std::endl;
    std::cout << "  --max <n>            Maximum records, default: unlimited" << std::endl;
    std::cout << "  --chunk <n>          Chunk size, default: 500" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << program << " localhost:50051" << std::endl;
    std::cout << "  " << program << " localhost:50051 --pollutant PM2.5 --max 5000" << std::endl;
    std::cout << "  " << program << " localhost:50051 --start 20200901 --end 20200910" << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string leader_address = argv[1];

    // Parse command line arguments
    std::string date_start = "20200810";
    std::string date_end = "20200815";
    std::string pollutant = "";
    int max_records = -1;
    int chunk_size = 500;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--start" && i + 1 < argc) {
            date_start = argv[++i];
        } else if (arg == "--end" && i + 1 < argc) {
            date_end = argv[++i];
        } else if (arg == "--pollutant" && i + 1 < argc) {
            pollutant = argv[++i];
        } else if (arg == "--max" && i + 1 < argc) {
            max_records = std::stoi(argv[++i]);
        } else if (arg == "--chunk" && i + 1 < argc) {
            chunk_size = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    try {
        std::cout << "Connecting to leader at " << leader_address << "..." << std::endl;

        auto channel = grpc::CreateChannel(leader_address, grpc::InsecureChannelCredentials());
        FireQueryClient client(channel);

        // Generate request ID
        std::string request_id = "req_" + std::to_string(time(nullptr));

        // Execute query
        client.QueryFire(request_id, date_start, date_end, pollutant,
                        -90.0, 90.0, -180.0, 180.0, max_records, chunk_size);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
