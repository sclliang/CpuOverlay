cmake_minimum_required(VERSION 3.24)
project(CpuOverlay VERSION 1.0.0.0 LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Build CpuOverlay with MSVC on Windows.")
endif()

add_executable(CpuOverlay WIN32
  src/main.cpp src/cpu_sampler.cpp src/overlay_renderer.cpp
  resources/CpuOverlay.rc)
target_compile_features(CpuOverlay PRIVATE cxx_std_17)
target_compile_definitions(CpuOverlay PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(CpuOverlay PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlay PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(CpuOverlay PRIVATE user32 gdi32 gdiplus shell32 advapi32 pdh powrprof shcore)
target_link_options(CpuOverlay PRIVATE /MANIFEST:NO)
set_target_properties(CpuOverlay PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/package"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/package")

# Optional desktop integration fixture. Never distributed in package/.
add_executable(CpuOverlayProbe EXCLUDE_FROM_ALL WIN32 tests/ui_probe.cpp)
target_compile_features(CpuOverlayProbe PRIVATE cxx_std_17)
target_compile_definitions(CpuOverlayProbe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(CpuOverlayProbe PRIVATE /W4 /utf-8 /EHsc)
set_property(TARGET CpuOverlayProbe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(CpuOverlayProbe PRIVATE user32 gdi32)
