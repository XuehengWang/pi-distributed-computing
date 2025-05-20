cmake_minimum_required(VERSION 3.15)
project(pi-distributed-computing)

add_compile_options(-UVOID)  # <--- 💡 This fixes the Cap’n Proto conflict

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# ----------------------------
# Dependencies
# ----------------------------

# Protobuf
option(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)

# Abseil (for logging)
find_package(absl CONFIG REQUIRED)

# # Cap’n Proto
find_package(CapnProto CONFIG REQUIRED)
message(STATUS "CapnProto found: ${CapnProto_FOUND}")

# ----------------------------
# Cap’n Proto (manual integration using pkg-config)
# ----------------------------

# find_package(PkgConfig REQUIRED)
# pkg_check_modules(CAPNP REQUIRED capnp)
# pkg_check_modules(CAPNP_RPC REQUIRED capnp-rpc)

# # Set Cap’n Proto compiler
# find_program(CAPNP_COMPILER capnp)
# if(NOT CAPNP_COMPILER)
#   message(FATAL_ERROR "capnp compiler not found. Please install Cap'n Proto or add it to your PATH.")
# endif()
# message(STATUS "Using Cap’n Proto compiler: ${CAPNP_COMPILER}")

# # Add Cap’n Proto include and lib dirs globally
# include_directories(${CAPNP_INCLUDE_DIRS} ${CAPNP_RPC_INCLUDE_DIRS})
# link_directories(${CAPNP_LIBRARY_DIRS} ${CAPNP_RPC_LIBRARY_DIRS})

# # ----------------------------
# # Cap’n Proto Schema
# # ----------------------------

# set(SCHEMA_FILE ${CMAKE_SOURCE_DIR}/protos/matrix.capnp)
# set(GENERATED_DIR ${CMAKE_BINARY_DIR}/protos)
# set(GENERATED_SRC ${GENERATED_DIR}/matrix.capnp.c++)
# set(GENERATED_HDR ${GENERATED_DIR}/matrix.capnp.h)

# file(MAKE_DIRECTORY ${GENERATED_DIR})

# add_custom_command(
#   OUTPUT ${GENERATED_SRC} ${GENERATED_HDR}
#   COMMAND ${CAPNP_COMPILER} compile -oc++:${GENERATED_DIR} --src-prefix=${CMAKE_SOURCE_DIR}/protos ${SCHEMA_FILE}
#   DEPENDS ${SCHEMA_FILE}
#   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/protos
#   COMMENT "Generating Cap'n Proto sources..."
# )

# add_custom_target(generate_capnp ALL DEPENDS ${GENERATED_SRC} ${GENERATED_HDR})

# set(CAPNP_SRCS ${GENERATED_SRC})
# set(CAPNP_HDRS ${GENERATED_HDR})

set(CAPNP_SRCS ${CMAKE_SOURCE_DIR}/protos/matrix.capnp.c++)
set(CAPNP_HDRS ${CMAKE_SOURCE_DIR}/protos/matrix.capnp.h)
# ----------------------------
# Targets
# ----------------------------

add_library(utils
  utils.cc
  utils.h
  ${CAPNP_SRCS}
  ${CAPNP_HDRS}
)
#add_dependencies(utils generate_capnp)

target_include_directories(utils
  PRIVATE
    ${CMAKE_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/protos
    ${CMAKE_BINARY_DIR}/protos
    ${CMAKE_CURRENT_BINARY_DIR}/protos         # <-- Add this line
)

target_link_libraries(utils
  PRIVATE
    #mult_grpc_proto
    ${_PROTOBUF_LIBPROTOBUF}
    CapnProto::capnp
    CapnProto::capnp-rpc
)
# if(TARGET CapnProto::capnp-rpc)
#   target_link_libraries(utils PRIVATE CapnProto::capnp-rpc)
# endif()

# task_handler
add_library(task_handler
  task_handler.h
  matrix_handler.cc
  matrix_handler.h
)
target_include_directories(task_handler
  PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/protos
    ${CMAKE_CURRENT_BINARY_DIR}/protos
)
target_link_libraries(task_handler
  utils
  blis
  ${_PROTOBUF_LIBPROTOBUF}
)

# resource_scheduler
add_library(resource_scheduler STATIC
  resource_scheduler.cc
  resource_scheduler.h
)
target_link_libraries(resource_scheduler
  utils
  ${_PROTOBUF_LIBPROTOBUF}
)

add_library(capnp_matrix_client
  CapnpMatrixClient.cc
  CapnpMatrixClient.h
)
target_link_libraries(capnp_matrix_client
  utils
  CapnProto::capnp
  CapnProto::capnp-rpc
)
target_include_directories(capnp_matrix_client
  PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/protos
    ${CMAKE_CURRENT_BINARY_DIR}/protos
)

#Cap'n Proto-aware executables
foreach(_target distmult_client distmult_server)
  set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${_target}.cc")
  if(EXISTS "${_src}")
    add_executable(${_target} "${_src}")
    target_link_libraries(${_target}
      task_handler
      resource_scheduler
      utils
      capnp_matrix_client
      blis
      absl::flags_parse
      absl::absl_log
      absl::log_initialize
      absl::log_globals
      CapnProto::capnp
      CapnProto::capnp-rpc
      ${_PROTOBUF_LIBPROTOBUF}
    )
    target_include_directories(${_target}
      PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/protos
    )
  endif()
endforeach()

