# WSL Port Forwarding Setup Script for Fire Query System
# Run this as Administrator in PowerShell

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Fire Query System - WSL Port Forwarding Setup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Get WSL IP dynamically
Write-Host "Getting WSL IP address..." -ForegroundColor Yellow
$WSL_IP = (wsl hostname -I).Trim()

if ([string]::IsNullOrEmpty($WSL_IP)) {
    Write-Host "ERROR: Could not get WSL IP address. Is WSL running?" -ForegroundColor Red
    exit 1
}

Write-Host "WSL IP: $WSL_IP" -ForegroundColor Green
Write-Host ""

# Get Windows IP (first IPv4 address)
Write-Host "Getting Windows Host IP..." -ForegroundColor Yellow
$WINDOWS_IP = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.IPAddress -like "192.168.*" -or $_.IPAddress -like "10.*" -or $_.IPAddress -like "172.*"} | Select-Object -First 1).IPAddress

if ([string]::IsNullOrEmpty($WINDOWS_IP)) {
    Write-Host "WARNING: Could not detect Windows IP. You'll need to find it manually." -ForegroundColor Yellow
} else {
    Write-Host "Windows Host IP: $WINDOWS_IP" -ForegroundColor Green
    Write-Host ""
    Write-Host "IMPORTANT: Update configs-2host/mac/process_a.json with this IP!" -ForegroundColor Yellow
    Write-Host "  Edit line 19: `"host`": `"$WINDOWS_IP`"" -ForegroundColor Yellow
}
Write-Host ""

# Remove old port forwarding rules (if any)
Write-Host "Removing old port forwarding rules..." -ForegroundColor Yellow
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50054 2>$null
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50055 2>$null
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50056 2>$null
Write-Host "Done" -ForegroundColor Green
Write-Host ""

# Add new port forwarding rules
Write-Host "Adding port forwarding rules..." -ForegroundColor Yellow
Write-Host "  Port 50054 (Process D - Worker)" -ForegroundColor Cyan
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50054 connectaddress=$WSL_IP connectport=50054

Write-Host "  Port 50055 (Process E - Team Leader) - CRITICAL!" -ForegroundColor Cyan
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50055 connectaddress=$WSL_IP connectport=50055

Write-Host "  Port 50056 (Process F - Python Worker)" -ForegroundColor Cyan
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=50056 connectaddress=$WSL_IP connectport=50056

Write-Host "Done" -ForegroundColor Green
Write-Host ""

# Add Windows Firewall rules
Write-Host "Configuring Windows Firewall..." -ForegroundColor Yellow

# Remove old rules first
netsh advfirewall firewall delete rule name="Fire Query Port 50054 TCP" 2>$null
netsh advfirewall firewall delete rule name="Fire Query Port 50055 TCP" 2>$null
netsh advfirewall firewall delete rule name="Fire Query Port 50056 TCP" 2>$null
netsh advfirewall firewall delete rule name="Fire Query Port 50054 UDP" 2>$null
netsh advfirewall firewall delete rule name="Fire Query Port 50055 UDP" 2>$null
netsh advfirewall firewall delete rule name="Fire Query Port 50056 UDP" 2>$null
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50054" 2>$null
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50055" 2>$null
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50056" 2>$null

# Add INBOUND rules (Mac to Windows)
Write-Host "  Adding INBOUND rules (TCP)" -ForegroundColor Cyan
netsh advfirewall firewall add rule name="Fire Query Port 50054 TCP" dir=in action=allow protocol=TCP localport=50054
netsh advfirewall firewall add rule name="Fire Query Port 50055 TCP" dir=in action=allow protocol=TCP localport=50055
netsh advfirewall firewall add rule name="Fire Query Port 50056 TCP" dir=in action=allow protocol=TCP localport=50056

Write-Host "  Adding INBOUND rules (UDP)" -ForegroundColor Cyan
netsh advfirewall firewall add rule name="Fire Query Port 50054 UDP" dir=in action=allow protocol=UDP localport=50054
netsh advfirewall firewall add rule name="Fire Query Port 50055 UDP" dir=in action=allow protocol=UDP localport=50055
netsh advfirewall firewall add rule name="Fire Query Port 50056 UDP" dir=in action=allow protocol=UDP localport=50056

# Add OUTBOUND rules (WSL to external network)
Write-Host "  Adding OUTBOUND rules" -ForegroundColor Cyan
netsh advfirewall firewall add rule name="WSL Fire Query Outbound 50054" dir=out action=allow protocol=TCP remoteport=50054
netsh advfirewall firewall add rule name="WSL Fire Query Outbound 50055" dir=out action=allow protocol=TCP remoteport=50055
netsh advfirewall firewall add rule name="WSL Fire Query Outbound 50056" dir=out action=allow protocol=TCP remoteport=50056

Write-Host "Done" -ForegroundColor Green
Write-Host ""

# Display configuration
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Configuration Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "WSL IP:          $WSL_IP" -ForegroundColor White
Write-Host "Windows Host IP: $WINDOWS_IP" -ForegroundColor White
Write-Host ""
Write-Host "Port Forwarding Rules:" -ForegroundColor White
netsh interface portproxy show all
Write-Host ""

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Setup Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next Steps:" -ForegroundColor Yellow
Write-Host "1. Update configs-2host/mac/process_a.json with Windows IP: $WINDOWS_IP" -ForegroundColor White
Write-Host "2. Build the project on Windows/WSL" -ForegroundColor White
Write-Host "3. Start Team Pink processes: ./configs-2host/windows/start_pink_team.sh" -ForegroundColor White
Write-Host "4. On Mac, start Leader + Team Green" -ForegroundColor White
Write-Host ""
Write-Host "To test port forwarding from Mac:" -ForegroundColor Yellow
Write-Host "  nc -zv $WINDOWS_IP 50055" -ForegroundColor White
Write-Host "  (Should connect successfully when processes are running)" -ForegroundColor Gray
Write-Host ""
Write-Host "To verify ports are listening in WSL:" -ForegroundColor Yellow
Write-Host "  wsl netstat -tuln | grep -E '5005[456]'" -ForegroundColor White
Write-Host ""
Write-Host "To remove port forwarding later:" -ForegroundColor Yellow
Write-Host "  .\cleanup_wsl_portforward.ps1" -ForegroundColor White
Write-Host ""
