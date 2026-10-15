cmake_minimum_required(VERSION 3.24)
project(CpuOverlay VERSION 1.2.0.0 LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Build CpuOverlay with MSVC on Windows.")
endif()

add_executable(CpuOverlay WIN32
  src/main.cpp src/cpu_sampler.cpp src/overlay_renderer.cpp src/pawn_sensors.cpp src/startup.cpp
  resources/CpuOverlay.rc)
target_compile_features(CpuOverlay PRIVATE cxx_std_17)
target_include_directories(CpuOverlay PRIVATE "${CMAKE_SOURCE_DIR}/third_party/pawnio")
target_compile_definitions(CpuOverlay PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(CpuOverlay PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlay PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(CpuOverlay PRIVATE user32 gdi32 gdiplus shell32 advapi32 pdh powrprof shcore ole32 oleaut32 taskschd uuid)
target_link_options(CpuOverlay PRIVATE /MANIFEST:NO)
set_target_properties(CpuOverlay PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/package"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/package")
add_custom_command(TARGET CpuOverlay POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CpuOverlay>/bin/pawnio"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/IntelMSR.bin"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/IntelMSR.p"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/AMDFamily17.bin"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/AMDFamily17.p"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/COPYING"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/README.md"
    "$<TARGET_FILE_DIR:CpuOverlay>/bin/pawnio")

# Optional desktop integration fixture. Never distributed in package/.
add_executable(CpuOverlayProbe EXCLUDE_FROM_ALL WIN32 tests/ui_probe.cpp)
target_compile_features(CpuOverlayProbe PRIVATE cxx_std_17)
target_compile_definitions(CpuOverlayProbe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(CpuOverlayProbe PRIVATE /W4 /utf-8 /EHsc)
set_property(TARGET CpuOverlayProbe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(CpuOverlayProbe PRIVATE user32 gdi32)

# Hardware-independent checks for CPU selection, temperature conversion and power.
enable_testing()
add_executable(CpuOverlaySensorTests EXCLUDE_FROM_ALL tests/sensor_math_tests.cpp tests/sensor_tests.rc)
target_include_directories(CpuOverlaySensorTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(CpuOverlaySensorTests PRIVATE cxx_std_17)
target_compile_options(CpuOverlaySensorTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlaySensorTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME CpuOverlaySensorTests COMMAND CpuOverlaySensorTests)

configure_file(installer/payload.rc.in "${CMAKE_BINARY_DIR}/payload.rc.in" @ONLY)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/payload-$<CONFIG>.rc" INPUT "${CMAKE_BINARY_DIR}/payload.rc.in")
add_executable(CpuOverlaySetup WIN32 installer/setup.cpp "${CMAKE_BINARY_DIR}/payload-$<CONFIG>.rc")
add_dependencies(CpuOverlaySetup CpuOverlay)
target_compile_features(CpuOverlaySetup PRIVATE cxx_std_17)
target_compile_definitions(CpuOverlaySetup PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(CpuOverlaySetup PRIVATE /W4 /utf-8 /EHsc)
set_property(TARGET CpuOverlaySetup PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(CpuOverlaySetup PRIVATE user32 shell32 advapi32 ole32 comctl32 version uuid)
target_link_options(CpuOverlaySetup PRIVATE /MANIFEST:NO)
set_target_properties(CpuOverlaySetup PROPERTIES OUTPUT_NAME "CpuOverlay-Setup"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/package")
