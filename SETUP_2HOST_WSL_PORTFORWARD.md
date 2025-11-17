# Complete 2-Host Deployment Setup with WSL Port Forwarding

## Architecture: A,B,C on Mac | D,E,F on Windows/WSL

This guide provides complete instructions for deploying the Fire Query System across MacBook and Windows/WSL with proper port forwarding.

---

## Table of Contents
1. [WSL Port Forwarding Setup (Windows)](#wsl-port-forwarding-setup)
2. [Network Configuration](#network-configuration)
3. [Build and Deploy](#build-and-deploy)
4. [Testing and Verification](#testing-and-verification)
5. [Troubleshooting](#troubleshooting)
6. [Cleanup](#cleanup)

---

## WSL Port Forwarding Setup

### Why Port Forwarding is Needed

WSL2 uses a virtual network adapter with its own IP address (e.g., 172.x.x.x). External systems (like your MacBook) cannot directly reach WSL processes. We need to forward ports from your Windows host IP to WSL.

**Ports Required:**
- **50054** - Process D (Pink Worker)
- **50055** - Process E (Pink Team Leader) - **MOST CRITICAL**
- **50056** - Process F (Python Worker)

---

### Step 1: Get IP Addresses

#### Get Windows Host IP:
```powershell
# In PowerShell
ipconfig | findstr "IPv4"
```
**Example output:** `192.168.1.65` ← **Your WINDOWS_HOST_IP**

#### Get WSL IP:
```bash
# In WSL terminal
hostname -I | awk '{print $1}'
# OR
ip addr show eth0 | grep "inet " | awk '{print $2}' | cut -d/ -f1
```
**Example output:** `172.28.16.89` ← **Your WSL_IP**

**Note these down!**

---

### Step 2: Configure Port Forwarding (PowerShell as Administrator)

**Open PowerShell as Administrator** (Right-click → Run as Administrator)

```powershell
# Set variables (REPLACE WITH YOUR ACTUAL IPs!)
$WSL_IP = "172.28.16.89"          # ← REPLACE with your WSL IP
$WINDOWS_IP = "192.168.1.65"      # ← Your Windows host IP (for reference)

# Port forward: 50054 (Process D - Worker)
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50054 connectaddress=$WSL_IP connectport=50054

# Port forward: 50055 (Process E - Team Leader) - CRITICAL!
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50055 connectaddress=$WSL_IP connectport=50055

# Port forward: 50056 (Process F - Python Worker)
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50056 connectaddress=$WSL_IP connectport=50056
```

**Verify port forwarding rules:**
```powershell
netsh interface portproxy show all
```

**Expected output:**
```
Listen on ipv4:             Connect to ipv4:

Address         Port        Address         Port
--------------- ----------  --------------- ----------
0.0.0.0         50054       172.28.16.89    50054
0.0.0.0         50055       172.28.16.89    50055
0.0.0.0         50056       172.28.16.89    50056
```

---

### Step 3: Configure Windows Firewall

**Still in PowerShell as Administrator:**

```powershell
# Add firewall rules for INBOUND traffic (from Mac to Windows)
netsh advfirewall firewall add rule name="Fire Query Port 50054 TCP" dir=in action=allow protocol=TCP localport=50054
netsh advfirewall firewall add rule name="Fire Query Port 50055 TCP" dir=in action=allow protocol=TCP localport=50055
netsh advfirewall firewall add rule name="Fire Query Port 50056 TCP" dir=in action=allow protocol=TCP localport=50056

# Add firewall rules for UDP (optional, but recommended)
netsh advfirewall firewall add rule name="Fire Query Port 50054 UDP" dir=in action=allow protocol=UDP localport=50054
netsh advfirewall firewall add rule name="Fire Query Port 50055 UDP" dir=in action=allow protocol=UDP localport=50055
netsh advfirewall firewall add rule name="Fire Query Port 50056 UDP" dir=in action=allow protocol=UDP localport=50056

# Add OUTBOUND rules (for WSL to external network)
netsh advfirewall firewall add rule name="WSL Fire Query Outbound 50054" dir=out action=allow protocol=TCP remoteport=50054
netsh advfirewall firewall add rule name="WSL Fire Query Outbound 50055" dir=out action=allow protocol=TCP remoteport=50055
netsh advfirewall firewall add rule name="WSL Fire Query Outbound 50056" dir=out action=allow protocol=TCP remoteport=50056
```

**Verify firewall rules:**
```powershell
netsh advfirewall firewall show rule name=all | findstr "Fire Query"
```

---

### Step 4: Test Port Forwarding (Before Building)

**From Windows PowerShell:**
```powershell
# Test that WSL IP is reachable from Windows
Test-NetConnection -ComputerName 172.28.16.89 -Port 50055
```

**From MacBook Terminal:**
```bash
# First, test basic connectivity to Windows
ping 192.168.1.65

# Test port forwarding (this will fail until processes are running, but should show connection attempt)
nc -zv 192.168.1.65 50055
# OR
telnet 192.168.1.65 50055
```

Expected: Connection attempt (not refused)

---

## Network Configuration

### Step 5: Update Configuration Files

#### On Windows/WSL:

**Verify WSL configs are correct** (they should already use `localhost` for intra-team communication):

```bash
cd /mnt/c/Vineet/v1-baseline

# Check Process E config
cat configs-2host/windows/process_e.json
```

**Should show:**
```json
{
  "process_id": "E",
  "role": "team_leader",
  "listen_host": "0.0.0.0",    ← Listening on all interfaces (CORRECT)
  "listen_port": 50055,
  ...
  "edges": [
    {
      "to": "F",
      "host": "localhost",      ← Workers on same WSL (CORRECT)
      "port": 50056,
      ...
    },
    {
      "to": "D",
      "host": "localhost",      ← Workers on same WSL (CORRECT)
      "port": 50054,
      ...
    }
  ]
}
```

✅ **No changes needed on Windows configs!**

---

#### On MacBook:

**Edit Process A config to use Windows HOST IP (NOT WSL IP):**

```bash
cd ~/v1-baseline

# Edit leader config
nano configs-2host/mac/process_a.json
```

**Update line 19 with your WINDOWS HOST IP:**
```json
{
  "process_id": "A",
  "role": "leader",
  ...
  "edges": [
    {
      "to": "B",
      "host": "localhost",
      "port": 50052,
      ...
    },
    {
      "to": "E",
      "host": "192.168.1.65",    ← YOUR WINDOWS HOST IP (NOT WSL IP!)
      "port": 50055,
      "relationship": "team_leader",
      "team": "pink"
    }
  ]
}
```

**Save and exit** (Ctrl+O, Enter, Ctrl+X)

---

## Build and Deploy

### Step 6: Build on Windows/WSL

```bash
cd /mnt/c/Vineet/v1-baseline

# Stop any running processes
pkill -f 'worker_server|team_leader_server|worker_server.py' 2>/dev/null || true

# Generate protobuf code (if not already done)
chmod +x generate_proto_unix.sh
./generate_proto_unix.sh

# Build C++ components
mkdir -p build
cd build
cmake ..
make -j4
cd ..

# Verify binaries
ls -lh build/worker_server build/team_leader_server src/servers/team_pink/worker_server.py
```

---

### Step 7: Build on MacBook

```bash
cd ~/v1-baseline

# Generate protobuf code (if not already done)
chmod +x generate_proto.sh
./generate_proto.sh

# Build C++ components
mkdir -p build
cd build
cmake ..
make -j4
cd ..

# Verify binaries
ls -lh build/leader_server build/team_leader_server build/worker_server build/fire_client
```

---

### Step 8: Start Processes

#### **CRITICAL ORDER: Start Windows FIRST!**

**Terminal 1 - Windows/WSL:**
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
  PID: XXXXX (check logs/process_f.log)
Starting Process D (Worker - Team Pink)...
  PID: XXXXX (check logs/process_d.log)
Starting Process E (Team Leader - Pink)...
  PID: XXXXX (check logs/process_e.log)

========================================
Team Pink processes started!
========================================

Ports listening:
  50055: Process E (Team Leader)
  50054: Process D (Worker)
  50056: Process F (Python Worker)
```

**Verify processes are listening:**
```bash
# Check logs
tail -20 logs/process_e.log

# Should show:
# Team Leader Process E (Team pink) starting...
# Listening on 0.0.0.0:50055
# Connected to worker F at localhost:50056
# Connected to worker D at localhost:50054
# *** Team Leader server listening on 0.0.0.0:50055 ***

# Verify ports are listening
netstat -tuln | grep -E '5005[456]'
# Should show:
# tcp        0      0 0.0.0.0:50054           0.0.0.0:*               LISTEN
# tcp        0      0 0.0.0.0:50055           0.0.0.0:*               LISTEN
# tcp        0      0 0.0.0.0:50056           0.0.0.0:*               LISTEN
```

---

**Terminal 2 - MacBook (Wait 10 seconds after Windows):**
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
  PID: XXXXX (check logs/process_c.log)
Starting Process B (Team Leader - Green)...
  PID: XXXXX (check logs/process_b.log)
Starting Process A (Leader)...
  PID: XXXXX (check logs/process_a.log)

========================================
All processes started!
========================================
```

**CRITICAL: Verify Leader connected to Team Pink across network:**
```bash
tail -30 logs/process_a.log
```

**Should show:**
```
Leader Process A starting...
Listening on 0.0.0.0:50051
Connected to team leader B (green) at localhost:50052
Connected to team leader E (pink) at 192.168.1.65:50055  ← NETWORK CONNECTION!
*** Leader server listening on 0.0.0.0:50051 ***
```

✅ **SUCCESS:** If you see "Connected to team leader E", port forwarding is working!

❌ **FAILURE:** If you see "Failed to connect to team leader E", see Troubleshooting section below.

---

## Testing and Verification

### Test 1: Local Team Query (Team Green - Mac Only)

**On MacBook:**
```bash
cd ~/v1-baseline

# Query only Team Green dates (no network traffic to Windows)
./build/fire_client localhost:50051 --start 20200810 --end 20200901 --chunk 500
```

**Expected:** Records only from B and C (Team Green on Mac)

---

### Test 2: Remote Team Query (Team Pink - Windows Only)

```bash
# Query only Team Pink dates (network traffic to Windows)
./build/fire_client localhost:50051 --start 20200902 --end 20200924 --chunk 500
```

**Expected:** Records from D, E, F (Team Pink on Windows)

**This test verifies:**
- ✅ Port forwarding is working
- ✅ Mac can reach Windows processes
- ✅ Data flows over network correctly

---

### Test 3: Full Cross-Host Query

```bash
# Query entire dataset (both teams)
./build/fire_client localhost:50051 --start 20200810 --end 20200924 --chunk 500
```

**Expected output:**
```
========================================
QUERY COMPLETE
========================================
Status:        SUCCESS
Total Records: ~300,000+
Duration:      1000-3000 ms

Records by Process:
  A: 0 records
  B: ~60,000 records (Team Green - Mac)
  C: ~60,000 records (Team Green - Mac)
  D: ~40,000 records (Team Pink - Windows)
  E: ~40,000 records (Team Pink - Windows)
  F: ~70,000 records (Team Pink - Windows - Python)
========================================
```

✅ **All processes contributing data = SUCCESS!**

---

### Test 4: Monitor Network Traffic (Optional)

**On Windows PowerShell (while query is running):**
```powershell
# Monitor connections to port 50055
netstat -an | findstr "50055"
```

**Should show:**
```
TCP    0.0.0.0:50055          0.0.0.0:0              LISTENING
TCP    192.168.1.65:50055     192.168.1.101:XXXXX    ESTABLISHED  ← Mac connected!
```

---

## Troubleshooting

### Issue 1: "Failed to connect to team leader E"

**Check 1: Port forwarding is configured**
```powershell
# In PowerShell
netsh interface portproxy show all
```
Should show port 50055 forwarding to WSL IP.

**Check 2: Firewall allows the port**
```powershell
netsh advfirewall firewall show rule name=all | findstr "50055"
```

**Check 3: WSL processes are listening**
```bash
# In WSL
netstat -tuln | grep 50055
```
Should show `0.0.0.0:50055` in LISTEN state.

**Check 4: Test from Windows to WSL**
```powershell
# In PowerShell
Test-NetConnection -ComputerName 172.28.16.89 -Port 50055
```
Should show `TcpTestSucceeded : True`

**Check 5: Test from Mac to Windows**
```bash
# On Mac
telnet 192.168.1.65 50055
# OR
nc -zv 192.168.1.65 50055
```
Should connect successfully.

**Fix: If WSL IP changed (WSL restarts change the IP)**
```bash
# Get new WSL IP
hostname -I | awk '{print $1}'

# Update port forwarding in PowerShell (as Admin)
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50055
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50055 connectaddress=<NEW_WSL_IP> connectport=50055

# Repeat for ports 50054 and 50056
```

---

### Issue 2: WSL IP Changes After Reboot

**Problem:** WSL2 assigns a new IP address after Windows reboot.

**Solution: Create a PowerShell script to auto-update port forwarding**

Create `update_wsl_portforward.ps1`:
```powershell
# Get WSL IP dynamically
$WSL_IP = (wsl hostname -I).Trim()

Write-Host "WSL IP: $WSL_IP"

# Remove old port forwarding rules
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50054
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50055
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50056

# Add new port forwarding rules
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50054 connectaddress=$WSL_IP connectport=50054
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50055 connectaddress=$WSL_IP connectport=50055
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50056 connectaddress=$WSL_IP connectport=50056

Write-Host "Port forwarding updated!"
netsh interface portproxy show all
```

**Run this script (as Administrator) after each Windows reboot:**
```powershell
.\update_wsl_portforward.ps1
```

---

### Issue 3: No data from Team Pink

**Check Windows logs:**
```bash
# In WSL
tail -50 logs/process_e.log
tail -50 logs/process_d.log
tail -50 logs/process_f.log
```

**Check if fire-data exists:**
```bash
ls -la fire-data/ | head -20
ls -la fire-data/20200914/  # Should show CSV files
```

**Test Pink-only query:**
```bash
# On Mac
./build/fire_client localhost:50051 --start 20200914 --end 20200920
```

---

### Issue 4: Slow Performance / High Latency

**Expected performance:**
- Single-host: 100-200ms
- 2-host with Gigabit LAN: 150-300ms
- 2-host with WiFi: 200-500ms

**Check network speed:**
```bash
# On Mac, ping Windows
ping -c 10 192.168.1.65
```
Should show <1ms latency on wired LAN, <10ms on WiFi.

**Check if Gigabit:**
```bash
# On Mac
networksetup -getmedia en0
# Should show "1000baseT" not "100baseTX"
```

---

## Cleanup

### Stop All Processes

**On MacBook:**
```bash
cd ~/v1-baseline
./configs-2host/mac/stop_green_and_leader.sh
```

**On Windows/WSL:**
```bash
cd /mnt/c/Vineet/v1-baseline
./configs-2host/windows/stop_pink_team.sh
```

---

### Remove Port Forwarding (Optional)

**In PowerShell as Administrator:**
```powershell
# Remove all port forwarding rules
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50054
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50055
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50056

# Verify
netsh interface portproxy show all
```

---

### Remove Firewall Rules (Optional)

**In PowerShell as Administrator:**
```powershell
# Remove firewall rules
netsh advfirewall firewall delete rule name="Fire Query Port 50054 TCP"
netsh advfirewall firewall delete rule name="Fire Query Port 50055 TCP"
netsh advfirewall firewall delete rule name="Fire Query Port 50056 TCP"
netsh advfirewall firewall delete rule name="Fire Query Port 50054 UDP"
netsh advfirewall firewall delete rule name="Fire Query Port 50055 UDP"
netsh advfirewall firewall delete rule name="Fire Query Port 50056 UDP"
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50054"
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50055"
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50056"
```

---

## Quick Reference Commands

### Get IPs
```bash
# WSL IP
hostname -I | awk '{print $1}'

# Windows IP
ipconfig | findstr "IPv4"
```

### Port Forwarding Status
```powershell
# Show all port forwarding rules
netsh interface portproxy show all

# Show firewall rules
netsh advfirewall firewall show rule name=all | findstr "Fire Query"
```

### Check Processes
```bash
# Windows/WSL - Check if processes running
ps aux | grep -E 'worker_server|team_leader_server|worker_server.py'

# Check ports listening
netstat -tuln | grep -E '5005[456]'
```

### Test Connectivity
```bash
# From Mac, test each port
nc -zv 192.168.1.65 50054
nc -zv 192.168.1.65 50055
nc -zv 192.168.1.65 50056
```

---

## Summary

**What we accomplished:**
1. ✅ Configured WSL port forwarding (WSL IP → Windows Host IP)
2. ✅ Configured Windows Firewall to allow external connections
3. ✅ Updated Mac config to use Windows HOST IP (not WSL IP)
4. ✅ Enabled Mac to connect to WSL processes via Windows IP
5. ✅ Tested cross-host distributed query system

**Network Flow:**
```
Mac (192.168.1.101)
  ↓
  Query to Leader A (localhost:50051 on Mac)
  ↓
  Leader A delegates to Team Pink E at 192.168.1.65:50055
  ↓
Windows Host IP (192.168.1.65:50055)
  ↓
  Port Forwarding
  ↓
WSL IP (172.28.16.89:50055)
  ↓
Process E (Team Pink Leader in WSL)
```

**This setup enables:**
- External systems (Mac) to connect to WSL processes
- Distributed query processing across 2 physical hosts
- Network performance testing and analysis
- Real-world distributed systems experimentation

---

**END OF SETUP GUIDE**
