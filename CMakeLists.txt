cmake_minimum_required(VERSION 3.8)

project(DistMultService C CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(Threads REQUIRED)

# assume gRPC and all its dependencies are installed, we use find_package() to locate them

# Protobuf installation
option(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

#find_package(Boost REQUIRED COMPONENTS system thread)
#message(STATUS "Using Boost ${Boost_VERSION}")
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

# Find BPF and XDP libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBBPF REQUIRED libbpf)
pkg_check_modules(LIBXDP REQUIRED libxdp)
message(STATUS "Using libbpf ${LIBBPF_VERSION}")
message(STATUS "Using libxdp ${LIBXDP_VERSION}")

#-------- Build AF_XDP Kernel (BPF) Program --------
# Path to your BPF program source file
set(BPF_SRC "${CMAKE_CURRENT_SOURCE_DIR}/xdp/af_xdp_kern.c")
# Output object file (you can adjust the name/location as needed.)
set(BPF_OBJ "${CMAKE_BINARY_DIR}/af_xdp_kern.o")

# Find clang which is needed for the BPF compilation.
find_program(CLANG clang REQUIRED)
if(NOT CLANG)
  message(FATAL_ERROR "clang is required for compiling the BPF program")
endif()
message(STATUS "Using clang: ${CLANG}")

#Add a custom command to compile the bpf object
add_custom_command(
	OUTPUT ${BPF_OBJ}
	COMMAND ${CLANG} -O2 -g -target bpf -D__TARGET_ARCH_arm64 -Wall -Werror -c ${BPF_SRC} -o ${BPF_OBJ} 
	DEPENDS ${BPF_SRC}
	COMMENT "Compiling AF_XDP kernel BPF program: ${BPF_SRC} -> ${BPF_OBJ}"
)

# Create a custom target that compiles the BPF program using clang.
add_custom_target(af_xdp_kern ALL
  DEPENDS ${BPF_OBJ}
)

# Assuming the Makefile that builds common objects is in the "common" folder.
set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")

# Create a custom target that runs the external make command.
add_custom_target(common_objs ALL
  COMMAND +${CMAKE_MAKE_PROGRAM} -C ${COMMON_DIR}
  COMMENT "Building common objects using external Makefile in ${COMMON_DIR}"
)

# List the object files that the external Makefile produces.
set(COMMON_OBJS
    "${COMMON_DIR}/common_params.o"
    "${COMMON_DIR}/common_user_bpf_xdp.o"
)


# Create a custom command to archive these object files into a static library.
# This assumes that your external Makefile does not already produce a static library.
add_custom_command(
  OUTPUT "${COMMON_DIR}/libcommon.a"
  COMMAND ${CMAKE_AR} rcs "${COMMON_DIR}/libcommon.a" ${COMMON_OBJS}
  DEPENDS common_objs
  COMMENT "Archiving common objects into libcommon.a"
)

# Create a custom target that depends on the static library.
add_custom_target(libcommon ALL DEPENDS "${COMMON_DIR}/libcommon.a")

# Create an IMPORTED static library target so that CMake can link against it.
add_library(common_lib STATIC IMPORTED)
set_target_properties(common_lib PROPERTIES
  IMPORTED_LOCATION "${COMMON_DIR}/libcommon.a"
)

# Proto file
get_filename_component(mult_proto "./protos/distmult_service.proto" ABSOLUTE)
get_filename_component(mult_proto_path "${mult_proto}" PATH)

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

# Include directories
include_directories(
  "${CMAKE_CURRENT_BINARY_DIR}"
  ${LIBBPF_INCLUDE_DIRS}
  ${LIBXDP_INCLUDE_DIRS}
)

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
  #  ${Boost_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread)

# primary utils
add_library(utils
  "utils.h"
  "utils.cc")
target_link_libraries(utils
  mult_grpc_proto
	#${_REFLECTION})
  #  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
  # ${Boost_LIBRARIES}
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
  # ${Boost_LIBRARIES}
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
  # ${Boost_LIBRARIES}
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
  PRIVATE 
    common_lib
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
  # ${Boost_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread
  ${LIBBPF_LIBRARIES}
  ${LIBXDP_LIBRARIES}
)
  target_include_directories(${_target} PRIVATE ${LIBBPF_INCLUDE_DIRS} ${LIBXDP_INCLUDE_DIRS})
  # Ensure the BPF program is compiled before the executable (if runtime loading depends on it)
    add_dependencies(${_target} af_xdp_kern libcommon)
endforeach()

#add_executable(test_resource_scheduler test_resource_scheduler.cc)
#target_link_libraries(test_resource_scheduler PRIVATE resource_scheduler)

#enable_testing()
#add_test(NAME test_resource_scheduler COMMAND test_resource_scheduler)

