cmake_minimum_required(VERSION 3.10)
project(ZIP)

file(GLOB_RECURSE SRC_FILES src/*.cpp)
include_directories(include)

add_executable(ZIP main.cpp ${SRC_FILES})