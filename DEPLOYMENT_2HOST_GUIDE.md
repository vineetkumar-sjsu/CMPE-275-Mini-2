# 2-Host Distributed Deployment Guide
## Fire Query System - Multi-Host Experiment

This guide will help you deploy the Fire Query System across 2 hosts connected via a Gigabit switch.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         NETWORK TOPOLOGY                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  MacBook (Mac)                    Netgear 5-Port               │
│  ┌──────────────┐                Gigabit Switch                │
│  │ Process A    │◄────────────┐                                │
│  │ (Leader)     │              │                                │
│  │ Port: 50051  │              │                                │
│  └──────┬───────┘              │                                │
│         │                      │                                │
│         ├─────local────┐       │                                │
│         │              │       │                                │
│  ┌──────▼───────┐ ┌───▼──────┐│                                │
│  │ Process B    │ │Process C ││                                │
│  │ (Team Green  │ │(Green    ││                                │
│  │  Leader)     │ │ Worker)  ││                                │
│  │ Port: 50052  │ │Port:50053││                                │
│  └──────┬───────┘ └──────────┘│                                │
│         │                      │                                │
│         │                      │                                │
│         │ network──────────────┼──────────────────┐             │
│         │                      │                  │             │
│         │              Windows (WSL Ubuntu)       │             │
│         │              ┌──────────────┐           │             │
│         └──────────────►  Process E   │           │             │
│                        │ (Team Pink   │           │             │
│                        │  Leader)     │           │             │
│                        │ Port: 50055  │           │             │
│                        └──────┬───────┘           │             │
│                               │                   │             │
│                        ┌──────┴──────┬────────────┘             │
│                        │             │                          │
│                 ┌──────▼──────┐ ┌───▼──────┐                   │
│                 │ Process D   │ │Process F │                   │
│                 │ (Pink       │ │(Pink     │                   │
│                 │  Worker)    │ │ Worker - │                   │
│                 │ Port: 50054 │ │ Python)  │                   │
│                 │             │ │Port:50056│                   │
│                 └─────────────┘ └──────────┘                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Process Distribution

**MacBook (Host 1):**
- Process A (Leader) - Port 50051
- Process B (Team Green Leader) - Port 50052
- Process C (Team Green Worker) - Port 50053

**Windows/WSL (Host 2):**
- Process E (Team Pink Leader) - Port 50055
- Process D (Team Pink Worker) - Port 50054
- Process F (Team Pink Worker - Python) - Port 50056

### Data Partitioning

**Team Green (MacBook):**
- Process B: Aug 10-22 (10 dates)
- Process C: Aug 23 - Sep 1 (10 dates)

**Team Pink (Windows):**
- Process D: Sep 2-7 (6 dates)
- Process E: Sep 8-13 (6 dates)
- Process F: Sep 14-24 (11 dates)

---

## Prerequisites Checklist

### Hardware
- [ ] Netgear 5-port Gigabit switch
- [ ] 2x Ethernet cables (Cat5e or Cat6)
- [ ] MacBook with Ethernet adapter (if needed)
- [ ] Windows laptop/desktop

### Software - Windows/WSL System
- [ ] WSL Ubuntu installed
- [ ] C++ compiler (g++)
- [ ] CMake 3.15+
- [ ] gRPC and Protocol Buffers installed
- [ ] Python 3.x with grpcio and protobuf packages
- [ ] Project built successfully

### Software - MacBook
- [ ] Xcode Command Line Tools or Homebrew
- [ ] C++ compiler (clang++)
- [ ] CMake 3.15+
- [ ] gRPC and Protocol Buffers (via Homebrew)
- [ ] Python 3.x
- [ ] Project source code copied
- [ ] fire-data directory copied

### Network
- [ ] Both systems connected to Gigabit switch
- [ ] Static IPs assigned or DHCP reservation configured
- [ ] Firewall rules allowing ports 50051-50056

---

## Step-by-Step Deployment Procedure

### PHASE 1: Network Setup

#### Step 1.1: Connect Hardware

1. Power on the Netgear 5-port Gigabit switch
2. Connect Windows laptop to switch (any port)
3. Connect MacBook to switch (any port)
4. Verify link lights are GREEN on both ports (indicating Gigabit connection)

#### Step 1.2: Find IP Addresses

**On Windows/WSL:**
```bash
# In WSL terminal
ip addr show eth0
# OR
hostname -I
```
Note the IP address (e.g., 192.168.1.100)

**On MacBook:**
```bash
# In Terminal
ifconfig en0 | grep "inet "
# OR
ipconfig getifaddr en0
```
Note the IP address (e.g., 192.168.1.101)

#### Step 1.3: Test Network Connectivity

**From MacBook, ping Windows:**
```bash
ping <WINDOWS_IP>
# Example: ping 192.168.1.100
```
Press Ctrl+C after a few successful pings.

**From Windows/WSL, ping MacBook:**
```bash
ping <MAC_IP>
# Example: ping 192.168.1.101
```
Press Ctrl+C after a few successful pings.

**Expected:** Both should show 0% packet loss and <1ms latency on Gigabit LAN.

---

### PHASE 2: Configuration Updates

#### Step 2.1: Update MacBook Configuration

**On MacBook, edit the leader config file:**

```bash
# Navigate to project directory
cd ~/path/to/v1-baseline

# Edit Process A configuration
nano configs-2host/mac/process_a.json
```

Replace `WINDOWS_IP_HERE` with your Windows IP address:

```json
{
  "process_id": "A",
  "role": "leader",
  "listen_host": "0.0.0.0",
  "listen_port": 50051,
  "data_path": "./fire-data",
  "team": null,
  "is_team_leader": false,
  "edges": [
    {
      "to": "B",
      "host": "localhost",
      "port": 50052,
      "relationship": "team_leader",
      "team": "green"
    },
    {
      "to": "E",
      "host": "192.168.1.100",    <-- REPLACE WITH YOUR WINDOWS IP
      "port": 50055,
      "relationship": "team_leader",
      "team": "pink"
    }
  ],
  "data_partitioning": {
    "strategy": "date_range",
    "owned_dates": []
  },
  "chunk_config": {
    "default_chunk_size": 500,
    "max_chunk_size": 2000,
    "min_chunk_size": 100
  }
}
```

Save and exit (Ctrl+O, Enter, Ctrl+X in nano).

#### Step 2.2: Verify Windows Configuration

**On Windows/WSL:**

The Windows configuration files are already created and don't need IP updates since all Team Pink processes are on the same host (using localhost).

Verify files exist:
```bash
ls -la configs-2host/windows/
```

You should see:
- process_e.json
- process_d.json
- process_f.json
- start_pink_team.sh
- stop_pink_team.sh

---

### PHASE 3: Firewall Configuration

#### Step 3.1: Windows Firewall

**On Windows (PowerShell as Administrator):**

```powershell
# Allow ports 50054, 50055, 50056 for WSL
New-NetFirewallRule -DisplayName "Fire Query - Team Pink" -Direction Inbound -LocalPort 50054,50055,50056 -Protocol TCP -Action Allow
```

**OR using Windows Defender GUI:**
1. Open Windows Defender Firewall
2. Click "Advanced settings"
3. Click "Inbound Rules" → "New Rule"
4. Port → TCP → Specific ports: 50054,50055,50056
5. Allow the connection
6. Apply to all profiles
7. Name: "Fire Query Team Pink"

#### Step 3.2: MacBook Firewall

**On MacBook:**

If firewall is enabled:
```bash
# Check firewall status
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --getglobalstate

# If enabled, allow the binaries
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add /path/to/build/leader_server
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add /path/to/build/team_leader_server
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add /path/to/build/worker_server
```

Or disable temporarily for testing:
```bash
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --setglobalstate off
```

---

### PHASE 4: Build and Prepare

#### Step 4.1: Build on Windows/WSL

**On Windows/WSL:**

```bash
# Navigate to project
cd /mnt/c/Vineet/v1-baseline

# Stop any running processes
./stop_all_unix.sh 2>/dev/null || true

# Generate proto and build
./generate_proto_unix.sh
mkdir -p build
cd build
cmake ..
make -j4
cd ..

# Make scripts executable
chmod +x configs-2host/windows/*.sh

# Verify binaries exist
ls -la build/worker_server build/team_leader_server
ls -la src/servers/team_pink/worker_server.py
```

#### Step 4.2: Build on MacBook

**On MacBook:**

```bash
# Navigate to project (adjust path as needed)
cd ~/v1-baseline

# Generate proto files
./generate_proto.sh

# Build
mkdir -p build
cd build
cmake ..
make -j4
cd ..

# Make scripts executable
chmod +x configs-2host/mac/*.sh

# Verify binaries exist
ls -la build/leader_server build/team_leader_server build/worker_server
```

---

### PHASE 5: Start Services

#### Step 5.1: Start Team Pink (Windows First)

**On Windows/WSL:**

```bash
cd /mnt/c/Vineet/v1-baseline

# Start Team Pink
./configs-2host/windows/start_pink_team.sh
```

**Expected output:**
```
========================================
Starting Team Pink on Windows/WSL
========================================
FIRE_DATA_PATH=/mnt/c/Vineet/v1-baseline/fire-data

Starting processes...

Starting Process F (Python Worker - Team Pink)...
  PID: XXXX (check logs/process_f.log)
Starting Process D (Worker - Team Pink)...
  PID: XXXX (check logs/process_d.log)
Starting Process E (Team Leader - Pink)...
  PID: XXXX (check logs/process_e.log)

========================================
Team Pink processes started!
========================================
```

**Verify Team Pink is running:**
```bash
# Check logs
tail logs/process_e.log

# Should show:
# Team Leader Process E (Team ) starting...
# Listening on 0.0.0.0:50055
# *** Team Leader server listening on 0.0.0.0:50055 ***

# Check if ports are listening
netstat -tuln | grep -E '5005[456]'
```

You should see ports 50054, 50055, 50056 in LISTEN state.

#### Step 5.2: Start Leader + Team Green (MacBook Second)

**On MacBook:**

```bash
cd ~/v1-baseline

# Start Leader + Team Green
./configs-2host/mac/start_green_and_leader.sh
```

**Expected output:**
```
========================================
Starting Leader + Team Green on MacBook
========================================
FIRE_DATA_PATH=/Users/yourusername/v1-baseline/fire-data

Starting processes...

Starting Process C (Worker - Team Green)...
  PID: XXXX (check logs/process_c.log)
Starting Process B (Team Leader - Green)...
  PID: XXXX (check logs/process_b.log)
Starting Process A (Leader)...
  PID: XXXX (check logs/process_a.log)

========================================
All processes started!
========================================
```

**Verify Leader is running and connected:**
```bash
# Check leader log
tail -20 logs/process_a.log

# Should show:
# Leader Process A starting...
# Listening on 0.0.0.0:50051
# Connected to team leader B (green) at localhost:50052
# Connected to team leader E (pink) at 192.168.1.100:50055
# *** Leader server listening on 0.0.0.0:50051 ***
```

**IMPORTANT:** If you see "Failed to connect to team leader E", check:
1. Team Pink is running on Windows
2. Firewall on Windows allows port 50055
3. IP address in process_a.json is correct
4. Network connectivity (ping test)

---

### PHASE 6: Test the System

#### Step 6.1: Simple Query Test

**On MacBook:**

```bash
cd ~/v1-baseline

# Run a simple query
./build/fire_client localhost:50051 --start 20200810 --end 20200815 --chunk 500
```

**Expected output:**
```
========================================
FIRE QUERY REQUEST
========================================
Request ID:    req_XXXXXXXXXX
Date Range:    20200810 to 20200815
...

Chunk   0 | Source: B | Records:  500 | Total so far:    500
Chunk   1 | Source: B | Records:  500 | Total so far:   1000
...

========================================
QUERY COMPLETE
========================================
Status:        SUCCESS
Total Chunks:  XXX
Total Records: XXXXX
Duration:      XXX ms
Throughput:    XXXXX records/sec

Records by Process:
  A: 0 records
  B: XXXXX records
========================================
```

#### Step 6.2: Cross-Team Query Test

**Query data from both teams:**

```bash
# Query Aug 10 to Sep 20 (spans both Green and Pink teams)
./build/fire_client localhost:50051 --start 20200810 --end 20200920 --chunk 500
```

**Expected:** You should see records from both Process B (Green) and Process E (Pink):

```
Records by Process:
  A: 0 records
  B: XXXXX records (Team Green on MacBook)
  E: XXXXX records (Team Pink on Windows)
  D: XXXXX records (Team Pink on Windows)
  F: XXXXX records (Team Pink on Windows)
```

#### Step 6.3: Full Dataset Query

**Query the entire date range:**

```bash
./build/fire_client localhost:50051 --start 20200810 --end 20200924 --chunk 500
```

**Expected:**
- ~300,000+ total records
- Duration: ~1-3 seconds
- Records from all processes across both hosts

---

### PHASE 7: Run Benchmark and Analysis

#### Step 7.1: Run Benchmark

**On MacBook:**

```bash
cd ~/v1-baseline

# Run 10-iteration benchmark
python3 benchmark.py
```

**Expected output:**
```
Command: ./build/fire_client localhost:50051 --start 20200810 --end 20200924 --chunk 500
Iterations: 10

Run  1:      XXXX ms   ClientRSS_max:     XXX KB   TotalServerRSS_max:     XXX KB
Run  2:      XXXX ms   ClientRSS_max:     XXX KB   TotalServerRSS_max:     XXX KB
...
Run 10:      XXXX ms   ClientRSS_max:     XXX KB   TotalServerRSS_max:     XXX KB
-----------------------------
Latency (ms):               min=XXX   avg=XXX   max=XXX
```

**Note:** Network latency will be higher than single-host (expect +10-50ms due to network hops).

#### Step 7.2: Analyze Metrics

**On MacBook:**

```bash
# Analyze performance metrics
python3 analyze_metrics.py
```

**Expected output:**
```
== TTFC since START_DELEGATE ==
TTFC-green: n=XX ms
  p50=XXX  p95=XXX  min=XXX  max=XXX
TTFC-pink: n=XX ms
  p50=XXX  p95=XXX  min=XXX  max=XXX

== End-to-end latency by request size ==
...
```

**Key metrics to observe:**
- TTFC-green vs TTFC-pink (should be similar)
- Network overhead (compare to single-host baseline)
- Chunk gap times
- Team load distribution

---

### PHASE 8: Collect Metrics from Both Hosts

#### Step 8.1: Metrics on MacBook

**The metrics are already on MacBook in logs/ directory:**

```bash
ls -lh logs/metrics*.csv
```

You'll see:
- metrics-leader-A-*.csv (Leader metrics)
- metrics-team_leader-B-*.csv (Team Green Leader)
- metrics-worker-C-*.csv (Team Green Worker)

#### Step 8.2: Copy Metrics from Windows

**Option A: Using SCP (if SSH is enabled on Windows/WSL):**

On MacBook:
```bash
# Copy Team Pink metrics from Windows to MacBook
scp vineet@<WINDOWS_IP>:/mnt/c/Vineet/v1-baseline/logs/metrics-*.csv ./logs/
```

**Option B: Manual file transfer:**

1. On Windows/WSL, compress metrics:
```bash
cd /mnt/c/Vineet/v1-baseline
tar -czf pink_metrics.tar.gz logs/metrics-team_leader-E-*.csv logs/metrics-worker-D-*.csv logs/metrics-worker-F-*.csv
```

2. Transfer file via USB drive, shared folder, or network share

3. On MacBook, extract:
```bash
tar -xzf pink_metrics.tar.gz
```

#### Step 8.3: Unified Analysis

**On MacBook with all metrics:**

```bash
# Analyze all metrics (both hosts)
python3 analyze_metrics.py
```

Now you'll see metrics from both teams!

---

## Troubleshooting

### Issue: Leader can't connect to Team Pink

**Symptoms:**
```
Failed to connect to team leader E (pink) at 192.168.1.100:50055
```

**Solutions:**
1. Verify Team Pink is running on Windows:
   ```bash
   # On Windows/WSL
   ps aux | grep -E 'worker_server|team_leader_server|worker_server.py'
   netstat -tuln | grep 50055
   ```

2. Check network connectivity:
   ```bash
   # On MacBook
   ping 192.168.1.100
   telnet 192.168.1.100 50055
   ```

3. Check Windows firewall:
   ```bash
   # On Windows PowerShell (as Admin)
   Get-NetFirewallRule | Where-Object {$_.LocalPort -eq 50055}
   ```

4. Verify IP address in config:
   ```bash
   # On MacBook
   grep -A2 '"to": "E"' configs-2host/mac/process_a.json
   ```

### Issue: No data from Team Pink

**Symptoms:**
- Query completes but only shows records from Team Green (Process B, C)
- Records by Process shows 0 for E, D, F

**Solutions:**
1. Check Team Pink logs:
   ```bash
   # On Windows
   tail -50 logs/process_e.log
   tail -50 logs/process_d.log
   tail -50 logs/process_f.log
   ```

2. Verify fire-data exists on Windows:
   ```bash
   # On Windows
   ls -la fire-data/ | head -20
   ls -la fire-data/20200914/  # Should show CSV files
   ```

3. Run direct query to Team Pink:
   ```bash
   # On MacBook, query only Pink dates
   ./build/fire_client localhost:50051 --start 20200914 --end 20200920
   ```

### Issue: High latency / slow queries

**Symptoms:**
- Query takes >5 seconds for full dataset
- Benchmark shows avg latency >2000ms

**Solutions:**
1. Verify Gigabit connection (not 100Mbps):
   ```bash
   # On MacBook
   networksetup -getmedia en0
   # Should show "1000baseT" not "100baseTX"

   # On Windows
   ethtool eth0 | grep Speed
   # Should show "Speed: 1000Mb/s"
   ```

2. Check switch is Gigabit and cables are Cat5e/Cat6

3. Monitor network during query:
   ```bash
   # On MacBook
   nload
   # OR
   iftop -i en0
   ```

4. Check for packet loss:
   ```bash
   ping -c 100 192.168.1.100
   # Should show 0% packet loss
   ```

### Issue: Shared memory errors

**Symptoms:**
```
Error: Failed to attach to shared memory segment
```

**Solution:**
Shared memory is local to each host, so:
- Team Pink processes share memory on Windows
- Team Green + Leader share memory on MacBook

This is EXPECTED and CORRECT. Ignore shared memory warnings from remote teams.

---

## Network Performance Expectations

### Baseline (Single Host)
- Query latency: 100-200ms
- Throughput: 80,000-100,000 records/sec
- TTFC: 50-100ms

### 2-Host Gigabit LAN
- Query latency: 150-300ms (+50-100ms network overhead)
- Throughput: 60,000-80,000 records/sec
- TTFC: 100-200ms
- Network overhead per delegation: 5-20ms

### Network Bandwidth Usage
- Full dataset query: ~50-100 MB transferred
- Gigabit LAN: 125 MB/s theoretical max
- Expected utilization: 30-50% during active query

---

## Cleanup

### Stop All Processes

**On MacBook:**
```bash
cd ~/v1-baseline
./configs-2host/mac/stop_green_and_leader.sh

# OR manually
pkill -f 'leader_server|team_leader_server|worker_server'
```

**On Windows/WSL:**
```bash
cd /mnt/c/Vineet/v1-baseline
./configs-2host/windows/stop_pink_team.sh

# OR manually
pkill -f 'worker_server|team_leader_server|worker_server.py'
```

### Clean Shared Memory

**On MacBook:**
```bash
ipcs -m | grep 2275
# If you see shared memory segment, remove it:
ipcrm -M 2275
```

**On Windows/WSL:**
```bash
ipcs -m | grep 2275
# If you see shared memory segment, remove it:
ipcrm -M 2275
```

---

## Summary of Commands

### Quick Start Sequence

**Terminal 1 - Windows/WSL:**
```bash
cd /mnt/c/Vineet/v1-baseline
./configs-2host/windows/start_pink_team.sh
tail -f logs/process_e.log  # Monitor until "listening on 0.0.0.0:50055"
```

**Terminal 2 - MacBook:**
```bash
cd ~/v1-baseline
./configs-2host/mac/start_green_and_leader.sh
tail -f logs/process_a.log  # Monitor until "Connected to team leader E"
```

**Terminal 3 - MacBook (Client):**
```bash
cd ~/v1-baseline
./build/fire_client localhost:50051 --start 20200810 --end 20200924
```

**Terminal 4 - MacBook (Benchmark):**
```bash
cd ~/v1-baseline
python3 benchmark.py
python3 analyze_metrics.py
```

---

## Next Steps / Experiments

1. **3-Host Deployment**: Add a third host with only workers
2. **Load Testing**: Multiple concurrent clients
3. **Failure Testing**: Kill one team mid-query and observe behavior
4. **Network Throttling**: Simulate slower network (100Mbps, 10Mbps)
5. **WAN Simulation**: Add artificial latency (tc qdisc on Linux)
6. **Metrics Analysis**: Compare single-host vs 2-host performance
7. **Data Skew**: Modify partitioning to create imbalanced loads

---

## Architecture Notes

### Why This Topology?

**Client fires query on MacBook** → This is the most natural setup since:
- MacBook has the Leader (Process A)
- Client connects to Leader on localhost:50051
- Leader delegates to local Team Green (fast) and remote Team Pink (network)

**Team Pink on Windows** → Demonstrates:
- Cross-platform deployment (macOS ↔ Windows/Linux)
- Cross-language (C++ on Mac ↔ C++ + Python on Windows)
- Network-based team coordination
- Real distributed system behavior

### Data Flow

```
Client (Mac)
  ↓
Leader A (Mac) ─┬─ local RPC ──→ Team Green B (Mac) ─→ Worker C (Mac)
                │
                └─ network RPC ─→ Team Pink E (Windows) ─┬─→ Worker D (Windows)
                                                          └─→ Worker F (Windows/Python)
```

All responses stream back through the same path, aggregating at Leader A before reaching Client.

---

## File Reference

### Configuration Files Created

**Windows:**
- `configs-2host/windows/process_e.json` - Team Pink Leader
- `configs-2host/windows/process_d.json` - Pink Worker (C++)
- `configs-2host/windows/process_f.json` - Pink Worker (Python)
- `configs-2host/windows/start_pink_team.sh` - Startup script
- `configs-2host/windows/stop_pink_team.sh` - Shutdown script

**MacBook:**
- `configs-2host/mac/process_a.json` - Leader (UPDATE WINDOWS_IP!)
- `configs-2host/mac/process_b.json` - Team Green Leader
- `configs-2host/mac/process_c.json` - Green Worker
- `configs-2host/mac/start_green_and_leader.sh` - Startup script
- `configs-2host/mac/stop_green_and_leader.sh` - Shutdown script

---

## Questions / Support

If you encounter issues not covered in this guide:

1. Check process logs in `logs/` directory
2. Verify network connectivity with `ping` and `telnet`
3. Confirm firewall rules are allowing the ports
4. Review the troubleshooting section above

---

**END OF DEPLOYMENT GUIDE**
