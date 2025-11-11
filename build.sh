#!/bin/bash

# Build script for Fire Query System
# Mini-2 Project - CMPE 275

set -e  # Exit on error

echo "========================================"
echo "Fire Query System - Build Script"
echo "========================================"

# Clean previous build
echo "Cleaning previous build..."
rm -rf build
mkdir -p build

# Build C++ components
echo ""
echo "Building C++ servers and client..."
cd build
cmake ..
make -j4

echo ""
echo "========================================"
echo "Build Summary"
echo "========================================"
echo "Executables created in build/:"
echo "  - leader_server (Process A)"
echo "  - team_leader_server (Processes B, D, E)"
echo "  - worker_server (Processes C, D)"
echo "  - fire_client"
echo ""

# Build Python proto files
echo "Building Python protobuf files..."
cd ..
python3 -m grpc_tools.protoc \
    -I./proto \
    --python_out=./proto \
    --pyi_out=./proto \
    --grpc_python_out=./proto \
    ./proto/fire_query.proto

echo ""
echo "Python protobuf files generated in proto/:"
echo "  - fire_query_pb2.py"
echo "  - fire_query_pb2.pyi"
echo "  - fire_query_pb2_grpc.py"
echo ""

echo "========================================"
echo "Build Complete!"
echo "========================================"
echo ""
echo "Next steps:"
echo "  1. Review configs/process_*.json for deployment settings"
echo "  2. Run ./start_all.sh to start all processes"
echo "  3. Run ./build/fire_client localhost:50051 to test"
echo ""
