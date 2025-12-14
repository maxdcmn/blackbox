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

if(NVML_LIB)
    target_link_libraries(blackbox-server ${NVML_LIB})
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
