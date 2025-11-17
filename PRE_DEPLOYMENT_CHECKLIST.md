# Pre-Deployment Checklist - 2-Host Experiment

## Hardware Setup

### Switch and Cables
- [ ] Netgear 5-port Gigabit switch powered on
- [ ] Cat5e or Cat6 Ethernet cable for Windows laptop
- [ ] Cat5e or Cat6 Ethernet cable for MacBook
- [ ] Ethernet adapter for MacBook (if needed - USB-C to Ethernet)
- [ ] Both systems physically connected to switch
- [ ] Link lights on switch are GREEN (Gigabit) not ORANGE (100Mbps)

---

## Windows/WSL System

### Software Prerequisites
- [ ] WSL Ubuntu is installed and running
- [ ] Project directory: `/mnt/c/Vineet/v1-baseline`
- [ ] g++ compiler installed: `g++ --version`
- [ ] CMake installed: `cmake --version` (>= 3.15)
- [ ] gRPC installed: `pkg-config --modversion grpc++`
- [ ] Protobuf installed: `protoc --version`
- [ ] Python 3 installed: `python3 --version`
- [ ] Python packages: `pip3 list | grep grpcio`

### Build Status
- [ ] Project built successfully: `ls -la build/worker_server build/team_leader_server`
- [ ] Python worker exists: `ls -la src/servers/team_pink/worker_server.py`
- [ ] fire-data directory exists: `ls -la fire-data/ | wc -l` (should show 43+ directories)
- [ ] Configs exist: `ls configs-2host/windows/*.json`
- [ ] Scripts executable: `ls -la configs-2host/windows/*.sh`

### Network Configuration
- [ ] Windows IP address noted: _________________ (e.g., 192.168.1.100)
- [ ] Can ping MacBook: `ping <MAC_IP>`
- [ ] Firewall allows ports 50054-50056
  - Test: `sudo ufw status` (if using ufw)
  - OR Windows Defender rule created

### Test Single Host First
- [ ] Can run single-host on Windows/WSL successfully
- [ ] `./start_all_unix.sh` works
- [ ] `./build/fire_client localhost:50051 --start 20200810 --end 20200815` returns data
- [ ] `./stop_all_unix.sh` works

---

## MacBook System

### Software Prerequisites
- [ ] Xcode Command Line Tools: `xcode-select -p`
- [ ] Homebrew installed (if using): `brew --version`
- [ ] CMake installed: `cmake --version` (>= 3.15)
- [ ] gRPC installed via Homebrew: `brew list | grep grpc`
- [ ] Protobuf installed: `protoc --version`
- [ ] Python 3 installed: `python3 --version`

### Project Files
- [ ] Project copied to MacBook: `~/v1-baseline` or your preferred location
- [ ] fire-data directory copied (all 43 subdirectories)
  - Verify: `ls ~/v1-baseline/fire-data/ | wc -l` should show 43
- [ ] Configs exist: `ls configs-2host/mac/*.json`
- [ ] Scripts executable: `ls -la configs-2host/mac/*.sh`

### Build Status
- [ ] Project built on MacBook: `ls -la build/leader_server build/team_leader_server build/worker_server build/fire_client`
- [ ] Generated proto files exist: `ls build/generated/`

### Network Configuration
- [ ] MacBook IP address noted: _________________ (e.g., 192.168.1.101)
- [ ] MacBook connected via Ethernet (not WiFi for best performance)
- [ ] Can ping Windows: `ping <WINDOWS_IP>`
- [ ] Firewall configured or disabled for testing
  - Check: `sudo /usr/libexec/ApplicationFirewall/socketfilterfw --getglobalstate`

### Configuration Updated
- [ ] `configs-2host/mac/process_a.json` updated with Windows IP
  - Verify: `grep -A3 '"to": "E"' configs-2host/mac/process_a.json`
  - Should show: `"host": "192.168.1.XXX"` (your Windows IP)

---

## Network Verification

### Connectivity Tests
- [ ] MacBook → Windows ping: `ping <WINDOWS_IP>` (0% loss, <5ms latency)
- [ ] Windows → MacBook ping: `ping <MAC_IP>` (0% loss, <5ms latency)
- [ ] Both systems on same subnet (e.g., 192.168.1.x)

### Speed Tests (Optional but Recommended)
- [ ] iperf3 test (if available):
  - Windows: `iperf3 -s`
  - Mac: `iperf3 -c <WINDOWS_IP>`
  - Expected: ~900+ Mbits/sec on Gigabit LAN

### Port Accessibility
On Windows, verify ports will be accessible:
- [ ] `netstat -tuln | grep 50055` after starting Team Pink

On Mac, verify leader port:
- [ ] `netstat -an | grep 50051` after starting Leader

---

## File Transfer Preparation

### If Copying Project to MacBook

**Option 1: Using rsync over network (if SSH enabled)**
- [ ] SSH server enabled on Windows/WSL
- [ ] Can SSH from Mac to Windows: `ssh vineet@<WINDOWS_IP>`
- [ ] Run: `rsync -av --progress vineet@<WINDOWS_IP>:/mnt/c/Vineet/v1-baseline/ ~/v1-baseline/`

**Option 2: Using USB drive**
- [ ] USB drive formatted (exFAT for compatibility)
- [ ] Copy entire v1-baseline folder to USB on Windows
- [ ] Transfer to MacBook

**Option 3: Using network share**
- [ ] Windows network sharing enabled
- [ ] MacBook can access Windows share
- [ ] Copy files

**Important:** Ensure fire-data directory (180MB) is copied completely!

---

## Baseline Performance Tests

### Windows/WSL Single-Host Baseline
- [ ] Run benchmark: `python3 benchmark.py`
- [ ] Note average latency: _______ ms
- [ ] Note throughput: _______ records/sec

### MacBook Single-Host Baseline (if testing)
- [ ] All 6 processes running locally on Mac
- [ ] Run benchmark: `python3 benchmark.py`
- [ ] Note average latency: _______ ms
- [ ] Note throughput: _______ records/sec

These baselines will help you compare 2-host vs single-host performance.

---

## Documentation Review

- [ ] Read `DEPLOYMENT_2HOST_GUIDE.md` (full guide)
- [ ] Read `QUICK_START_2HOST.md` (quick reference)
- [ ] Understand architecture diagram
- [ ] Understand process distribution:
  - Windows: E (Team Pink Leader) + D + F (Pink Workers)
  - Mac: A (Leader) + B (Team Green Leader) + C (Green Worker)

---

## Workspace Preparation

### Windows/WSL Terminal
- [ ] Terminal open in `/mnt/c/Vineet/v1-baseline`
- [ ] Ready to run: `./configs-2host/windows/start_pink_team.sh`

### MacBook Terminal 1
- [ ] Terminal open in `~/v1-baseline`
- [ ] Ready to run: `./configs-2host/mac/start_green_and_leader.sh`

### MacBook Terminal 2
- [ ] Terminal open in `~/v1-baseline`
- [ ] Ready to run: `./build/fire_client localhost:50051 --start 20200810 --end 20200924`

### MacBook Terminal 3 (Optional)
- [ ] Terminal ready for log monitoring: `tail -f logs/process_a.log`

---

## Safety / Rollback

### Backup Original Configs
- [ ] Original configs backed up: `cp -r configs configs.backup`

### Stop Scripts Ready
- [ ] Know how to stop Windows: `./configs-2host/windows/stop_pink_team.sh`
- [ ] Know how to stop Mac: `./configs-2host/mac/stop_green_and_leader.sh`
- [ ] Know manual kill: `pkill -f 'leader_server|team_leader_server|worker_server'`

---

## Expected Issues & Solutions

### Known Issue 1: Shared Memory Warnings
**Expected:** Team Pink processes on Windows may log:
```
Error: Failed to attach to shared memory segment
```
**Solution:** This is NORMAL. Shared memory is per-host, not cross-network.

### Known Issue 2: Clock Skew Warnings
**Expected:** CMake/make may show:
```
warning: Clock skew detected
```
**Solution:** Ignore - this is WSL/Windows timestamp sync issue, harmless.

### Known Issue 3: First Query Slower
**Expected:** First query may take 2-3x longer than subsequent queries.
**Solution:** Normal - data loading and connection establishment overhead.

---

## Final Checklist Before Starting

- [ ] All software installed on both systems
- [ ] Both systems connected via Gigabit switch
- [ ] IP addresses noted and configs updated
- [ ] Firewall rules configured
- [ ] fire-data exists on both systems
- [ ] Builds successful on both systems
- [ ] Connectivity verified (ping tests pass)
- [ ] Documentation read
- [ ] Terminals ready

---

## Go / No-Go Decision

**GO if all critical items checked:**
- ✅ Network connectivity working
- ✅ Builds successful on both hosts
- ✅ fire-data exists on both hosts
- ✅ IP address updated in process_a.json
- ✅ Firewall allows ports

**NO-GO if any critical item fails:**
- ❌ Can't ping between systems
- ❌ Build failures
- ❌ fire-data missing or incomplete
- ❌ Firewall blocking ports

---

## Post-Deployment Success Criteria

A successful deployment will show:
- [ ] All 6 processes running (3 on each host)
- [ ] Leader log shows: "Connected to team leader E (pink) at <WINDOWS_IP>:50055"
- [ ] Query returns data from both hosts
- [ ] Records from processes B, C (Mac) and D, E, F (Windows)
- [ ] Total records ~300,000+ for full date range
- [ ] Query latency 150-500ms (acceptable for network overhead)

---

## Experiment Goals

What to measure/observe:
- [ ] End-to-end query latency (2-host vs single-host)
- [ ] Network bandwidth utilization during query
- [ ] TTFC (Time to First Chunk) for each team
- [ ] Team load distribution (Green vs Pink)
- [ ] Impact of network on chunk streaming
- [ ] Metrics from both hosts

---

**Ready to deploy? Start with QUICK_START_2HOST.md!**
