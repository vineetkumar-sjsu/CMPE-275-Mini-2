#!/bin/bash

# Start Leader + Team Green processes on MacBook
# Processes: A (Leader), B (Team Leader Green), C (Worker Green)

set -e

echo "========================================"
echo "Starting Leader + Team Green on MacBook"
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

# Start Process C (Worker) - Port 50053
echo "Starting Process C (Worker - Team Green)..."
nohup $BUILD_DIR/worker_server configs-2host/mac/process_c.json > logs/process_c.log 2>&1 &
PID_C=$!
echo "  PID: $PID_C (check logs/process_c.log)"
sleep 2

# Start Process B (Team Leader) - Port 50052
echo "Starting Process B (Team Leader - Green)..."
nohup $BUILD_DIR/team_leader_server configs-2host/mac/process_b.json > logs/process_b.log 2>&1 &
PID_B=$!
echo "  PID: $PID_B (check logs/process_b.log)"
sleep 2

# Start Process A (Leader) - Port 50051
echo "Starting Process A (Leader)..."
nohup $BUILD_DIR/leader_server configs-2host/mac/process_a.json > logs/process_a.log 2>&1 &
PID_A=$!
echo "  PID: $PID_A (check logs/process_a.log)"
sleep 3

echo ""
echo "========================================"
echo "All processes started!"
echo "========================================"
echo ""
echo "Process Status:"
echo "  PID $PID_C: Process C (Worker)"
echo "  PID $PID_B: Process B (Team Leader)"
echo "  PID $PID_A: Process A (Leader)"
echo ""
echo "Ports listening:"
echo "  50051: Process A (Leader) - CLIENT CONNECTS HERE"
echo "  50052: Process B (Team Leader)"
echo "  50053: Process C (Worker)"
echo ""
echo "To test the system, run:"
echo "  $BUILD_DIR/fire_client localhost:50051 --start 20200810 --end 20200924"
echo ""
echo "To check logs:"
echo "  tail -f logs/process_a.log"
echo "  tail -f logs/process_b.log"
echo "  tail -f logs/process_c.log"
echo ""
echo "To stop all processes:"
echo "  pkill -f 'leader_server|team_leader_server|worker_server'"
echo ""
