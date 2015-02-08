cmake_minimum_required(VERSION 2.8)
project(Piphoned)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

########################################
# Version number

set(PIPHONED_VERSION_MAJOR 0)
set(PIPHONED_VERSION_MINOR 0)
set(PIPHONED_VERSION_PATCH 1)
set(PIPHONED_VERSION_POSTFIX "dev")

execute_process(COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE PIPHONED_VERSION_GIT
  OUTPUT_STRIP_TRAILING_WHITESPACE)

# If git was not available, unset empty variable
if (NOT(PIPHONED_VERSION_GIT))
  unset(PIPHONED_VERSION_GIT)
endif()

########################################
# Compilation options

set(PIPHONED_MAX_PROXY_NUM 256 CACHE STRING "Maximum number of proxies we can connect to simultaneously.")

########################################
# Extra flags

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=c99")
endif()

if (${CMAKE_BUILD_TYPE} MATCHES Debug)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -D_PIPHONED_DEBUG")
endif()

add_definitions("-D_POSIX_C_SOURCE=200809L")
add_definitions("-D_BSD_SOURCE=1")

########################################
# Dependencies

find_package(PkgConfig REQUIRED)
find_package(WiringPi REQUIRED)
pkg_check_modules(Linphone REQUIRED linphone)

string(REPLACE ";" " " Linphone_CFLAGS "${Linphone_CFLAGS}") # WTF? http://www.cmake.org/Bug/view.php?id=12317 is marked as WONTFIX?
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${Linphone_CFLAGS}")
include_directories(${Linphone_INCLUDE_DIRS} ${WiringPi_INCLUDE_DIR})

########################################
# Source files

file(GLOB_RECURSE piphoned_sources
  "src/*.c"
  "src/*.h")

file(GLOB_RECURSE piphoned_soundcards_sources
  "piphoned-soundcards-src/*.c"
  "piphoned-soundcards-src/*.h")

configure_file(${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_BINARY_DIR}/config.h)
include_directories("${CMAKE_SOURCE_DIR}/src" ${CMAKE_BINARY_DIR})

########################################
# Targets

add_executable(piphoned ${piphoned_sources})
add_executable(piphoned-soundcards ${piphoned_soundcards_sources})
target_link_libraries(piphoned
  ${Linphone_LIBRARIES}
  ${WiringPi_LIBRARIES})
target_link_libraries(piphoned-soundcards
  ${Linphone_LIBRARIES})

########################################
# Installation information

install(TARGETS piphoned piphoned-soundcards
  DESTINATION sbin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/
  DESTINATION share/piphoned)
