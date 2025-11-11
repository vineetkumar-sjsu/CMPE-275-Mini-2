#!/bin/bash

# Generate protobuf files for Python

echo "Generating Python protobuf files..."

python3 -m grpc_tools.protoc \
    -I./proto \
    --python_out=./proto \
    --pyi_out=./proto \
    --grpc_python_out=./proto \
    ./proto/fire_query.proto

if [ $? -eq 0 ]; then
    echo "✓ Generated proto/fire_query_pb2.py"
    echo "✓ Generated proto/fire_query_pb2.pyi"
    echo "✓ Generated proto/fire_query_pb2_grpc.py"
    echo ""
    echo "Python server is ready to run!"
else
    echo "✗ Failed to generate protobuf files"
    echo "Make sure grpcio-tools is installed:"
    echo "  python3 -m pip install grpcio-tools"
    exit 1
fi
