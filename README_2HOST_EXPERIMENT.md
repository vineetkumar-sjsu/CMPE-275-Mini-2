# 2-Host Distributed Deployment - Quick Reference

## What Was Created

All files needed for a 2-host deployment across your MacBook and Windows/WSL system connected via Gigabit switch.

---

## File Structure

```
v1-baseline/
├── configs-2host/
│   ├── windows/                    # Windows/WSL configuration
│   │   ├── process_e.json          # Team Pink Leader config
│   │   ├── process_d.json          # Pink Worker config
│   │   ├── process_f.json          # Pink Worker (Python) config
│   │   ├── start_pink_team.sh      # Start script for Windows
│   │   └── stop_pink_team.sh       # Stop script for Windows
│   │
│   └── mac/                        # MacBook configuration
│       ├── process_a.json          # Leader config (UPDATE WINDOWS IP!)
│       ├── process_b.json          # Team Green Leader config
│       ├── process_c.json          # Green Worker config
│       ├── start_green_and_leader.sh   # Start script for Mac
│       └── stop_green_and_leader.sh    # Stop script for Mac
│
├── DEPLOYMENT_2HOST_GUIDE.md       # FULL detailed guide (50+ pages)
├── QUICK_START_2HOST.md            # Quick reference (2 pages)
├── PRE_DEPLOYMENT_CHECKLIST.md     # Pre-flight checklist
└── README_2HOST_EXPERIMENT.md      # This file
```

---

## Architecture at a Glance

```
MacBook                          Windows/WSL
┌─────────────────┐              ┌─────────────────┐
│ Process A       │              │ Process E       │
│ (Leader)        │◄────NET──────┤ (Team Pink Ldr) │
│ Port 50051      │              │ Port 50055      │
└────┬────────────┘              └────┬────────────┘
     │                                │
     ├──local──┬──────────┐           ├──local──┬──────┐
     │         │          │           │         │      │
┌────▼───┐ ┌──▼────┐ ┌───▼─────┐ ┌───▼───┐ ┌──▼────┐ │
│Process │ │Process│ │ Client  │ │Process│ │Process│ │
│B (Green│ │C      │ │         │ │D      │ │F (Py) │ │
│Leader) │ │(Worker│ │         │ │(Worker│ │(Worker│ │
│:50052  │ │:50053)│ │         │ │:50054)│ │:50056)│ │
└────────┘ └───────┘ └─────────┘ └───────┘ └───────┘ │
                                                       │
Client fires queries from MacBook to localhost:50051  │
Leader delegates to local Team Green + remote Team Pink
```

---

## Setup Steps (Summary)

### 1. Hardware
- Connect both systems to Netgear Gigabit switch
- Verify GREEN link lights (Gigabit)

### 2. Get IP Addresses
- Windows: `wsl hostname -I`
- Mac: `ifconfig en0 | grep inet`

### 3. Update Configuration
**CRITICAL:** Edit `configs-2host/mac/process_a.json`
- Replace `WINDOWS_IP_HERE` with actual Windows IP
- Example: `"host": "192.168.1.100"`

### 4. Start Services

**Windows Terminal:**
```bash
cd /mnt/c/Vineet/v1-baseline
./configs-2host/windows/start_pink_team.sh
```

**MacBook Terminal:**
```bash
cd ~/v1-baseline
./configs-2host/mac/start_green_and_leader.sh
```

### 5. Test Query

**MacBook:**
```bash
./build/fire_client localhost:50051 --start 20200810 --end 20200924
```

### 6. Run Benchmark

**MacBook:**
```bash
python3 benchmark.py
python3 analyze_metrics.py
```

---

## Documentation Guide

**Start here (first time):**
1. `PRE_DEPLOYMENT_CHECKLIST.md` - Verify everything is ready
2. `DEPLOYMENT_2HOST_GUIDE.md` - Full detailed instructions
3. `QUICK_START_2HOST.md` - Keep this open during deployment

**When troubleshooting:**
- See "Troubleshooting" section in `DEPLOYMENT_2HOST_GUIDE.md`
- Check process logs in `logs/` directory

---

## Key Points to Remember

### Before Starting
1. **Update IP address** in `configs-2host/mac/process_a.json`
2. **Copy fire-data** to MacBook (all 43 directories, ~180MB)
3. **Build on both systems** before starting
4. **Start Windows first**, then MacBook

### During Operation
1. **Client connects to MacBook** (localhost:50051)
2. **MacBook delegates to Windows** (network RPC to Team Pink)
3. **Results stream back** through Leader on MacBook
4. **Metrics collected** on both hosts

### After Experiment
1. **Stop MacBook processes** first
2. **Stop Windows processes** second
3. **Copy metrics** from Windows to MacBook for unified analysis

---

## Port Reference

| Process | Port  | Host    | Description        |
|---------|-------|---------|--------------------|
| A       | 50051 | Mac     | Leader (CLIENT HERE) |
| B       | 50052 | Mac     | Team Green Leader  |
| C       | 50053 | Mac     | Green Worker       |
| E       | 50055 | Windows | Team Pink Leader   |
| D       | 50054 | Windows | Pink Worker        |
| F       | 50056 | Windows | Pink Worker (Py)   |

---

## Data Distribution

| Process | Team  | Dates                | Count | Host    |
|---------|-------|----------------------|-------|---------|
| B       | Green | Aug 10-22            | 10    | Mac     |
| C       | Green | Aug 23 - Sep 1       | 10    | Mac     |
| D       | Pink  | Sep 2-7              | 6     | Windows |
| E       | Pink  | Sep 8-13             | 6     | Windows |
| F       | Pink  | Sep 14-24            | 11    | Windows |
| **Total** |     | **Aug 10 - Sep 24** | **43** | Both |

---

## Expected Performance

### Single-Host Baseline
- Latency: 100-200ms
- Throughput: 80,000-100,000 records/sec

### 2-Host Gigabit LAN
- Latency: 150-300ms (+network overhead)
- Throughput: 60,000-80,000 records/sec
- Network adds ~5-20ms per delegation hop

### Full Dataset Query
- Total records: ~300,000
- Query time: 1-3 seconds
- Data transfer: ~50-100 MB
- Chunks: ~600 (500 records each)

---

## Common Issues & Quick Fixes

### "Failed to connect to team leader E"
```bash
# 1. Check Team Pink is running on Windows
wsl ps aux | grep team_leader_server

# 2. Verify IP in config
grep -A2 '"to": "E"' configs-2host/mac/process_a.json

# 3. Test network
ping <WINDOWS_IP>
telnet <WINDOWS_IP> 50055
```

### No records from Team Pink
```bash
# Query only Pink dates to isolate issue
./build/fire_client localhost:50051 --start 20200914 --end 20200920

# Check Windows logs
wsl tail logs/process_e.log
```

### Slow performance
```bash
# Verify Gigabit speed (not 100Mbps)
# Mac: networksetup -getmedia en0
# Windows: wsl ethtool eth0 | grep Speed
```

---

## Experiment Ideas

1. **Baseline Comparison**
   - Run single-host benchmark first
   - Run 2-host benchmark
   - Compare latency and throughput

2. **Network Impact**
   - Query only Team Green (local): Aug 10 - Sep 1
   - Query only Team Pink (remote): Sep 2-24
   - Compare TTFC and latency

3. **Load Distribution**
   - Analyze metrics to see team balance
   - Check if Team Pink takes longer due to network

4. **Failure Scenarios**
   - Kill Team Pink mid-query
   - Observe graceful degradation
   - Only Team Green data returns

5. **Network Monitoring**
   - Use `nload`, `iftop`, or Wireshark
   - Observe gRPC traffic patterns
   - Measure bandwidth during query

---

## Success Criteria

Your deployment is successful when:
- ✅ All 6 processes running across both hosts
- ✅ Leader log shows "Connected to team leader E (pink)"
- ✅ Query returns records from both B/C (Mac) and D/E/F (Windows)
- ✅ Query completes in <5 seconds for full dataset
- ✅ No connection errors or timeouts
- ✅ Metrics generated on both hosts

---

## Next Steps After Successful Deployment

1. **Collect Baseline Metrics**
   - Document single-host performance
   - Document 2-host performance
   - Calculate network overhead

2. **Experiment with Queries**
   - Different date ranges
   - Different pollutant filters
   - Geographic filters (lat/lon bounds)
   - Record limits

3. **Analyze Metrics**
   - TTFC comparison (Green vs Pink)
   - Chunk gap analysis
   - End-to-end latency distribution
   - Team load balancing

4. **Advanced Experiments**
   - Add artificial network latency
   - Simulate network packet loss
   - Test with 100Mbps link (throttle)
   - Multiple concurrent clients

---

## Support & Troubleshooting

**If stuck:**
1. Check `DEPLOYMENT_2HOST_GUIDE.md` Troubleshooting section
2. Review process logs: `logs/process_*.log`
3. Verify network: `ping` and `telnet` tests
4. Check firewall: Windows Defender and macOS firewall
5. Confirm build: `ls -la build/` on both systems

**Debug commands:**
```bash
# Check running processes
ps aux | grep -E 'leader_server|team_leader|worker_server'

# Check listening ports
netstat -tuln | grep -E '5005[1-6]'

# View logs in real-time
tail -f logs/process_a.log

# Check metrics generated
ls -lh logs/metrics*.csv
```

---

## File Checklist

Before starting, verify these files exist:

**On Windows/WSL:**
- [ ] `configs-2host/windows/process_e.json`
- [ ] `configs-2host/windows/process_d.json`
- [ ] `configs-2host/windows/process_f.json`
- [ ] `configs-2host/windows/start_pink_team.sh` (executable)
- [ ] `configs-2host/windows/stop_pink_team.sh` (executable)
- [ ] `build/worker_server` (compiled)
- [ ] `build/team_leader_server` (compiled)
- [ ] `src/servers/team_pink/worker_server.py`
- [ ] `fire-data/` (43 subdirectories)

**On MacBook:**
- [ ] `configs-2host/mac/process_a.json` (IP UPDATED!)
- [ ] `configs-2host/mac/process_b.json`
- [ ] `configs-2host/mac/process_c.json`
- [ ] `configs-2host/mac/start_green_and_leader.sh` (executable)
- [ ] `configs-2host/mac/stop_green_and_leader.sh` (executable)
- [ ] `build/leader_server` (compiled)
- [ ] `build/team_leader_server` (compiled)
- [ ] `build/worker_server` (compiled)
- [ ] `build/fire_client` (compiled)
- [ ] `fire-data/` (43 subdirectories)

---

## Timeline Estimate

**Setup (first time):** 30-60 minutes
- Hardware setup: 5 min
- Copy files to Mac: 10 min
- Build on Mac: 5 min
- Configure IPs: 5 min
- Firewall setup: 5 min
- Test connectivity: 10 min

**Deployment:** 5 minutes
- Start Windows: 1 min
- Start Mac: 1 min
- Test query: 1 min
- Run benchmark: 2 min

**Experiment & Analysis:** 15-30 minutes
- Multiple queries: 10 min
- Metrics collection: 5 min
- Analysis: 10 min

**Total:** ~1-2 hours for complete first-time setup and experiment

---

## Ready to Start?

1. Read `PRE_DEPLOYMENT_CHECKLIST.md` and check all items
2. Follow `QUICK_START_2HOST.md` for step-by-step commands
3. Refer to `DEPLOYMENT_2HOST_GUIDE.md` for detailed explanations

**Good luck with your distributed systems experiment!**
