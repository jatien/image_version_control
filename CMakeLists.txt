cmake_minimum_required(VERSION 3.14)
project(image_version_control CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  set(IVC_WARNINGS /W4)
else()
  set(IVC_WARNINGS -Wall -Wextra)
endif()

# Core library: image loading and hashing, shared by every tool.
add_library(ivc_core
  src/image.cpp
  src/stb_impl.cpp
  src/sha256.cpp
  src/image_hash.cpp
  src/similarity_hash.cpp
  src/orientation.cpp
  src/tile_hash.cpp
  src/compare.cpp
)
target_include_directories(ivc_core PUBLIC src third_party)
target_compile_options(ivc_core PRIVATE ${IVC_WARNINGS})

# Command-line tools.
add_executable(ivc_pixels src/main.cpp)
target_link_libraries(ivc_pixels PRIVATE ivc_core)
target_compile_options(ivc_pixels PRIVATE ${IVC_WARNINGS})

add_executable(ivc_hash src/hash_main.cpp)
target_link_libraries(ivc_hash PRIVATE ivc_core)
target_compile_options(ivc_hash PRIVATE ${IVC_WARNINGS})

add_executable(ivc_compare src/compare_main.cpp)
target_link_libraries(ivc_compare PRIVATE ivc_core)
target_compile_options(ivc_compare PRIVATE ${IVC_WARNINGS})

# Tests:  ctest --test-dir build --output-on-failure
enable_testing()
add_executable(test_hash tests/test_hash.cpp)
target_link_libraries(test_hash PRIVATE ivc_core)
target_compile_options(test_hash PRIVATE ${IVC_WARNINGS})
target_compile_definitions(test_hash PRIVATE SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples")
add_test(NAME hash COMMAND test_hash)

add_executable(test_similarity tests/test_similarity.cpp)
target_link_libraries(test_similarity PRIVATE ivc_core)
target_compile_options(test_similarity PRIVATE ${IVC_WARNINGS})
target_compile_definitions(test_similarity PRIVATE SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples")
add_test(NAME similarity COMMAND test_similarity)

add_executable(test_compare tests/test_compare.cpp)
target_link_libraries(test_compare PRIVATE ivc_core)
target_compile_options(test_compare PRIVATE ${IVC_WARNINGS})
target_compile_definitions(test_compare PRIVATE SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples")
add_test(NAME compare COMMAND test_compare)
