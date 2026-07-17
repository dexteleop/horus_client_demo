cmake_minimum_required(VERSION 3.22.1)
project(horus_jni)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 预编译的 talite_xrstream.so（交付物，目前仅 arm64-v8a）作为 IMPORTED 库。
add_library(talite_xrstream SHARED IMPORTED)
set_target_properties(talite_xrstream PROPERTIES
        IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libtalite_xrstream.so)

# JNI 桥接库，通过 talite_xrstream.h 调用 C API。
add_library(horus_jni SHARED
        jni.cpp)

target_include_directories(horus_jni PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(horus_jni
        talite_xrstream
        log)
