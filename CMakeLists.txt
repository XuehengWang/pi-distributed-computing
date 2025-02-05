cmake_minimum_required(VERSION 3.8)

project(DistMultService C CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(Threads REQUIRED)

# assume gRPC and all its dependencies are installed,  we use find_package() to locate them

# Protobuf installation
option(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost REQUIRED COMPONENTS system thread)
message(STATUS "Using Boost ${Boost_VERSION}")
find_package(OpenSSL REQUIRED)
message(STATUS "Using OpenSSL ${OpenSSL_VERSION}")

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_REFLECTION gRPC::grpc++_reflection)

set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)

# gRPC installation
# Looks for gRPCConfig.cmake file installed by gRPC's cmake installation.
find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

set(_GRPC_GRPCPP gRPC::grpc++)
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

# From gRPC examples
# Find absl package (log)
find_package(absl CONFIG REQUIRED)

# Proto file
get_filename_component(mult_proto "./protos/distmult_service.proto" ABSOLUTE)
get_filename_component(mult_proto_path "${mult_proto}" PATH)

#Boost installation
#set(BOOST_ROOT "$ENV{HOME}/boost/boost_1_74_0")
#set(Boost_INCLUDE_DIR  "$ENV{HOME}/boost/boost_1_74_0")
#set(Boost_LIBRARY_DIR "$ENV{HOME}/boost/boost_1_74_0/libs")
#find_package(Boost REQUIRED Components system) 

#blis 
# find_package(BLIS CONFIG REQUIRED)

# Generated sources in build, will move to src
set(mult_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/distmult_service.pb.cc")
set(mult_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/distmult_service.pb.h")
#set(mult_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/distmult_service.grpc.pb.cc")
#set(mult_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/distmult_service.grpc.pb.h")
add_custom_command(
      OUTPUT "${mult_proto_srcs}" "${mult_proto_hdrs}"# "${mult_grpc_srcs}" "${mult_grpc_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${mult_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${mult_proto}"
      DEPENDS "${mult_proto}")

# Include generated *.pb.h files
include_directories(${Boost_INCLUDE_DIRS})
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
# include_directories("${CMAKE_CURRENT_BINARY_DIR}/src")

# grpc_proto
add_library(mult_grpc_proto
	#  ${mult_grpc_srcs}
#  ${mult_grpc_hdrs}
  ${mult_proto_srcs}
  ${mult_proto_hdrs})
target_link_libraries(mult_grpc_proto
  absl::absl_log
#  ${_REFLECTION}
#  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
  ${Boost_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread)

# function map 
# add_library(distmult_function_map
#  "function_map.h"
#  "function_map.cc")
#target_link_libraries(distmult_function_map
#  mult_grpc_proto
#  ${_REFLECTION}
#  ${_GRPC_GRPCPP}
#  ${_PROTOBUF_LIBPROTOBUF})

# primary utils
add_library(utils
  "utils.h"
  "utils.cc")
target_link_libraries(utils
  mult_grpc_proto
	#${_REFLECTION})
  #  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
  ${Boost_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread)

# task_handler class
add_library(task_handler
  "task_handler.h"
  "matrix_handler.h"
  "matrix_handler.cc")
target_link_libraries(task_handler
  mult_grpc_proto
  utils
  blis
 # ${_REFLECTION})
  #  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
  ${Boost_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread)

# Primary node resource scheduler
add_library(resource_scheduler STATIC resource_scheduler.cc resource_scheduler.h)
target_link_libraries(resource_scheduler
 mult_grpc_proto
 utils
  #${_REFLECTION})
  #  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
  ${Boost_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread)

# target_include_directories(resource_scheduler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Targets distmult_(client|server)
foreach(_target
  distmult_client distmult_server)
  add_executable(${_target}
    "${_target}.cc")
  target_link_libraries(${_target}
    mult_grpc_proto
    task_handler
    resource_scheduler
    utils
    blis
    absl::flags_parse
    absl::absl_log
    absl::log_initialize
    absl::log_globals
    # ${_REFLECTION})
    #    ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
  ${Boost_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread)
endforeach()

#add_executable(test_resource_scheduler test_resource_scheduler.cc)
#target_link_libraries(test_resource_scheduler PRIVATE resource_scheduler)

#enable_testing()
#add_test(NAME test_resource_scheduler COMMAND test_resource_scheduler)
