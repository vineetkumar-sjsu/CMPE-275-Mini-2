#!/bin/bash

echo "Stopping Leader + Team Green processes..."

# Kill all related processes
pkill -f 'leader_server configs-2host/mac/process_a.json' 2>/dev/null || true
pkill -f 'team_leader_server configs-2host/mac/process_b.json' 2>/dev/null || true
pkill -f 'worker_server configs-2host/mac/process_c.json' 2>/dev/null || true

sleep 1

echo "All processes stopped."
