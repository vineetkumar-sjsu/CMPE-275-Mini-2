# CMPE 275 Mini-Project 2

- **Course:** CMPE 275 - Enterprise Application Development
- **Project:** Multi-Process Distributed Query System with gRPC and Shared Memory

---

## Project Overview

This project implements a **distributed query processing system** for California wildfire air quality data using a multi-process architecture. The system demonstrates advanced concepts in distributed systems including:

- **Multi-process coordination** with hierarchical delegation
- **gRPC-based inter-process communication** with server-side streaming
- **Cross-language integration** (C++ and Python)
- **POSIX shared memory** for status coordination
- **Data partitioning** across distributed processes
- **Chunked streaming** for efficient memory management

### System Architecture

The system consists of **6 processes** organized in a 3-tier hierarchical structure:

```
Client (fire_client)
      ↓
Process A (Leader)                    Port 50051
      ├→ Process B (Team Green Leader) → Port 50052
      │     └→ Process C (Worker)       → Port 50053
      │
      └→ Process E (Team Pink Leader)  → Port 50055
            ├→ Process D (Team Leader)  → Port 50054
            └→ Process F (Python Worker) → Port 50056
```

**Key Features:**
- **Process A**: Central leader that receives client queries and delegates to team leaders
- **Processes B & E**: Team leaders that coordinate workers and process their data partitions
- **Processes C, D, F**: Worker processes that handle specific date ranges of data
- **Process F**: Implemented in Python to demonstrate cross-language interoperability
- **Chunked Streaming**: Results streamed in chunks of 500 records for memory efficiency
- **Shared Memory**: Used only for status coordination (not for query results)

### Dataset

The system processes California wildfire air quality sensor data from **August 10 to September 24, 2020** (43 days), partitioned across processes:
- **Process B**: 10 dates (Aug 10-22)
- **Process C**: 10 dates (Aug 23 - Sep 1)
- **Process D**: 6 dates (Sep 2-7)
- **Process E**: 6 dates (Sep 8-13)
- **Process F**: 11 dates (Sep 14-24)

---

## Technology Stack

| Component | Technology | Version |
|-----------|-----------|---------|
| Primary Language | C++17 | - |
| Secondary Language | Python | 3.x |
| RPC Framework | gRPC | 1.62.2 |
| Serialization | Protocol Buffers | 4.25.3 |
| Build System | CMake | 3.30+ |
| IPC | POSIX Shared Memory | - |

---

## Prerequisites

### Required Software

1. **C++ Compiler** with C++17 support:
   ```bash
   # Check if already installed
   g++ --version
   # or
   clang++ --version
   ```

2. **CMake** (version 3.10 or higher):
   ```bash
   cmake --version
   ```

3. **gRPC and Protocol Buffers**:

   **Option A: Using Homebrew (Recommended for macOS)**
   ```bash
   brew install grpc protobuf cmake
   ```

   **Option B: Using Anaconda**
   ```bash
   conda install -c conda-forge grpcio grpcio-tools protobuf
   ```

4. **Python 3.x** with required packages:
   ```bash
   pip install grpcio grpcio-tools protobuf
   ```

---

## Installation and Build Instructions

### Step 1: Clone the Repository

```bash
git clone git@github.com:vineetkumar-sjsu/CMPE-275-Mini-2.git
cd CMPE-275-Mini-2
```

### Step 2: Configure Data Path (Optional - Auto-configured!)

**🎉 No action needed!** The `start_all.sh` script automatically sets `FIRE_DATA_PATH` for you.

The system uses a priority order for finding data:
1. **FIRE_DATA_PATH** environment variable (auto-set by start_all.sh)
2. **data_path** from config files (fallback: `./fire-data`)

If you want to use a custom data location, you can set the environment variable:

```bash
# Option 1: Set manually
export FIRE_DATA_PATH=/path/to/your/fire-data

# Option 2: Use .env file
cp .env.example .env
# Edit .env and set FIRE_DATA_PATH, then:
source .env

# Option 3: Update config files (not recommended)
# Edit configs/process_*.json and change "data_path" field
```

**Note:** The fire-data directory contains 43 days of California wildfire air quality data (Aug 10 - Sep 24, 2020) with 516 CSV files (~180MB total).

### Step 3: Generate Protocol Buffer Code

```bash
chmod +x generate_proto.sh
./generate_proto.sh
```

**Expected Output:**
```
Generating C++ protobuf code...
Generating Python protobuf code...
Protobuf generation complete!
```

### Step 4: Build C++ Components

```bash
mkdir -p build
cd build
cmake ..
make -j4
cd ..
```

**Expected Output:**
```
[100%] Built target fire_query_proto
[100%] Built target leader_server
[100%] Built target team_leader_server
[100%] Built target worker_server
[100%] Built target fire_client
```

**Verify executables were created:**
```bash
ls -la build/
# You should see: leader_server, team_leader_server, worker_server, fire_client
```

### Step 5: Create Log Directory

```bash
mkdir -p logs
```

---

## Running the System

### Step 1: Start All 6 Processes

```bash
chmod +x start_all.sh
./start_all.sh
```

**Expected Output:**
```
Starting Fire Query System...
Starting Process A (Leader)... ✓
Starting Process B (Team Green Leader)... ✓
Starting Process C (Worker - Green)... ✓
Starting Process D (Team Pink Leader)... ✓
Starting Process E (Team Pink Leader)... ✓
Starting Process F (Python Worker)... ✓

All 6 processes started successfully!
Logs available in ./logs/
```

**Verify all processes are running:**
```bash
ps aux | grep -E "(leader_server|team_leader_server|worker_server)" | grep -v grep
```

You should see **6 processes** running on ports 50051-50056.

### Step 2: Run Test Query - Small Dataset (1,000 records)

```bash
./build/fire_client localhost 50051 20200810 20200815 PM2.5 1000 500
```

**Expected Output:**
```
========================================
Fire Query System - Client
========================================
Connecting to: localhost:50051
Request ID: req_XXXXXXXXXX
Date Range: 20200810 to 20200815
Pollutant: PM2.5
Max Records: 1000
Chunk Size: 500

Sending query...
Connected successfully.

Receiving chunks...
  Chunk 0: 500 records from B
  Chunk 1: 500 records from B
  Chunk 2: 0 records (final chunk)

========================================
Query Results Summary
========================================
Status: SUCCESS
Total Chunks Received: 3
Total Records: 1000
Duration: ~40ms
Throughput: ~25,000 records/sec

Sample Record:
  Pollutant: PM2.5
  Concentration: 20.4 UG/M3
  Location: (41.7561, -124.203)
  Timestamp: 2020-08-10T21:00
  Site: Crescent City
========================================
```

### Step 3: Run Test Query - Large Dataset (20,000 records)

```bash
./build/fire_client localhost 50051 20200810 20200924 PM2.5 50000 500
```

**Expected Output:**
```
========================================
Fire Query System - Client
========================================
Request ID: req_XXXXXXXXXX
Date Range: 20200810 to 20200924 (ALL DATES)

Receiving chunks...
  Chunk 0-9: Process B (5,000 records)
  Chunk 10-19: Process C (5,000 records)
  Chunk 20-29: Process E (5,000 records)
  Chunk 30-39: Process F (5,000 records) [PYTHON]
  Chunk 40: Final chunk

========================================
Query Results Summary
========================================
Status: SUCCESS
Total Chunks Received: 41
Total Records: 20,000
Duration: ~1,500ms
Throughput: ~13,000 records/sec

Records by Process:
  Process B: 5,000 records (Team Green Leader)
  Process C: 5,000 records (Team Green Worker)
  Process E: 5,000 records (Team Pink Leader)
  Process F: 5,000 records (Python Worker)
========================================
```

### Step 4: Stop All Processes

```bash
chmod +x stop_all.sh
./stop_all.sh
```

**Expected Output:**
```
Stopping all Fire Query System processes...
Stopped Process A (PID: XXXXX)
Stopped Process B (PID: XXXXX)
Stopped Process C (PID: XXXXX)
Stopped Process D (PID: XXXXX)
Stopped Process E (PID: XXXXX)
Stopped Process F (PID: XXXXX)

All processes stopped.
```

---

## Verification Checklist

Use this checklist to verify the system is working correctly:

### ✅ Build Verification
- [ ] All 4 C++ executables built without errors
- [ ] Python protobuf files generated (`fire_query_pb2.py`, `fire_query_pb2_grpc.py`)
- [ ] No compilation warnings

### ✅ Process Startup Verification
- [ ] All 6 processes started (check with `ps aux | grep server`)
- [ ] Log files created in `logs/` directory for all 6 processes
- [ ] No "connection refused" errors in logs
- [ ] Process A log shows connections to B and E
- [ ] Process B log shows connection to C
- [ ] Process E log shows connection to F

### ✅ Query Execution Verification
- [ ] Small query (1K records) completes in < 100ms
- [ ] Large query (20K records) completes in < 2 seconds
- [ ] All 4 data processes (B, C, E, F) contribute records
- [ ] Python Process F successfully returns data
- [ ] Chunked streaming working (500 records per chunk)
- [ ] Client receives final chunk indicator

### ✅ Data Correctness Verification
- [ ] Sample records show realistic data types (double for lat/lon, not strings)
- [ ] Correct pollutant filtering (PM2.5)
- [ ] Date range filtering works correctly
- [ ] No duplicate records across processes
- [ ] Total record count matches expected results

---

## Project Structure

```
CMPE-275-Mini-2/
├── proto/
│   └── fire_query.proto              # gRPC service and message definitions
├── src/
│   ├── common/
│   │   ├── config.hpp                # JSON configuration parser
│   │   └── fire_data_loader.hpp      # CSV data loader with filtering
│   ├── shmem/
│   │   └── status_manager.hpp        # POSIX shared memory wrapper
│   ├── servers/
│   │   ├── leader/
│   │   │   └── leader_server.cpp     # Process A - Central leader
│   │   ├── team_green/
│   │   │   ├── team_leader_server.cpp # Process B - Team Green leader
│   │   │   └── worker_server.cpp      # Process C - Worker
│   │   └── team_pink/
│   │       └── worker_server.py       # Process F - Python worker
│   └── client/
│       └── fire_client.cpp           # Query client with streaming
├── configs/
│   ├── process_a.json                # Configuration for each process
│   ├── process_b.json
│   ├── process_c.json
│   ├── process_d.json
│   ├── process_e.json
│   └── process_f.json
├── fire-data/                        # Fire sensor data (43 date dirs, 516 CSV files)
├── logs/                             # Runtime log files (generated)
├── build/                            # Build artifacts (generated)
├── CMakeLists.txt                    # CMake build configuration
├── generate_proto.sh                 # Protocol buffer generation script
├── build.sh                          # Build all components (C++ and Python)
├── start_all.sh                      # Start all 6 processes
├── stop_all.sh                       # Stop all processes
├── clean.sh                          # Clean all build artifacts
├── requirements.txt                  # Python dependencies
├── .env.example                      # Environment variable template
├── .gitignore                        # Git ignore rules
└── README.md                         # This file
```

---

## Assignment Requirements Compliance

This implementation satisfies all assignment requirements:

| Requirement | Implementation | Verification |
|-------------|----------------|--------------|
| **6 Processes (A-F)** | ✅ Implemented | `ps aux \| grep server` shows 6 processes |
| **Team Structure** | ✅ Green (B,C) and Pink (D,E,F) | Check `configs/*.json` files |
| **Overlay Network** | ✅ Edges: AB, BC, BD, AE, ED, EF | Verified in process logs |
| **gRPC Communication** | ✅ All inter-process via gRPC | Check `.proto` file and implementations |
| **C++ Servers** | ✅ Processes A, B, C, D, E | Built executables in `build/` |
| **Python Server** | ✅ Process F | `src/servers/team_pink/worker_server.py` |
| **C++ Client** | ✅ Implemented | `build/fire_client` executable |
| **Chunked Streaming** | ✅ 500 records/chunk | Test output shows 41 chunks |
| **Shared Memory** | ✅ Status only (not results) | `src/shmem/status_manager.hpp` |
| **No Hardcoding** | ✅ All configs in JSON | 6 JSON config files |
| **CMake Build** | ✅ CMakeLists.txt | Builds all targets |
| **Multi-Host Ready** | ✅ Configurable hosts | Update JSON with IPs |
| **Realistic Data Types** | ✅ double, int32, bool | Check `.proto` definitions |
| **Synchronous gRPC** | ✅ No async APIs | Code inspection |
| **No Results in Shmem** | ✅ Only status | StatusManager stores health only |
| **Tree Structure** | ✅ 3-tier: A→(B,E)→(C,F) | Architecture diagram |

---

## Performance Metrics

Based on testing with the provided dataset:

| Metric | Small Query (1K) | Large Query (20K) |
|--------|------------------|-------------------|
| **Records** | 1,000 | 20,000 |
| **Duration** | ~40 ms | ~1,500 ms |
| **Throughput** | ~25,000 rec/sec | ~13,000 rec/sec |
| **Chunks** | 3 | 41 |
| **Processes Used** | 1 (B only) | 4 (B, C, E, F) |

**Key Observations:**
- Sub-second response for 1,000 records
- Linear scalability with data size
- Load balanced across multiple processes
- Python process (F) performs comparably to C++ processes

---

## Troubleshooting

### Issue: "Connection refused" errors

**Solution:**
```bash
# Check if processes are running
ps aux | grep server

# Check if ports are in use
lsof -i :50051-50056

# Restart the system
./stop_all.sh
./start_all.sh
```

### Issue: "Failed to attach to shared memory"

**Solution:**
```bash
# Ensure Process A (leader) starts first
./stop_all.sh
# Wait 2 seconds, then
./start_all.sh
```

### Issue: Build fails with "Protobuf not found"

**Solution:**
```bash
# For Anaconda users, specify paths explicitly:
cmake .. -DProtobuf_INCLUDE_DIR=/opt/anaconda3/include \
         -DProtobuf_LIBRARIES=/opt/anaconda3/lib/libprotobuf.dylib
make -j4
```

### Issue: Python import errors

**Solution:**
```bash
# Ensure protobuf Python files are generated
./generate_proto.sh

# Install Python dependencies
pip install grpcio grpcio-tools protobuf
```

### Issue: No data returned from queries

**Solution:**
```bash
# Verify fire data directory exists
ls -la fire-data/

# Check process logs for errors
cat logs/process_*.log
```

### Issue: Build issues or want a clean slate

**Solution:**
```bash
# Use the clean script to remove all build artifacts
./clean.sh

# This removes:
# - build/ directory
# - Generated protobuf files (C++ and Python)
# - Python cache (__pycache__, *.pyc)
# - Log files
# - Compiled objects (*.o, *.so, *.dylib)
# - CMake cache files
# - Temporary files

# Then rebuild from scratch:
./generate_proto.sh
mkdir build && cd build && cmake .. && make -j4 && cd ..
# Or simply:
./build.sh
```

---

## Implementation Highlights

### 1. Hierarchical Delegation
Process A delegates queries to team leaders B and E, which further delegate to their workers (C and F). This creates a tree structure that scales efficiently.

### 2. Data Partitioning
The 43 days of fire data are partitioned across 5 processes (B, C, D, E, F) with no overlaps. Each process loads only its assigned dates, enabling parallel query processing.

### 3. Chunked Streaming
Results are streamed in fixed-size chunks (500 records) instead of loading all data into memory. This enables:
- Low memory footprint
- Early processing on client side
- Better responsiveness for large queries

### 4. Cross-Language Integration
Process F is implemented in Python while all others are C++. This demonstrates gRPC's language-agnostic design - both implementations use the same `.proto` definition and communicate seamlessly.

### 5. Configuration-Driven Architecture
All process IDs, ports, hosts, and data partitions are defined in JSON configuration files. This enables:
- Easy multi-host deployment (just update IPs in JSON)
- No code recompilation for configuration changes
- Clear separation of code and configuration

### 6. Error Handling and Logging
Each process logs its operations to individual log files, making debugging and monitoring straightforward. All gRPC errors are properly handled with status codes.

---

## Testing Evidence

The system has been thoroughly tested with the following scenarios:

1. **Single-process query** (dates 20200810-20200815): Only Process B contributes
2. **Multi-process query** (dates 20200810-20200924): All 4 data processes contribute
3. **Python integration**: Process F successfully returns 5,000 records
4. **Chunked streaming**: 41 chunks with 500 records each verified
5. **Error handling**: Invalid dates, connection failures handled gracefully

Log files in `logs/` directory provide complete execution traces.

---

## Pending Enhancements

1. **Fault Tolerance**: Worker failover and retry logic
2. **Query Optimization**: Predicate pushdown, query caching
3. **Load Balancing**: Dynamic work distribution based on process load
