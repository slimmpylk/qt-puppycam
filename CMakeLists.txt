cmake_minimum_required(VERSION 3.22)
project(qt_puppycam VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_executable(puppycam
  src/main.cpp
)

target_link_libraries(puppycam PRIVATE Qt6::Core Qt6::Network)
target_compile_options(puppycam PRIVATE -Wall -Wextra -Wpedantic)
