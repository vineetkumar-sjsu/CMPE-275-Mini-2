#!/bin/bash

# Start Leader + Team Pink processes on MacBook
# Processes: E (Team Leader Pink), D (Worker Pink), F (Worker Pink)

set -e

echo "========================================"
echo "Starting Leader + Team Pink on MacBook"
echo "========================================"

# Set data path
export FIRE_DATA_PATH="$(pwd)/fire-data"
echo "FIRE_DATA_PATH=$FIRE_DATA_PATH"

# Set config path
SCRIPT_DIR="$(pwd)"
BASE_DIR="${SCRIPT_DIR}"
BUILD_DIR="$(pwd)/build"
LOG_DIR="$(pwd)/logs"
CONFIG_DIR="$(pwd)/configs-2host/mac-def"

echo ""
echo "Starting processes..."
echo ""


# Start Process F (Python worker) first
echo "Starting Process F (Python Worker - Team Pink)..."
python3 ${BASE_DIR}/src/servers/team_pink/worker_server.py ${CONFIG_DIR}/process_f.json \
    > ${LOG_DIR}/process_f.log 2>&1 & PID_F=$!
echo "  PID: $PID_F (check logs/process_f.log)"
sleep 1

# Start Process E (Team Leader - Pink)
echo "Starting Process E (Team Leader - Pink)..."
${BUILD_DIR}/team_leader_server ${CONFIG_DIR}/process_e.json \
    > ${LOG_DIR}/process_e.log 2>&1 & PID_E=$!
echo "  PID: $PID_E (check logs/process_e.log)"
sleep 1

# Start Process D (Worker - Pink)
echo "Starting Process D (Worker - Team Pink)..."
${BUILD_DIR}/worker_server ${CONFIG_DIR}/process_d.json \
    > ${LOG_DIR}/process_d.log 2>&1 & PID_D=$!
echo "  PID: $PID_D (check logs/process_d.log)"
sleep 1


echo ""
echo "========================================"
echo "All processes started!"
echo "========================================"
echo ""
echo "Process Status:"
echo "  PID $PID_F: Process F (Worker)"
echo "  PID $PID_E: Process E (Team Leader)"
echo "  PID $PID_D: Process D (Leader)"
echo ""
echo "Ports listening:"
echo "  50056: Process D (Worker)"
echo "  50055: Process E (Team Leader)"
echo "  50054: Process F (Worker)"
echo ""
echo ""
echo "To check logs:"
echo "  tail -f logs/process_d.log"
echo "  tail -f logs/process_e.log"
echo "  tail -f logs/process_f.log"
echo ""
echo "To stop all processes:"
echo "  pkill -f 'leader_server|team_leader_server|worker_server'"
echo ""
