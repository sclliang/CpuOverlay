if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.25)
    project(CpuOverlayPresentMonStandalone LANGUAGES CXX)
endif()

add_library(CpuOverlayPresentMon STATIC
    PresentMonRealtime.cpp
    PresentData/DebugStubs.cpp
    PresentData/GpuTrace.cpp
    PresentData/PresentMonTraceConsumer.cpp
    PresentData/TraceConsumer.cpp
    PresentData/TraceSession.cpp
)

target_include_directories(CpuOverlayPresentMon PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(CpuOverlayPresentMon PUBLIC cxx_std_20)
target_compile_definitions(CpuOverlayPresentMon PRIVATE
    UNICODE
    _UNICODE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_compile_options(CpuOverlayPresentMon PRIVATE
    /W3
    /EHsc
    /utf-8
    /permissive-
)
set_target_properties(CpuOverlayPresentMon PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
target_link_libraries(CpuOverlayPresentMon PUBLIC
    advapi32
    tdh
)

if(COMMAND winmonitor_flat_output)
    winmonitor_flat_output(CpuOverlayPresentMon)
endif()
