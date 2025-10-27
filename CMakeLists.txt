cmake_minimum_required(VERSION 3.16)
project(cache_lab LANGUAGES CXX)

# C++17 is enough for chrono/optional and containers
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Helpful warnings + optimization (you can switch to -O0 for debugging)
add_compile_options(-Wall -Wextra -Wpedantic -O2)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(app
    src/main.cpp
    src/LRU.cpp
    src/LFU.cpp
)
