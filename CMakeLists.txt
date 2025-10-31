cmake_minimum_required(VERSION 3.16)
project(cache_lab_full LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic -O2)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(app
    src/main.cpp
    src/Metrics.cpp
    src/LRU.cpp
    src/LFU.cpp
)
