# WSL Port Forwarding Cleanup Script
# Run this as Administrator in PowerShell

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Fire Query System - Port Forwarding Cleanup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Remove port forwarding rules
Write-Host "Removing port forwarding rules..." -ForegroundColor Yellow
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50054
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50055
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=50056
Write-Host "Done" -ForegroundColor Green
Write-Host ""

# Remove firewall rules
Write-Host "Removing firewall rules..." -ForegroundColor Yellow
netsh advfirewall firewall delete rule name="Fire Query Port 50054 TCP"
netsh advfirewall firewall delete rule name="Fire Query Port 50055 TCP"
netsh advfirewall firewall delete rule name="Fire Query Port 50056 TCP"
netsh advfirewall firewall delete rule name="Fire Query Port 50054 UDP"
netsh advfirewall firewall delete rule name="Fire Query Port 50055 UDP"
netsh advfirewall firewall delete rule name="Fire Query Port 50056 UDP"
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50054"
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50055"
netsh advfirewall firewall delete rule name="WSL Fire Query Outbound 50056"
Write-Host "Done" -ForegroundColor Green
Write-Host ""

# Verify
Write-Host "Verifying cleanup..." -ForegroundColor Yellow
$portproxy = netsh interface portproxy show all
if ($portproxy -match "50054|50055|50056") {
    Write-Host "WARNING: Some port forwarding rules may still exist" -ForegroundColor Yellow
} else {
    Write-Host "All port forwarding rules removed" -ForegroundColor Green
}
Write-Host ""

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Cleanup Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
