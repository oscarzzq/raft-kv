cmake_minimum_required(VERSION 3.20)
project(raft_kv)

list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
set(CMAKE_PREFIX_PATH "/opt/homebrew" ${CMAKE_PREFIX_PATH})

set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find gRPC and Protobuf installed via Homebrew
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)

# Proto file setup
set(PROTO_FILE ${CMAKE_SOURCE_DIR}/proto/raft.proto)

# Auto-generate gRPC and protobuf source files from your .proto
get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
set(PROTO_OUT ${CMAKE_BINARY_DIR}/proto)
file(MAKE_DIRECTORY ${PROTO_OUT})

set(PROTO_SRCS "${PROTO_OUT}/${PROTO_NAME}.pb.cc")
set(PROTO_HDRS "${PROTO_OUT}/${PROTO_NAME}.pb.h")
set(GRPC_SRCS  "${PROTO_OUT}/${PROTO_NAME}.grpc.pb.cc")
set(GRPC_HDRS  "${PROTO_OUT}/${PROTO_NAME}.grpc.pb.h")

# This command runs protoc automatically when you build
add_custom_command(
        OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
        COMMAND protoc
        --grpc_out=${PROTO_OUT}
        --cpp_out=${PROTO_OUT}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        -I${CMAKE_SOURCE_DIR}/proto
        ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
)

# Shared library containing generated proto + grpc code
add_library(proto_lib
        ${PROTO_SRCS}
        ${GRPC_SRCS}
)
target_include_directories(proto_lib PUBLIC ${PROTO_OUT})
target_link_libraries(proto_lib PUBLIC gRPC::grpc++ protobuf::libprotobuf)

# Server executable
add_executable(raft_server
        src/server/main.cpp
        src/server/RaftNode.cpp
)
target_include_directories(raft_server PRIVATE ${PROTO_OUT} src/server)
target_link_libraries(raft_server PRIVATE proto_lib gRPC::grpc++)

# Client executable
add_executable(raft_client
        src/client/main.cpp
)
target_include_directories(raft_client PRIVATE ${PROTO_OUT})
target_link_libraries(raft_client PRIVATE proto_lib gRPC::grpc++)