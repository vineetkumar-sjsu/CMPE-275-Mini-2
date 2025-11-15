#!/bin/bash

# Start all processes for Fire Query System
# This script starts all 6 processes in the background

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BASE_DIR="${SCRIPT_DIR}"
BUILD_DIR="${BASE_DIR}/build"
CONFIG_DIR="${BASE_DIR}/configs"
LOG_DIR="${BASE_DIR}/logs"

# Set FIRE_DATA_PATH environment variable if not already set
if [ -z "$FIRE_DATA_PATH" ]; then
    export FIRE_DATA_PATH="${BASE_DIR}/fire-data"
    echo "Set FIRE_DATA_PATH=${FIRE_DATA_PATH}"
fi

# Create logs directory
mkdir -p ${LOG_DIR}

echo "========================================"
echo "Starting Fire Query System"
echo "========================================"
echo "Logs will be written to ${LOG_DIR}/"
echo ""

# Start Process F (Python worker) first
echo "Starting Process F (Python Worker - Team Pink)..."
python3 ${BASE_DIR}/src/servers/team_pink/worker_server.py ${CONFIG_DIR}/process_f.json \
    > ${LOG_DIR}/process_f.log 2>&1 &
echo "  PID: $! (check logs/process_f.log)"
sleep 1

# Start Process E (Team Leader - Pink)
echo "Starting Process E (Team Leader - Pink)..."
${BUILD_DIR}/team_leader_server ${CONFIG_DIR}/process_e.json \
    > ${LOG_DIR}/process_e.log 2>&1 &
echo "  PID: $! (check logs/process_e.log)"
sleep 1

# Start Process D (Worker - Pink)
echo "Starting Process D (Worker - Team Pink)..."
${BUILD_DIR}/worker_server ${CONFIG_DIR}/process_d.json \
    > ${LOG_DIR}/process_d.log 2>&1 &
echo "  PID: $! (check logs/process_d.log)"
sleep 1

# Start Process C (Worker - Green)
echo "Starting Process C (Worker - Team Green)..."
${BUILD_DIR}/worker_server ${CONFIG_DIR}/process_c.json \
    > ${LOG_DIR}/process_c.log 2>&1 &
echo "  PID: $! (check logs/process_c.log)"
sleep 1

# Start Process B (Team Leader - Green)
echo "Starting Process B (Team Leader - Green)..."
${BUILD_DIR}/team_leader_server ${CONFIG_DIR}/process_b.json \
    > ${LOG_DIR}/process_b.log 2>&1 &
echo "  PID: $! (check logs/process_b.log)"
sleep 1

# Start Process A (Leader) last
echo "Starting Process A (Leader)..."
${BUILD_DIR}/leader_server ${CONFIG_DIR}/process_a.json \
    > ${LOG_DIR}/process_a.log 2>&1 &
echo "  PID: $! (check logs/process_a.log)"
sleep 2

echo ""
echo "========================================"
echo "All processes started!"
echo "========================================"
echo ""
echo "Process Status:"
pgrep -f "leader_server|team_leader_server|worker_server|worker_server.py" | while read pid; do
    echo "  PID $pid: $(ps -p $pid -o comm=)"
done

echo ""
echo "To test the system, run:"
echo "  ${BUILD_DIR}/fire_client localhost:50051"
echo ""
echo "To monitor logs in real-time:"
echo "  tail -f ${LOG_DIR}/*.log"
echo ""
echo "To stop all processes:"
echo "  ./stop_all.sh"
echo ""
