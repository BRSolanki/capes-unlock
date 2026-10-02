cmake_minimum_required(VERSION 3.22.1)
project(capes LANGUAGES CXX)

# The output library is named "capes" → produces libcapes.so
add_library(capes SHARED
    src/libcapes.cpp
)

target_compile_features(capes PRIVATE cxx_std_20)
target_compile_options(capes PRIVATE -fvisibility=hidden -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti)
target_link_options(capes PRIVATE -Wl,--gc-sections)

# Generated headers go into generated/ at build time
target_include_directories(capes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

target_link_libraries(capes PRIVATE log)
