#!/bin/bash

echo "Stopping Team Pink processes..."

# Kill all related processes
pkill -f 'worker_server configs-2host/windows/process_d.json' 2>/dev/null || true
pkill -f 'team_leader_server configs-2host/windows/process_e.json' 2>/dev/null || true
pkill -f 'worker_server.py configs-2host/windows/process_f.json' 2>/dev/null || true

sleep 1

echo "All Team Pink processes stopped."
