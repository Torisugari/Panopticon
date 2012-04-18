project(GoldenPanopticon)
cmake_minimum_required(VERSION 2.8)

include(FindPkgConfig)

# Path to <glib.h>
pkg_check_modules(GLIB2 glib-2.0)
INCLUDE_DIRECTORIES(${GLIB2_INCLUDE_DIRS})
LINK_DIRECTORIES(${GLIB2_LIBRARY_DIRS})
LINK_LIBRARIES(${GLIB2_LIBRARIES})

# Path to <atspi/atspi.h> and <libatspi.so>
pkg_check_modules(ATSPI2 atspi-2)
INCLUDE_DIRECTORIES(${ATSPI2_INCLUDE_DIRS})
LINK_DIRECTORIES(${ATSPI2_LIBRARY_DIRS})
LINK_LIBRARIES(${ATSPI2_LIBRARIES})

set(CMAKE_C_FLAGS "-Wall -O2")
set(CMAKE_CXX_FLAGS "-Wall -O2")
add_executable(greatfd src/main.cpp)
