cmake_minimum_required(VERSION 3.10)
project(ZIP)

# Client
file(GLOB_RECURSE CLIENT_SRC client/src/*.cpp)
include_directories(client/include)
add_executable(client ${CLIENT_SRC})

# Server
file(GLOB_RECURSE SERVER_SRC server/src/*.cpp)
include_directories(server/include)
add_executable(server ${SERVER_SRC})