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
find_package(Threads REQUIRED)
target_link_libraries(eva2 PRIVATE Threads::Threads)

# Medições da Eva 2 (v2/tests)
set(EVA2_CORE v2/eva2.cpp v2/temporal_memory.cpp v2/vision.cpp)
foreach(t bench ground grammar topic scenes real)
  add_executable(eva2_${t} v2/tests/${t}.cpp ${EVA2_CORE})
  target_include_directories(eva2_${t} PRIVATE v2 v2/tests src)
  target_link_libraries(eva2_${t} PRIVATE Threads::Threads)
endforeach()
add_executable(eva2_vision_test v2/tests/vision_test.cpp v2/vision.cpp)
add_executable(eva2_eyes_features v2/tests/eyes_features.cpp v2/vision.cpp)
target_include_directories(eva2_eyes_features PRIVATE v2)
target_include_directories(eva2_vision_test PRIVATE v2)
add_executable(eva2_categories_test v2/tests/categories_test.cpp)
target_include_directories(eva2_categories_test PRIVATE v2)
