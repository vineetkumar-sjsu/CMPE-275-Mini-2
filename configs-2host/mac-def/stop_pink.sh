#!/bin/bash

echo "Stopping Leader + Team Green processes..."


# Kill all related processes
pkill -f 'leader_server|team_leader_server|worker_server'

sleep 1

echo "All processes stopped."
