cmake_minimum_required(VERSION 3.15)
project(blackbox-server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Download Boost (headers + minimal build for system library)
FetchContent_Declare(
    boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.gz
)
set(BOOST_INCLUDE_LIBRARIES system)
set(BOOST_ENABLE_CMAKE ON)
FetchContent_MakeAvailable(boost)

# Download Abseil (header-only)
FetchContent_Declare(
    abseil
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 20240116.2
)
FetchContent_MakeAvailable(abseil)

# Check for NVML (optional)
find_path(NVML_INCLUDE_DIR nvml.h
    PATHS
    /usr/include
    /usr/local/include
    /opt/nvidia/include
)

if(NVML_INCLUDE_DIR)
    message(STATUS "Found NVML: ${NVML_INCLUDE_DIR}")
    add_definitions(-DNVML_AVAILABLE)
    include_directories(${NVML_INCLUDE_DIR})
    
    find_library(NVML_LIB nvidia-ml
        PATHS
        /usr/lib
        /usr/lib64
        /usr/local/lib
        /opt/nvidia/lib
    )
    
    if(NVML_LIB)
        message(STATUS "Found NVML library: ${NVML_LIB}")
    endif()
endif()

add_executable(blackbox-server src/main.cpp)

target_include_directories(blackbox-server PRIVATE
    ${boost_SOURCE_DIR}
    ${abseil_SOURCE_DIR}
)

target_link_libraries(blackbox-server
    Boost::system
    Boost::headers
    absl::strings
)

if(NVML_LIB)
    target_link_libraries(blackbox-server ${NVML_LIB})
endif()
