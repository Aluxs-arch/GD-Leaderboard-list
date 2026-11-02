cmake_minimum_required(VERSION 3.21)
project(GDLeaderboardList VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (DEFINED ENV{GEODE_SDK} AND NOT DEFINED GEODE_SDK)
    set(GEODE_SDK "$ENV{GEODE_SDK}")
endif()
if (NOT GEODE_SDK)
    message(FATAL_ERROR "Geode SDK not found. Set GEODE_SDK to your Geode SDK directory.")
endif()

# Geode SDK's Android post-build strip command uses legacy add_custom_command
# arguments rejected by newer CMake versions. Preserve compatibility until the
# SDK updates that command.
if (POLICY CMP0175)
    cmake_policy(SET CMP0175 OLD)
endif()
set(CMAKE_POLICY_DEFAULT_CMP0175 OLD)

add_subdirectory(${GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})
setup_geode_mod(${PROJECT_NAME})
