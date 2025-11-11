#!/bin/bash

# Clean all build artifacts and generated files
# This script provides a clean slate for rebuilding the project

echo "========================================="
echo "Fire Query System - Cleanup Script"
echo "========================================="
echo ""

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "${SCRIPT_DIR}"

# Stop all running processes first
echo "Checking for running processes..."
if pgrep -f "leader_server|team_leader_server|worker_server" > /dev/null; then
    echo "⚠️  Warning: Fire Query System processes are still running!"
    read -p "Do you want to stop them? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        if [ -f "./stop_all.sh" ]; then
            ./stop_all.sh
        else
            echo "Stopping processes manually..."
            pkill -f "leader_server"
            pkill -f "team_leader_server"
            pkill -f "worker_server"
            sleep 2
        fi
    fi
fi

echo ""
echo "Cleaning build artifacts..."

# Remove build directory
if [ -d "build" ]; then
    echo "  ✓ Removing build/"
    rm -rf build
fi

# Remove generated protobuf files (C++)
echo "  ✓ Removing generated C++ protobuf files"
find proto -name "*.pb.cc" -delete 2>/dev/null
find proto -name "*.pb.h" -delete 2>/dev/null

# Remove generated protobuf files (Python)
echo "  ✓ Removing generated Python protobuf files"
find proto -name "*_pb2.py" -delete 2>/dev/null
find proto -name "*_pb2_grpc.py" -delete 2>/dev/null
find proto -name "*_pb2.pyi" -delete 2>/dev/null

# Remove Python cache
echo "  ✓ Removing Python cache files"
find . -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null
find . -type f -name "*.pyc" -delete 2>/dev/null
find . -type f -name "*.pyo" -delete 2>/dev/null

# Remove log files
if [ -d "logs" ]; then
    LOG_COUNT=$(ls -1 logs/*.log 2>/dev/null | wc -l)
    if [ $LOG_COUNT -gt 0 ]; then
        echo "  ✓ Removing log files (${LOG_COUNT} files)"
        rm -f logs/*.log
    fi
fi

# Remove compiled object files
echo "  ✓ Removing compiled object files"
find . -type f -name "*.o" -delete 2>/dev/null
find . -type f -name "*.a" -delete 2>/dev/null
find . -type f -name "*.so" -delete 2>/dev/null
find . -type f -name "*.dylib" -delete 2>/dev/null

# Remove CMake cache
if [ -f "CMakeCache.txt" ]; then
    echo "  ✓ Removing CMakeCache.txt"
    rm -f CMakeCache.txt
fi

# Remove any core dumps
if ls core.* 1> /dev/null 2>&1; then
    echo "  ✓ Removing core dump files"
    rm -f core.*
fi

# Remove .DS_Store files (macOS)
if [[ "$OSTYPE" == "darwin"* ]]; then
    DS_COUNT=$(find . -name ".DS_Store" 2>/dev/null | wc -l)
    if [ $DS_COUNT -gt 0 ]; then
        echo "  ✓ Removing .DS_Store files (${DS_COUNT} files)"
        find . -name ".DS_Store" -delete 2>/dev/null
    fi
fi

# Remove temporary files
echo "  ✓ Removing temporary files"
find . -type f -name "*.tmp" -delete 2>/dev/null
find . -type f -name "*.temp" -delete 2>/dev/null
find . -type f -name "*~" -delete 2>/dev/null

echo ""
echo "========================================="
echo "✅ Cleanup Complete!"
echo "========================================="
echo ""
echo "The following have been removed:"
echo "  • Build directory (build/)"
echo "  • Generated protobuf files (C++ and Python)"
echo "  • Python cache (__pycache__, *.pyc)"
echo "  • Log files (logs/*.log)"
echo "  • Compiled object files (*.o, *.so, *.dylib)"
echo "  • CMake cache files"
echo "  • Temporary files"
echo ""
echo "To rebuild the project:"
echo "  1. ./generate_proto.sh"
echo "  2. mkdir build && cd build"
echo "  3. cmake .."
echo "  4. make -j4"
echo "  5. cd .."
echo "  6. ./start_all.sh"
echo ""
echo "Or use the build.sh script:"
echo "  ./build.sh"
echo ""
