cmake_minimum_required(VERSION 3.10)
project(ZIP LANGUAGES CXX)

# Global C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Better IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

# Shared library
add_library(shared
    shared/src/print_utils.cpp
    shared/src/udp_socket.cpp
)
target_include_directories(shared PUBLIC shared/include)
target_link_libraries(shared PUBLIC
    $<$<PLATFORM_ID:Windows>:ws2_32>  # Winsock on Windows
)

# Executables (server, client)
foreach(t IN ITEMS server client)
    add_executable(${t}
        ${t}/main.cpp
        ${t}/src/${t}.cpp
    )
    target_include_directories(${t} PRIVATE ${t}/include)
    target_link_libraries(${t} PRIVATE shared Threads::Threads)
endforeach()

# Executables tests
add_executable(tests
    tests/main.cpp
    tests/src/subprocess.cpp
)
target_include_directories(tests PRIVATE tests/include)
target_link_libraries(tests PRIVATE shared Threads::Threads)
