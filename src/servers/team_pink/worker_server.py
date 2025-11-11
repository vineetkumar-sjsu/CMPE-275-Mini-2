#!/usr/bin/env python3
"""
Python Worker Server (Process F) - Team Pink
Demonstrates cross-language gRPC integration
"""

import sys
import json
import grpc
import time
import csv
import os
from concurrent import futures
from pathlib import Path

# Add proto path
proto_path = str(Path(__file__).parent.parent.parent.parent / 'proto')
if proto_path not in sys.path:
    sys.path.insert(0, proto_path)

import fire_query_pb2
import fire_query_pb2_grpc


class WorkerServiceImpl(fire_query_pb2_grpc.FireQueryServiceServicer):
    def __init__(self, config):
        self.config = config
        self.process_id = config['process_id']
        self.team = config['team']
        self.data_path = config['data_path']
        self.owned_dates = config['data_partitioning']['owned_dates']
        self.chunk_size = config['chunk_config']['default_chunk_size']
        self.pending_requests = 0
        self.completed_requests = 0

        print(f"Worker Process {self.process_id} (Team {self.team}) starting...")
        print(f"Listening on {config['listen_host']}:{config['listen_port']}")
        print(f"Data partition: {' '.join(self.owned_dates)}")

    def DelegateQuery(self, request, context):
        """Handle delegated query from team leader"""
        print(f"\n[Worker {self.process_id}] Received delegation {request.request_id} from {request.delegating_process}")

        self.pending_requests += 1

        # Deserialize original query
        original_query = fire_query_pb2.QueryRequest()
        original_query.ParseFromString(request.original_query)

        # Determine dates to process
        dates_to_process = self._select_dates_to_process(original_query)
        print(f"  [Worker {self.process_id}] Processing {len(dates_to_process)} dates")

        if not dates_to_process:
            print(f"  [Worker {self.process_id}] No matching dates in partition")
            self.pending_requests -= 1
            self.completed_requests += 1
            return

        # Load and process data
        start_time = time.time()
        records = self._load_data(
            dates_to_process,
            original_query.pollutant_type,
            original_query.latitude_min,
            original_query.latitude_max,
            original_query.longitude_min,
            original_query.longitude_max,
            original_query.max_records
        )
        duration = (time.time() - start_time) * 1000  # Convert to ms

        print(f"  [Worker {self.process_id}] Loaded {len(records)} records in {duration:.0f}ms")

        # Send records in chunks
        chunk_count = 0
        for i in range(0, len(records), self.chunk_size):
            chunk_end = min(i + self.chunk_size, len(records))
            chunk_records = records[i:chunk_end]

            response = fire_query_pb2.DelegationResponse()
            response.request_id = request.request_id
            response.chunk_number = chunk_count
            response.is_final = False
            response.responding_process = self.process_id

            # Add records to response
            for record_data in chunk_records:
                record = response.records.add()
                self._populate_fire_record(record, record_data)

            yield response

            print(f"  [Worker {self.process_id}] Sent chunk {chunk_count} with {len(chunk_records)} records")
            chunk_count += 1

            # Simulate processing time
            time.sleep(0.05)

        print(f"[Worker {self.process_id}] Delegation {request.request_id} complete. Sent {chunk_count} chunks")

        self.pending_requests -= 1
        self.completed_requests += 1

    def HealthCheck(self, request, context):
        """Health check endpoint"""
        response = fire_query_pb2.HealthResponse()
        response.responding_process = self.process_id
        response.is_healthy = True
        response.pending_requests = self.pending_requests
        response.active_workers = 1
        return response

    def CancelQuery(self, request, context):
        """Handle query cancellation"""
        print(f"[Worker {self.process_id}] Cancel request for {request.request_id}")
        response = fire_query_pb2.CancelResponse()
        response.request_id = request.request_id
        response.cancelled = True
        return response

    def QueryFire(self, request, context):
        """Not implemented for workers"""
        context.set_code(grpc.StatusCode.UNIMPLEMENTED)
        context.set_details("Workers don't accept direct queries")
        return fire_query_pb2.QueryResponse()

    def _select_dates_to_process(self, query):
        """Select dates that match query range and are owned by this worker"""
        result = []
        for date in self.owned_dates:
            if query.date_start <= date <= query.date_end:
                result.append(date)
        return result

    def _load_data(self, dates, pollutant_filter, lat_min, lat_max, lon_min, lon_max, max_records):
        """Load fire data from CSV files"""
        results = []

        for date in dates:
            date_dir = os.path.join(self.data_path, date)
            if not os.path.exists(date_dir):
                print(f"Warning: Date directory not found: {date_dir}")
                continue

            # Load all CSV files for this date
            for csv_file in Path(date_dir).glob('*.csv'):
                records = self._load_csv(
                    str(csv_file),
                    pollutant_filter,
                    lat_min, lat_max,
                    lon_min, lon_max,
                    max_records - len(results) if max_records > 0 else -1
                )
                results.extend(records)

                if max_records > 0 and len(results) >= max_records:
                    return results[:max_records]

        return results

    def _load_csv(self, csv_path, pollutant_filter, lat_min, lat_max, lon_min, lon_max, max_records):
        """Load and parse a single CSV file"""
        results = []

        try:
            with open(csv_path, 'r') as f:
                reader = csv.reader(f, quotechar='"')
                for row in reader:
                    if max_records > 0 and len(results) >= max_records:
                        break

                    if len(row) < 13:
                        continue

                    try:
                        lat = float(row[0])
                        lon = float(row[1])
                        pollutant = row[3]

                        # Apply filters
                        if pollutant_filter and pollutant != pollutant_filter:
                            continue

                        if lat < lat_min or lat > lat_max:
                            continue

                        if lon < lon_min or lon > lon_max:
                            continue

                        # Parse full record
                        record = {
                            'latitude': lat,
                            'longitude': lon,
                            'timestamp': row[2],
                            'pollutant': pollutant,
                            'concentration': float(row[4]),
                            'unit': row[5],
                            'raw_concentration': float(row[6]),
                            'aqi': int(row[7]),
                            'aqi_category': int(row[8]),
                            'site_name': row[9],
                            'agency': row[10],
                            'site_id': row[11],
                            'full_site_id': row[12]
                        }

                        results.append(record)

                    except (ValueError, IndexError) as e:
                        # Skip malformed records
                        continue

        except FileNotFoundError:
            print(f"Warning: CSV file not found: {csv_path}")

        return results

    def _populate_fire_record(self, proto_record, data):
        """Convert Python dict to protobuf FireRecord"""
        proto_record.latitude = data['latitude']
        proto_record.longitude = data['longitude']
        proto_record.timestamp = data['timestamp']
        proto_record.pollutant = data['pollutant']
        proto_record.concentration = data['concentration']
        proto_record.unit = data['unit']
        proto_record.raw_concentration = data['raw_concentration']
        proto_record.aqi = data['aqi']
        proto_record.aqi_category = data['aqi_category']
        proto_record.site_name = data['site_name']
        proto_record.agency = data['agency']
        proto_record.site_id = data['site_id']
        proto_record.full_site_id = data['full_site_id']


def load_config(config_file):
    """Load configuration from JSON file"""
    with open(config_file, 'r') as f:
        return json.load(f)


def serve(config_file):
    """Start the Python worker server"""
    config = load_config(config_file)

    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    fire_query_pb2_grpc.add_FireQueryServiceServicer_to_server(
        WorkerServiceImpl(config), server
    )

    listen_addr = f"{config['listen_host']}:{config['listen_port']}"
    server.add_insecure_port(listen_addr)
    server.start()

    print(f"\n*** Python Worker server listening on {listen_addr} ***\n")

    try:
        server.wait_for_termination()
    except KeyboardInterrupt:
        print("\nShutting down Python worker server...")
        server.stop(0)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <config_file>")
        sys.exit(1)

    serve(sys.argv[1])
