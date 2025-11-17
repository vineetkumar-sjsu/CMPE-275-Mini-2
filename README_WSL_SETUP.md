# WSL Port Forwarding Setup - Complete Documentation

## Files Created

This setup includes the following documentation and automation scripts:

### 📄 Documentation
1. **[SETUP_2HOST_WSL_PORTFORWARD.md](SETUP_2HOST_WSL_PORTFORWARD.md)** - Complete step-by-step guide with troubleshooting
2. **[QUICK_START_WSL.md](QUICK_START_WSL.md)** - 5-minute quick start guide
3. **[README_WSL_SETUP.md](README_WSL_SETUP.md)** - This file (overview)

### 🔧 Automation Scripts
1. **[setup_wsl_portforward.ps1](setup_wsl_portforward.ps1)** - One-click setup (run as Admin)
2. **[cleanup_wsl_portforward.ps1](cleanup_wsl_portforward.ps1)** - Clean up port forwarding rules

---

## Why WSL Port Forwarding?

WSL2 runs on a virtualized network with its own IP address (e.g., `172.28.16.89`). External systems like your MacBook cannot directly reach WSL processes.

**Port forwarding** maps your Windows host IP (e.g., `192.168.1.65`) to WSL IP, making WSL processes accessible from the network.

---

## Recommended Deployment: A,B,C on Mac | D,E,F on Windows

### Architecture Benefits
✅ **Natural team separation** - Each host has one complete team
✅ **Minimal network traffic** - Only 1 cross-host RPC (A→E)
✅ **Balanced data** - 20 dates on Mac, 23 on Windows (46%/54%)
✅ **Leader co-located with client** - Lower client latency
✅ **Failure isolation** - Team-based failure domains

### Process Distribution

**MacBook (Team Green + Leader):**
- Process A (Leader) - Port 50051
- Process B (Team Green Leader) - Port 50052
- Process C (Green Worker) - Port 50053
- Data: Aug 10 - Sep 1 (20 dates)

**Windows/WSL (Team Pink):**
- Process E (Team Pink Leader) - Port 50055 **← CRITICAL**
- Process D (Pink Worker) - Port 50054
- Process F (Python Worker) - Port 50056
- Data: Sep 2 - Sep 24 (23 dates)

---

## Setup Process Overview

### Phase 1: Port Forwarding (Windows)
Run `setup_wsl_portforward.ps1` as Administrator to:
- Auto-detect WSL IP address
- Configure port forwarding (50054, 50055, 50056)
- Configure Windows Firewall rules
- Display Windows host IP for Mac configuration

### Phase 2: Configuration (Mac)
Update `configs-2host/mac/process_a.json` with Windows host IP

### Phase 3: Build (Both Systems)
Build the project on Windows/WSL and MacBook

### Phase 4: Deploy
Start Windows processes first, then Mac processes

### Phase 5: Test
Run queries to verify cross-host communication

---

## Quick Start Commands

### 1️⃣ Windows PowerShell (as Administrator)
```powershell
cd C:\Vineet\v1-baseline
.\setup_wsl_portforward.ps1
# Note the Windows IP displayed
```

### 2️⃣ MacBook Terminal
```bash
cd ~/v1-baseline
nano configs-2host/mac/process_a.json
# Update line 19 with Windows IP
```

### 3️⃣ Windows WSL Terminal
```bash
cd /mnt/c/Vineet/v1-baseline
./configs-2host/windows/start_pink_team.sh
```

### 4️⃣ MacBook Terminal
```bash
cd ~/v1-baseline
./configs-2host/mac/start_green_and_leader.sh
```

### 5️⃣ Test (MacBook)
```bash
./build/fire_client localhost:50051 --start 20200810 --end 20200924
```

---

## Port Forwarding Explanation

### What Gets Configured

**Port Forwarding Rules:**
```
Windows Host IP:50054 → WSL IP:50054 (Process D)
Windows Host IP:50055 → WSL IP:50055 (Process E) ← MOST IMPORTANT
Windows Host IP:50056 → WSL IP:50056 (Process F)
```

**Firewall Rules:**
- **INBOUND:** Allow external connections to ports 50054-50056
- **OUTBOUND:** Allow WSL to connect to external networks

### Network Flow

```
Client on Mac
  ↓
Leader A on Mac (localhost:50051)
  ↓ Delegate to Team Pink
Network Request to 192.168.1.65:50055 (Windows Host IP)
  ↓
Windows Port Forwarding
  ↓
172.28.16.89:50055 (WSL IP)
  ↓
Process E in WSL
```

---

## Common Issues & Solutions

### ❌ "Failed to connect to team leader E"

**Quick Fix:**
```powershell
# Windows PowerShell (as Admin)
.\setup_wsl_portforward.ps1
```

**Verify:**
```bash
# Windows WSL
netstat -tuln | grep 50055

# MacBook
nc -zv <WINDOWS_IP> 50055
```

---

### ❌ WSL IP Changed After Reboot

**Symptom:** Port forwarding stops working after Windows reboot

**Fix:**
```powershell
# Windows PowerShell (as Admin)
.\setup_wsl_portforward.ps1
```

The script auto-detects the new WSL IP and updates port forwarding.

---

### ❌ Firewall Blocking Connections

**Verify:**
```powershell
# Windows PowerShell
netsh advfirewall firewall show rule name=all | findstr "Fire Query"
```

Should show 9 rules (3 ports × 3 rule types).

**Fix:**
```powershell
# Re-run setup script
.\setup_wsl_portforward.ps1
```

---

### ❌ No Data from Team Pink

**Check WSL processes:**
```bash
# Windows WSL
ps aux | grep -E 'worker_server|team_leader_server|worker_server.py'
tail logs/process_e.log
```

**Test Pink-only query:**
```bash
# MacBook
./build/fire_client localhost:50051 --start 20200914 --end 20200920
```

---

## Manual Commands Reference

### Get IP Addresses
```bash
# WSL IP
wsl hostname -I | awk '{print $1}'

# Windows IP
ipconfig | findstr "IPv4"
```

### Configure Port Forwarding Manually
```powershell
# PowerShell as Admin - Replace WSL_IP with actual IP
$WSL_IP = "172.28.16.89"

netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50054 connectaddress=$WSL_IP connectport=50054
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50055 connectaddress=$WSL_IP connectport=50055
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50056 connectaddress=$WSL_IP connectport=50056
```

### Configure Firewall Manually
```powershell
# PowerShell as Admin
netsh advfirewall firewall add rule name="Fire Query Port 50054 TCP" dir=in action=allow protocol=TCP localport=50054
netsh advfirewall firewall add rule name="Fire Query Port 50055 TCP" dir=in action=allow protocol=TCP localport=50055
netsh advfirewall firewall add rule name="Fire Query Port 50056 TCP" dir=in action=allow protocol=TCP localport=50056

netsh advfirewall firewall add rule name="WSL Fire Query Outbound 50054" dir=out action=allow protocol=TCP remoteport=50054
netsh advfirewall firewall add rule name="WSL Fire Query Outbound 50055" dir=out action=allow protocol=TCP remoteport=50055
netsh advfirewall firewall add rule name="WSL Fire Query Outbound 50056" dir=out action=allow protocol=TCP remoteport=50056
```

### Check Port Forwarding Status
```powershell
# Show all port forwarding rules
netsh interface portproxy show all

# Show firewall rules
netsh advfirewall firewall show rule name=all | findstr "Fire Query"
```

### Test Connectivity
```bash
# From MacBook to Windows
ping 192.168.1.65
nc -zv 192.168.1.65 50055
telnet 192.168.1.65 50055

# From Windows PowerShell to WSL
Test-NetConnection -ComputerName 172.28.16.89 -Port 50055
```

---

## Cleanup

### Remove Port Forwarding
```powershell
# Windows PowerShell (as Admin)
cd C:\Vineet\v1-baseline
.\cleanup_wsl_portforward.ps1
```

### Manual Cleanup
```powershell
# Remove port forwarding
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50054
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50055
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50056

# Remove firewall rules
netsh advfirewall firewall delete rule name="Fire Query Port 50054 TCP"
netsh advfirewall firewall delete rule name="Fire Query Port 50055 TCP"
netsh advfirewall firewall delete rule name="Fire Query Port 50056 TCP"
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50054"
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50055"
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50056"
```

---

## Documentation Hierarchy

**Start here:**
1. **[QUICK_START_WSL.md](QUICK_START_WSL.md)** - 5-minute quick start

**For detailed setup:**
2. **[SETUP_2HOST_WSL_PORTFORWARD.md](SETUP_2HOST_WSL_PORTFORWARD.md)** - Complete guide

**For architecture/background:**
3. **[DEPLOYMENT_2HOST_GUIDE.md](DEPLOYMENT_2HOST_GUIDE.md)** - Original deployment guide
4. **[README.md](README.md)** - Project overview

---

## Summary

✅ **Port forwarding configured** - Windows IP → WSL IP
✅ **Firewall rules added** - Allow external connections
✅ **Mac config updated** - Use Windows host IP
✅ **Automated scripts** - One-click setup and cleanup
✅ **Complete documentation** - Step-by-step guides

**Result:** MacBook can now connect to WSL processes via Windows host IP, enabling true distributed query processing across 2 physical systems!

---

**Ready to deploy?** Start with [QUICK_START_WSL.md](QUICK_START_WSL.md) for 5-minute setup!
