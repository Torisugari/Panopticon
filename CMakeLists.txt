project(GoldenPanopticon)
cmake_minimum_required(VERSION 2.8)

include(FindPkgConfig)

pkg_check_modules(DBUS dbus-1)
INCLUDE_DIRECTORIES(${DBUS_INCLUDE_DIRS})
LINK_DIRECTORIES(${DBUS_LIBRARY_DIRS})
LINK_LIBRARIES(${DBUS_LIBRARIES})

set(CMAKE_C_FLAGS "-Wall")
set(CMAKE_CXX_FLAGS "-Wall")

ADD_CUSTOM_TARGET(debug
                    ${CMAKE_COMMAND} 
                      -DCMAKE_BUILD_TYPE:STRING=Debug ${PROJECT_SOURCE_DIR})
ADD_CUSTOM_TARGET(release
                    ${CMAKE_COMMAND}
                      -DCMAKE_BUILD_TYPE:STRING=Release ${PROJECT_SOURCE_DIR})

set(CMAKE_C_FLAGS_DEBUG "-O0 -g -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

add_executable(greatfd src/greatfd.cpp)
