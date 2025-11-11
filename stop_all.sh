#!/bin/bash

# Stop all Fire Query System processes

echo "Stopping Fire Query System processes..."

# Kill all related processes
pkill -f "leader_server"
pkill -f "team_leader_server"
pkill -f "worker_server"
pkill -f "worker_server.py"

sleep 1

# Check if any processes remain
remaining=$(pgrep -f "leader_server|team_leader_server|worker_server|worker_server.py" | wc -l)

if [ $remaining -eq 0 ]; then
    echo "All processes stopped successfully."
else
    echo "Warning: $remaining process(es) still running."
    echo "You may need to kill them manually:"
    pgrep -f "leader_server|team_leader_server|worker_server|worker_server.py"
fi
