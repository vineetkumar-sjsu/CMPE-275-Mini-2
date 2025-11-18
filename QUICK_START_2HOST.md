# Quick Start - 2-Host Deployment

## Setup Summary

**Windows (WSL):** Team Pink (E, D, F)
**MacBook:** Leader + Team Green (A, B, C)
**Client:** MacBook fires queries

---

## Before You Start

1. Connect both systems to Netgear switch
2. Note IP addresses:
   - Windows: `hostname -I` in WSL
   - Mac: `ifconfig en0 | grep inet`
3. Update MacBook config with Windows IP:
   - Edit: `configs-2host/mac/process_a.json`
   - Replace `WINDOWS_IP_HERE` with actual Windows IP

---

## Commands

### 1. Start Windows Team Pink (Terminal 1)

```bash
cd /mnt/c/Vineet/v1-baseline
./configs-2host/windows/start_pink_team.sh
```

Wait for: "Team Pink processes started!"

### 2. Start MacBook Leader + Green (Terminal 2)

```bash
cd ~/v1-baseline
./configs-2host/mac/start_green_and_leader.sh
```

Wait for: "All processes started!"

### 3. Test Query (Terminal 3 - MacBook)

```bash
cd ~/v1-baseline
./build/fire_client localhost:50051 --start 20200810 --end 20200924
```

Expected: Records from both hosts (B, C, D, E, F)

### 4. Run Benchmark (MacBook)

```bash
python3 benchmark.py
python3 analyze_metrics.py
```

---

## Verify Network

**Test connectivity:**
```bash
# From Mac to Windows
ping <WINDOWS_IP>

# From Windows to Mac
ping <MAC_IP>
```

**Check ports listening:**
```bash
# Windows - should show 50054, 50055, 50056
netstat -tuln | grep -E '5005[456]'

# Mac - should show 50051, 50052, 50053
netstat -an | grep -E '5005[123]'
```

---

## Stop Everything

**MacBook:**
```bash
./configs-2host/mac/stop_green_and_leader.sh
```

**Windows:**
```bash
./configs-2host/windows/stop_pink_team.sh
```

---

## Troubleshooting

**Leader can't connect to Pink team:**
1. Check Team Pink is running on Windows
2. Verify firewall allows port 50055
3. Confirm IP in `process_a.json` is correct
4. Test: `telnet <WINDOWS_IP> 50055` from Mac

**No records from Team Pink:**
1. Check Windows logs: `tail logs/process_e.log`
2. Verify fire-data exists on Windows
3. Query Pink dates only: `--start 20200914 --end 20200920`

**Slow queries:**
1. Verify Gigabit link (not 100Mbps)
2. Check cables are Cat5e or Cat6
3. Monitor with `nload` or `iftop`

---

## File Locations

**Configs:**
- Windows: `configs-2host/windows/*.json`
- Mac: `configs-2host/mac/*.json`

**Scripts:**
- Windows: `configs-2host/windows/*.sh`
- Mac: `configs-2host/mac/*.sh`

**Logs:**
- Both: `logs/process_*.log`
- Metrics: `logs/metrics-*.csv`

**Guide:**
- Full documentation: `DEPLOYMENT_2HOST_GUIDE.md`

---

## Expected Performance

**Single-host baseline:**
- Latency: 100-200ms
- Throughput: 80K-100K records/sec

**2-host Gigabit:**
- Latency: 150-300ms (network overhead)
- Throughput: 60K-80K records/sec
- Network per delegation: +5-20ms

---

## Process Ports

| Process | Port  | Host    | Role              |
|---------|-------|---------|-------------------|
| A       | 50051 | Mac     | Leader            |
| B       | 50052 | Mac     | Team Green Leader |
| C       | 50053 | Mac     | Green Worker      |
| E       | 50055 | Windows | Team Pink Leader  |
| D       | 50054 | Windows | Pink Worker       |
| F       | 50056 | Windows | Pink Worker (Py)  |

---

**Client connects to:** localhost:50051 (Process A on Mac)
