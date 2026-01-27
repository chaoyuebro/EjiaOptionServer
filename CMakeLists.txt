cmake_minimum_required(VERSION 3.24)
project(EjiaOptionServer)
set(CMAKE_CXX_STANDARD 14)
add_definitions(-D__STDC_FORMAT_MACROS)
file(GLOB_RECURSE  CPP_FILES
        "${PROJECT_SOURCE_DIR}/HPSocket/*.h"
        "${PROJECT_SOURCE_DIR}/HPSocket/*.cpp"
        "${PROJECT_SOURCE_DIR}/libcurl/*.h"
        "${PROJECT_SOURCE_DIR}/protocol/*.h"
        "${PROJECT_SOURCE_DIR}/protocol/*.cc"
        "${PROJECT_SOURCE_DIR}/spdlog/*.h"
        "${PROJECT_SOURCE_DIR}/rapidjson/*.h"
        "${PROJECT_SOURCE_DIR}/timer/*.h"
        "${PROJECT_SOURCE_DIR}/timer/*.cpp"
        "${PROJECT_SOURCE_DIR}/alibabacloud/*.h"
        "${PROJECT_SOURCE_DIR}/breakpad/include/*.h"
)

file(GLOB  SRC_FILES
        "${PROJECT_SOURCE_DIR}/*.h"
        "${PROJECT_SOURCE_DIR}/*.cpp"
        "${PROJECT_SOURCE_DIR}/*.cc"
)
include_directories(${PROJECT_SOURCE_DIR}/  ${PROJECT_SOURCE_DIR}/breakpad/include/)

link_directories(${PROJECT_SOURCE_DIR}/HPSocket/zlib  ${PROJECT_SOURCE_DIR}/libcurl
        ${PROJECT_SOURCE_DIR}/protocol ${PROJECT_SOURCE_DIR}/jemalloc ${PROJECT_SOURCE_DIR}/sqlLib
        ${PROJECT_SOURCE_DIR}/alibabacloud ${PROJECT_SOURCE_DIR}/breakpad/lib)
SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
SET(CMAKE_INSTALL_RPATH "\${ORIGIN}/")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_SOURCE_DIR}/bin/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE  ${PROJECT_SOURCE_DIR}/bin/Release)
add_executable(EjiaOptionServer.out ${CPP_FILES} ${SRC_FILES})
add_compile_options(-pthread -Wl -z -rpath='$ORIGIN')
target_link_libraries(EjiaOptionServer.out rt z dl jemalloc_pic curl crypto alibabacloud-oss-cpp-sdk
        protobuf-lite pthread breakpad_client)


