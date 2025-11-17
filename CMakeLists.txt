cmake_minimum_required(VERSION 3.15)
project(FireQuerySystem CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

# Set up gRPC paths - Try Homebrew first, fallback to Anaconda
if(EXISTS "/opt/homebrew/include/grpc")
    set(GRPC_INCLUDE_DIR "/opt/homebrew/include")
    set(GRPC_LIB_DIR "/opt/homebrew/lib")
elseif(EXISTS "/opt/anaconda3/include/grpc")
    set(GRPC_INCLUDE_DIR "/opt/anaconda3/include")
    set(GRPC_LIB_DIR "/opt/anaconda3/lib")
else()
    set(GRPC_INCLUDE_DIR "/usr/local/include")
    set(GRPC_LIB_DIR "/usr/local/lib")
endif()

message(STATUS "Using gRPC from: ${GRPC_INCLUDE_DIR}")
include_directories(${GRPC_INCLUDE_DIR})
link_directories(${GRPC_LIB_DIR})

# Generated protobuf code directory
set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(PROTO_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GENERATED_DIR})

# Compile proto files
set(PROTO_FILES ${PROTO_SRC_DIR}/fire_query.proto)

# Generate C++ code from proto
add_custom_command(
    OUTPUT
        ${PROTO_GENERATED_DIR}/fire_query.pb.cc
        ${PROTO_GENERATED_DIR}/fire_query.pb.h
        ${PROTO_GENERATED_DIR}/fire_query.grpc.pb.cc
        ${PROTO_GENERATED_DIR}/fire_query.grpc.pb.h
    COMMAND protoc
    ARGS --cpp_out=${PROTO_GENERATED_DIR}
         --grpc_out=${PROTO_GENERATED_DIR}
         --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`
         -I${PROTO_SRC_DIR}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating protobuf and gRPC C++ files"
)

# Create a library for generated protobuf code
add_library(fire_query_proto
    ${PROTO_GENERATED_DIR}/fire_query.pb.cc
    ${PROTO_GENERATED_DIR}/fire_query.grpc.pb.cc
)
target_link_libraries(fire_query_proto ${Protobuf_LIBRARIES})
target_include_directories(fire_query_proto PUBLIC ${PROTO_GENERATED_DIR})

# Include common headers and generated directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${PROTO_GENERATED_DIR})

# Common gRPC libraries
set(GRPC_LIBS
    fire_query_proto
    grpc++
    grpc
    gpr
    absl_synchronization
    ${Protobuf_LIBRARIES}
)

# Leader Server (Process A)
add_executable(leader_server
    src/servers/leader/leader_server.cpp
    src/common/metrics.cpp
)
target_link_libraries(leader_server ${GRPC_LIBS})

# Team Leader Server (Processes B, D, E)
add_executable(team_leader_server
    src/servers/team_green/team_leader_server.cpp
    src/common/metrics.cpp
)
target_link_libraries(team_leader_server ${GRPC_LIBS})

# Worker Server (Processes C, D - C++ implementation)
add_executable(worker_server
    src/servers/team_green/worker_server.cpp
    src/common/metrics.cpp
)
target_link_libraries(worker_server ${GRPC_LIBS})

# Fire Query Client
add_executable(fire_client
    src/client/fire_client.cpp
)
target_link_libraries(fire_client ${GRPC_LIBS})

# Platform-specific settings
if(APPLE)
    # macOS specific settings
    foreach(target leader_server team_leader_server worker_server fire_client)
        set_target_properties(${target} PROPERTIES
            LINK_FLAGS "-Wl,-undefined,dynamic_lookup"
        )
    endforeach()
else()
    # Linux specific settings
    foreach(target leader_server team_leader_server worker_server fire_client)
        target_link_libraries(${target} rt pthread)
    endforeach()
endif()

# Print build configuration
message(STATUS "========================================")
message(STATUS "Fire Query System Build Configuration")
message(STATUS "========================================")
message(STATUS "C++ Standard:        ${CMAKE_CXX_STANDARD}")
message(STATUS "Protobuf Version:    ${Protobuf_VERSION}")
message(STATUS "Proto Source Dir:    ${PROTO_SRC_DIR}")
message(STATUS "Generated Code Dir:  ${PROTO_GENERATED_DIR}")
message(STATUS "========================================")
