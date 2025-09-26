cmake_minimum_required(VERSION 3.10)
project(ZIP)

# Shared
include_directories(shared/include)

# Client
file(GLOB_RECURSE CLIENT_SRC client/src/*.cpp)
file(GLOB CLIENT_MAIN client/main.cpp)
file(GLOB_RECURSE SHARED_SRC shared/src/*.cpp)
include_directories(client/include)
add_executable(client ${CLIENT_SRC} ${CLIENT_MAIN} ${SHARED_SRC})

# Server
file(GLOB_RECURSE SERVER_SRC server/src/*.cpp)
file(GLOB SERVER_MAIN server/main.cpp)
include_directories(server/include)
add_executable(server ${SERVER_SRC} ${SERVER_MAIN} ${SHARED_SRC})