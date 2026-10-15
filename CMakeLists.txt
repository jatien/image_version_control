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

# Third-party single-header libraries (stb), compiled without our strict
# warning flags since we don't maintain that code.
add_library(ivc_stb src/stb_impl.cpp)
target_include_directories(ivc_stb PUBLIC third_party)

# Core library: image loading, hashing and comparison, shared by every tool.
add_library(ivc_core
  src/image.cpp
  src/sha256.cpp
  src/image_hash.cpp
  src/gray.cpp
  src/orientation.cpp
  src/compare.cpp
  src/convolution.cpp
  src/ssim.cpp
  src/diff.cpp
  src/repository.cpp
)
target_include_directories(ivc_core PUBLIC src)
target_link_libraries(ivc_core PUBLIC ivc_stb)
target_compile_options(ivc_core PRIVATE ${IVC_WARNINGS})

# Command-line tools. ivc is the version control; the others are for
# inspecting single images.
add_executable(ivc src/ivc_main.cpp)
target_link_libraries(ivc PRIVATE ivc_core)
target_compile_options(ivc PRIVATE ${IVC_WARNINGS})

add_executable(ivc_pixels src/main.cpp)
target_link_libraries(ivc_pixels PRIVATE ivc_core)
target_compile_options(ivc_pixels PRIVATE ${IVC_WARNINGS})

add_executable(ivc_hash src/hash_main.cpp)
target_link_libraries(ivc_hash PRIVATE ivc_core)
target_compile_options(ivc_hash PRIVATE ${IVC_WARNINGS})

add_executable(ivc_compare src/compare_main.cpp)
target_link_libraries(ivc_compare PRIVATE ivc_core)
target_compile_options(ivc_compare PRIVATE ${IVC_WARNINGS})

add_executable(ivc_diff src/diff_main.cpp)
target_link_libraries(ivc_diff PRIVATE ivc_core)
target_compile_options(ivc_diff PRIVATE ${IVC_WARNINGS})

# Tests:  ctest --test-dir build --output-on-failure
enable_testing()
add_executable(test_hash tests/test_hash.cpp)
target_link_libraries(test_hash PRIVATE ivc_core)
target_compile_options(test_hash PRIVATE ${IVC_WARNINGS})
target_compile_definitions(test_hash PRIVATE SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples")
add_test(NAME hash COMMAND test_hash)

add_executable(test_compare tests/test_compare.cpp)
target_link_libraries(test_compare PRIVATE ivc_core)
target_compile_options(test_compare PRIVATE ${IVC_WARNINGS})
target_compile_definitions(test_compare PRIVATE SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples")
add_test(NAME compare COMMAND test_compare)

add_executable(test_ssim tests/test_ssim.cpp)
target_link_libraries(test_ssim PRIVATE ivc_core)
target_compile_options(test_ssim PRIVATE ${IVC_WARNINGS})
target_compile_definitions(test_ssim PRIVATE
  SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples"
  BINARY_DIR="${CMAKE_BINARY_DIR}")
add_test(NAME ssim COMMAND test_ssim)

add_executable(test_repository tests/test_repository.cpp)
target_link_libraries(test_repository PRIVATE ivc_core)
target_compile_options(test_repository PRIVATE ${IVC_WARNINGS})
target_compile_definitions(test_repository PRIVATE
  SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples"
  BINARY_DIR="${CMAKE_BINARY_DIR}")
add_test(NAME repository COMMAND test_repository)
