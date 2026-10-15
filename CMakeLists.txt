cmake_minimum_required(VERSION 3.14)
project(image_version_control CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Image loading library, reused by every tool we add later (hashing, diff...).
add_library(ivc_image src/image.cpp src/stb_impl.cpp)
target_include_directories(ivc_image PUBLIC src third_party)

if(MSVC)
  target_compile_options(ivc_image PRIVATE /W4)
else()
  target_compile_options(ivc_image PRIVATE -Wall -Wextra)
endif()

add_executable(ivc_pixels src/main.cpp)
target_link_libraries(ivc_pixels PRIVATE ivc_image)
