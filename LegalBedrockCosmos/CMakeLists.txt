cmake_minimum_required(VERSION 3.22.1)
project(libcapes_reconstructed LANGUAGES CXX)

add_library(libcapes_reconstructed SHARED
    src/libcapes.cpp
)

target_compile_features(libcapes_reconstructed PRIVATE cxx_std_20)
target_compile_options(libcapes_reconstructed PRIVATE -fvisibility=hidden -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti)
target_link_options(libcapes_reconstructed PRIVATE -Wl,--gc-sections)

target_include_directories(libcapes_reconstructed PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(libcapes_reconstructed PRIVATE log)
