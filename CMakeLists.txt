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

# Keep the selected platform explicit for both the SDK and its code-generation
# sub-builds, which otherwise may not inherit the root configure arguments.
if (NOT DEFINED GEODE_TARGET_PLATFORM OR GEODE_TARGET_PLATFORM STREQUAL "")
    # Android64 is the workflow's portable fallback; CI may override this per target.
    set(GEODE_TARGET_PLATFORM "Android64")
endif()
if (DEFINED GEODE_TARGET_PLATFORM)
    set(GEODE_CODEGEN_CMAKE_ARGS "${GEODE_CODEGEN_CMAKE_ARGS};-DGEODE_TARGET_PLATFORM=${GEODE_TARGET_PLATFORM}" CACHE STRING "" FORCE)
endif()

add_subdirectory(${GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})
setup_geode_mod(${PROJECT_NAME})
