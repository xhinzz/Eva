cmake_minimum_required(VERSION 3.16)
project(Eva LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(eva_core
  src/population.cpp
  src/projection.cpp
  src/eva.cpp)
target_include_directories(eva_core PUBLIC src)

add_executable(eva src/main.cpp)
target_link_libraries(eva PRIVATE eva_core)

# Eva 2: córtex de colunas
add_executable(eva2 v2/main2.cpp v2/eva2.cpp v2/temporal_memory.cpp v2/vision.cpp)
target_include_directories(eva2 PRIVATE v2 src)
