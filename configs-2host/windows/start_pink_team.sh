#!/bin/bash

# Start Team Pink processes on Windows/WSL system
# Processes: E (Team Leader), D (Worker), F (Python Worker)

set -e

echo "========================================"
echo "Starting Team Pink on Windows/WSL"
echo "========================================"

# Set data path
export FIRE_DATA_PATH="$(pwd)/fire-data"
echo "FIRE_DATA_PATH=$FIRE_DATA_PATH"

# Create logs directory
mkdir -p logs

# Build path (adjust if needed)
BUILD_DIR="$(pwd)/build"

echo ""
echo "Starting processes..."
echo ""

# Start Process F (Python Worker) - Port 50056
echo "Starting Process F (Python Worker - Team Pink)..."
nohup python3 src/servers/team_pink/worker_server.py configs-2host/windows/process_f.json > logs/process_f.log 2>&1 &
PID_F=$!
echo "  PID: $PID_F (check logs/process_f.log)"
sleep 2

# Start Process D (C++ Worker) - Port 50054
echo "Starting Process D (Worker - Team Pink)..."
nohup $BUILD_DIR/worker_server configs-2host/windows/process_d.json > logs/process_d.log 2>&1 &
PID_D=$!
echo "  PID: $PID_D (check logs/process_d.log)"
sleep 2

# Start Process E (Team Leader) - Port 50055
echo "Starting Process E (Team Leader - Pink)..."
nohup $BUILD_DIR/team_leader_server configs-2host/windows/process_e.json > logs/process_e.log 2>&1 &
PID_E=$!
echo "  PID: $PID_E (check logs/process_e.log)"
sleep 2

echo ""
echo "========================================"
echo "Team Pink processes started!"
echo "========================================"
echo ""
echo "Process Status:"
echo "  PID $PID_F: Process F (Python Worker)"
echo "  PID $PID_D: Process D (C++ Worker)"
echo "  PID $PID_E: Process E (Team Leader)"
echo ""
echo "Ports listening:"
echo "  50055: Process E (Team Leader)"
echo "  50054: Process D (Worker)"
echo "  50056: Process F (Python Worker)"
echo ""
echo "To check logs:"
echo "  tail -f logs/process_e.log"
echo "  tail -f logs/process_d.log"
echo "  tail -f logs/process_f.log"
echo ""
echo "To stop all processes:"
echo "  pkill -f 'worker_server|team_leader_server|worker_server.py'"
echo ""
