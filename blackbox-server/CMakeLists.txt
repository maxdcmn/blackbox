cmake_minimum_required(VERSION 3.15)
project(blackbox-server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build for compatibility with older systems
# Link libstdc++ statically to avoid GLIBC version issues
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")

# Use old FindBoost module (more compatible)
set(Boost_NO_BOOST_CMAKE ON)

# Try to find system Boost
find_package(Boost QUIET COMPONENTS system)
if(Boost_FOUND AND Boost_SYSTEM_FOUND)
    message(STATUS "Using system Boost: ${Boost_VERSION}")
    set(USE_SYSTEM_BOOST TRUE)
else()
    # Check if Boost headers exist but library is missing
    if(EXISTS "/usr/include/boost")
        message(FATAL_ERROR "\n"
            "Boost headers found but boost_system library is missing.\n"
            "Please install: sudo apt install libboost-system-dev\n"
            "Or for Debian packaging, add it to Build-Depends in debian/control\n"
        )
    else()
        message(STATUS "System Boost not found, will download via FetchContent")
        set(USE_SYSTEM_BOOST FALSE)
        include(FetchContent)
        
        # Download Boost headers (full release with all headers)
        FetchContent_Declare(
            boost
            URL https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.gz
        )
        FetchContent_GetProperties(boost)
        if(NOT boost_POPULATED)
            FetchContent_Populate(boost)
            set(BOOST_BUILD_DIR ${CMAKE_BINARY_DIR}/boost_build)
            file(MAKE_DIRECTORY ${BOOST_BUILD_DIR})
            # Build only system library (required by asio)
            execute_process(
                COMMAND ./bootstrap.sh --with-libraries=system --prefix=${CMAKE_BINARY_DIR}/boost_install
                WORKING_DIRECTORY ${boost_SOURCE_DIR}
                RESULT_VARIABLE bootstrap_result
                OUTPUT_QUIET ERROR_QUIET
            )
            if(NOT bootstrap_result EQUAL 0)
                message(FATAL_ERROR "Boost bootstrap failed")
            endif()
            execute_process(
                COMMAND bash -c "nproc"
                OUTPUT_VARIABLE NPROC
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            execute_process(
                COMMAND ./b2 --with-system --prefix=${CMAKE_BINARY_DIR}/boost_install variant=release link=static threading=multi -j${NPROC}
                WORKING_DIRECTORY ${boost_SOURCE_DIR}
                RESULT_VARIABLE b2_result
            )
            if(NOT b2_result EQUAL 0)
                message(FATAL_ERROR "Boost b2 build failed")
            endif()
            execute_process(
                COMMAND ./b2 install
                WORKING_DIRECTORY ${boost_SOURCE_DIR}
                RESULT_VARIABLE install_result
            )
        endif()
    endif()
endif()

# Try to find system Abseil
find_package(absl QUIET)
if(absl_FOUND)
    message(STATUS "Using system Abseil")
    set(USE_SYSTEM_ABSEIL TRUE)
else()
    message(STATUS "System Abseil not found, will download via FetchContent")
    set(USE_SYSTEM_ABSEIL FALSE)
    if(NOT FetchContent_Declare)
        include(FetchContent)
    endif()
    
    # Download Abseil
    FetchContent_Declare(
        abseil
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG 20240116.2
    )
    FetchContent_MakeAvailable(abseil)
endif()

# Check for NVML (optional)
find_path(NVML_INCLUDE_DIR nvml.h
    PATHS
    /usr/include
    /usr/local/include
    /opt/nvidia/include
    /usr/include/nvidia
)

# Try multiple library names
find_library(NVML_LIB 
    NAMES nvidia-ml libnvidia-ml.so.1 libnvidia-ml.so
    PATHS
    /usr/lib
    /usr/lib64
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib
    /opt/nvidia/lib
    /usr/lib/nvidia
)

# Only enable NVML if both headers and library are found
if(NVML_INCLUDE_DIR AND NVML_LIB)
    message(STATUS "Found NVML: ${NVML_INCLUDE_DIR}")
    message(STATUS "Found NVML library: ${NVML_LIB}")
    add_definitions(-DNVML_AVAILABLE)
    include_directories(${NVML_INCLUDE_DIR})
else()
    if(NVML_INCLUDE_DIR)
        message(WARNING "NVML headers found but library not found. NVML features will be disabled.")
    endif()
    if(NVML_LIB)
        message(WARNING "NVML library found but headers not found. NVML features will be disabled.")
    endif()
    unset(NVML_INCLUDE_DIR)
    unset(NVML_LIB)
endif()

add_executable(blackbox-server
    src/infra/main.cpp
    src/infra/http_server.cpp
    src/services/nvml_utils.cpp
    src/services/vllm_client.cpp
    src/services/nsight_utils.cpp
    src/services/hf_deploy.cpp
    src/services/deploy_service.cpp
    src/services/model_manager.cpp
    src/services/spindown_service.cpp
    src/services/vram_tracker.cpp
    src/services/optimization_service.cpp
    src/services/aggregation_service.cpp
    src/utils/json_serializer.cpp
    src/utils/json_parser.cpp
    src/utils/env_utils.cpp
    src/utils/logger.cpp
)

target_include_directories(blackbox-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(Boost_FOUND AND Boost_SYSTEM_FOUND)
    # Use system Boost
    target_include_directories(blackbox-server PRIVATE
        ${Boost_INCLUDE_DIRS}
    )
    target_link_libraries(blackbox-server
        ${Boost_LIBRARIES}
    )
else()
    # Use downloaded Boost
    target_include_directories(blackbox-server PRIVATE
        ${boost_SOURCE_DIR}
    )
    target_link_directories(blackbox-server PRIVATE
        ${CMAKE_BINARY_DIR}/boost_install/lib
    )
    target_link_libraries(blackbox-server
        boost_system
    )
endif()

# Link Abseil (same for both system and downloaded)
target_link_libraries(blackbox-server
    absl::strings
    absl::str_format_internal
)

# Add nlohmann/json (header-only library)
if(NOT FetchContent_Declare)
    include(FetchContent)
endif()

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# Add yaml-cpp
find_package(yaml-cpp QUIET)
if(yaml-cpp_FOUND)
    message(STATUS "Using system yaml-cpp")
else()
    message(STATUS "System yaml-cpp not found, will download via FetchContent")
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
    )
    FetchContent_MakeAvailable(yaml-cpp)
endif()

target_link_libraries(blackbox-server
    nlohmann_json::nlohmann_json
    yaml-cpp
)

if(NVML_LIB)
    message(STATUS "Linking NVML library: ${NVML_LIB}")
    target_link_libraries(blackbox-server ${NVML_LIB})
else()
    message(STATUS "NVML library not found - NVML features will be disabled at runtime")
endif()

# Installation
install(TARGETS blackbox-server
    RUNTIME DESTINATION bin
)

# Install documentation
install(DIRECTORY docs/
    DESTINATION share/doc/blackbox-server
    FILES_MATCHING PATTERN "*.md"
)

# Install README
install(FILES README.md
    DESTINATION share/doc/blackbox-server
)
