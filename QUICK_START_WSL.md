# Quick Start Guide - 2-Host Deployment with WSL

## TL;DR - Get Running in 5 Minutes

### On Windows (Run PowerShell as Administrator)

```powershell
# 1. Setup port forwarding (automatic)
cd C:\Vineet\v1-baseline
.\setup_wsl_portforward.ps1

# Note the Windows IP shown (e.g., 192.168.1.65)
```

### On MacBook

```bash
# 2. Edit config with Windows IP
cd ~/v1-baseline
nano configs-2host/mac/process_a.json
# Update line 19: "host": "192.168.1.65" (your Windows IP)
# Save: Ctrl+O, Enter, Ctrl+X
```

### On Windows WSL

```bash
# 3. Start Team Pink
cd /mnt/c/Vineet/v1-baseline
./configs-2host/windows/start_pink_team.sh

# Wait 5 seconds, verify:
tail logs/process_e.log
# Should show: "*** Team Leader server listening on 0.0.0.0:50055 ***"
```

### On MacBook

```bash
# 4. Start Leader + Team Green
cd ~/v1-baseline
./configs-2host/mac/start_green_and_leader.sh

# Verify connection:
tail logs/process_a.log
# Should show: "Connected to team leader E (pink) at 192.168.1.65:50055"
```

### Test It!

```bash
# 5. Run query (on MacBook)
./build/fire_client localhost:50051 --start 20200810 --end 20200924

# Should show records from all processes: A, B, C, D, E, F
```

---

## Troubleshooting Quick Fixes

### "Failed to connect to team leader E"

**On Windows PowerShell (as Admin):**
```powershell
# Re-run port forwarding setup
.\setup_wsl_portforward.ps1
```

**On Windows WSL:**
```bash
# Check processes are running
netstat -tuln | grep 50055
# Should show: tcp 0.0.0.0:50055 ... LISTEN
```

**On MacBook:**
```bash
# Test connectivity
nc -zv 192.168.1.65 50055
# Should connect successfully
```

---

### WSL IP Changed After Reboot

**On Windows PowerShell (as Admin):**
```powershell
# Just re-run the setup script
.\setup_wsl_portforward.ps1
```

This automatically detects the new WSL IP and updates port forwarding.

---

## Stop Everything

**MacBook:**
```bash
./configs-2host/mac/stop_green_and_leader.sh
```

**Windows WSL:**
```bash
./configs-2host/windows/stop_pink_team.sh
```

**Windows PowerShell (Optional - remove port forwarding):**
```powershell
.\cleanup_wsl_portforward.ps1
```

---

## Full Documentation

- **Complete Setup Guide:** [SETUP_2HOST_WSL_PORTFORWARD.md](SETUP_2HOST_WSL_PORTFORWARD.md)
- **Original Deployment Guide:** [DEPLOYMENT_2HOST_GUIDE.md](DEPLOYMENT_2HOST_GUIDE.md)
- **Architecture Details:** [README.md](README.md)

---

## Key Points

1. **Always run `setup_wsl_portforward.ps1` as Administrator first**
2. **Windows IP goes in Mac config** (not WSL IP!)
3. **Start Windows processes first**, Mac processes second
4. **Port 50055 is critical** - this is how Mac reaches Team Pink
5. **WSL IP changes after reboot** - just re-run setup script

---

## What's Happening?

```
MacBook (192.168.1.101)
  ↓ Query
Leader A (localhost:50051)
  ↓ Delegates to Team Pink at 192.168.1.65:50055
Windows Host IP (192.168.1.65:50055)
  ↓ Port Forwarding
WSL IP (172.28.16.89:50055)
  ↓
Process E (Team Pink Leader)
```

Port forwarding makes WSL processes accessible from external network!
