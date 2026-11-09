cmake_minimum_required(VERSION 3.24)
project(CpuOverlay VERSION 1.12.0.0 LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Build CpuOverlay with MSVC on Windows.")
endif()

add_subdirectory(third_party/presentmon)

# All game overlay code is shipped with this product; no RTSS or Vulkan SDK is
# required on the target computer. Vulkan calls are dispatched through its loader.
add_library(CpuOverlayHookLibrary STATIC
  third_party/game_overlay/minhook/src/buffer.c
  third_party/game_overlay/minhook/src/hook.c
  third_party/game_overlay/minhook/src/trampoline.c
  third_party/game_overlay/minhook/src/hde/hde64.c)
target_include_directories(CpuOverlayHookLibrary PUBLIC third_party/game_overlay/minhook/include)
set_property(TARGET CpuOverlayHookLibrary PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(CpuOverlayGame SHARED
  src/game_overlay_runtime.cpp src/game_overlay_hooks.cpp src/game_overlay_d3d11.cpp
  src/game_overlay_d3d12.cpp src/game_overlay_opengl.cpp
  src/game_overlay_vulkan_layer.cpp src/game_overlay_vulkan_renderer.cpp resources/GameOverlay.rc)
target_compile_features(CpuOverlayGame PRIVATE cxx_std_17)
target_compile_definitions(CpuOverlayGame PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0A00 VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR)
target_include_directories(CpuOverlayGame PRIVATE third_party/game_overlay/vulkan/include third_party/game_overlay)
target_compile_options(CpuOverlayGame PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlayGame PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(CpuOverlayGame PRIVATE CpuOverlayHookLibrary d3d11 d3d12 dxgi d3dcompiler opengl32 user32 gdi32)
target_link_options(CpuOverlayGame PRIVATE /EXPORT:vkNegotiateLoaderLayerInterfaceVersion)
set_target_properties(CpuOverlayGame PROPERTIES OUTPUT_NAME "CpuOverlayGame64"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/package/bin/game"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/package/bin/game")
file(SHA256 "${CMAKE_SOURCE_DIR}/third_party/librehardwaremonitor/LhmCpuSensorBridge.dll" GPU_BRIDGE_SHA256)
if(NOT GPU_BRIDGE_SHA256 STREQUAL "696d0c1a178013d88450093e61648c2ff399e4d0941f9b97b18165ea9ad35e61")
  message(FATAL_ERROR "The GPU sensor bridge does not match the pinned WinMonitor build.")
endif()

add_executable(CpuOverlay WIN32
  src/main.cpp src/cpu_sampler.cpp src/overlay_renderer.cpp src/pawn_sensors.cpp src/startup.cpp
  src/overlay_settings.cpp src/settings_dialog.cpp src/peripheral_battery.cpp src/pump_display.cpp
  src/gpu_sampler.cpp src/frame_rate_monitor.cpp src/frame_rate_estimator.cpp src/overlay_zorder.cpp src/game_overlay_host.cpp
  resources/CpuOverlay.rc)
target_compile_features(CpuOverlay PRIVATE cxx_std_17)
target_include_directories(CpuOverlay PRIVATE "${CMAKE_SOURCE_DIR}/third_party/pawnio")
target_compile_definitions(CpuOverlay PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(CpuOverlay PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlay PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(CpuOverlay PRIVATE CpuOverlayPresentMon dxgi dwmapi user32 gdi32 gdiplus shell32 advapi32 pdh powrprof shcore ole32 oleaut32 taskschd uuid comctl32 comdlg32 hid setupapi)
target_link_options(CpuOverlay PRIVATE /MANIFEST:NO)
set_target_properties(CpuOverlay PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/package"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/package")
add_custom_target(CpuOverlayModules
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CpuOverlay>/bin/pawnio"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/IntelMSR.bin"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/IntelMSR.p"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/AMDFamily17.bin"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/AMDFamily17.p"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/AMDFamily0F.bin"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/AMDFamily0F.p"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/AMDFamily10.bin"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/AMDFamily10.p"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/COPYING"
    "${CMAKE_SOURCE_DIR}/third_party/pawnio/README.md"
    "$<TARGET_FILE_DIR:CpuOverlay>/bin/pawnio")
add_dependencies(CpuOverlay CpuOverlayModules)
add_custom_target(CpuOverlayGpuModules
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CpuOverlay>/bin/gpu"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/third_party/librehardwaremonitor/LhmCpuSensorBridge.dll"
    "${CMAKE_SOURCE_DIR}/third_party/librehardwaremonitor/LICENSE-MPL-2.0"
    "${CMAKE_SOURCE_DIR}/third_party/librehardwaremonitor/LICENSE-PawnIO-Modules-LGPL-2.1"
    "${CMAKE_SOURCE_DIR}/third_party/librehardwaremonitor/README.md"
    "$<TARGET_FILE_DIR:CpuOverlay>/bin/gpu"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CpuOverlay>/bin/presentmon"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/third_party/presentmon/LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/third_party/presentmon/README.md"
    "$<TARGET_FILE_DIR:CpuOverlay>/bin/presentmon"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CpuOverlay>/bin/voltide"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/third_party/voltide/LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/third_party/voltide/README.md"
    "$<TARGET_FILE_DIR:CpuOverlay>/bin/voltide")
add_dependencies(CpuOverlay CpuOverlayGpuModules)
add_dependencies(CpuOverlay CpuOverlayGame)
add_custom_target(CpuOverlayGameFiles
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CpuOverlay>/bin/game"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/resources/CpuOverlayGame64.json"
    "$<TARGET_FILE_DIR:CpuOverlay>/bin/game"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/third_party/game_overlay/minhook/LICENSE.txt"
    "$<TARGET_FILE_DIR:CpuOverlay>/bin/game/LICENSE-MinHook.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/third_party/game_overlay/LICENSE-imgui.txt"
    "$<TARGET_FILE_DIR:CpuOverlay>/bin/game/LICENSE-imgui.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/third_party/game_overlay/LICENSE-Vulkan-Headers.txt"
    "$<TARGET_FILE_DIR:CpuOverlay>/bin/game/LICENSE-Vulkan-Headers.txt")
add_dependencies(CpuOverlay CpuOverlayGameFiles)

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

add_executable(CpuOverlaySettingsTests EXCLUDE_FROM_ALL tests/settings_tests.cpp tests/settings_tests.rc
  src/overlay_settings.cpp src/overlay_renderer.cpp src/settings_dialog.cpp)
target_include_directories(CpuOverlaySettingsTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(CpuOverlaySettingsTests PRIVATE cxx_std_17)
target_compile_definitions(CpuOverlaySettingsTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(CpuOverlaySettingsTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlaySettingsTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(CpuOverlaySettingsTests PRIVATE user32 gdi32 gdiplus advapi32 comctl32 comdlg32)
add_test(NAME CpuOverlaySettingsTests COMMAND CpuOverlaySettingsTests)
set_tests_properties(CpuOverlaySettingsTests PROPERTIES TIMEOUT 30)

add_executable(CpuOverlayBatteryTests EXCLUDE_FROM_ALL tests/battery_tests.cpp)
target_include_directories(CpuOverlayBatteryTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(CpuOverlayBatteryTests PRIVATE cxx_std_17)
target_compile_options(CpuOverlayBatteryTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlayBatteryTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME CpuOverlayBatteryTests COMMAND CpuOverlayBatteryTests)

add_executable(CpuOverlayVendorBatteryTests EXCLUDE_FROM_ALL tests/vendor_battery_tests.cpp)
target_include_directories(CpuOverlayVendorBatteryTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(CpuOverlayVendorBatteryTests PRIVATE cxx_std_17)
target_compile_options(CpuOverlayVendorBatteryTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlayVendorBatteryTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME CpuOverlayVendorBatteryTests COMMAND CpuOverlayVendorBatteryTests)

add_executable(CpuOverlayPumpTests EXCLUDE_FROM_ALL tests/pump_tests.cpp)
target_include_directories(CpuOverlayPumpTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(CpuOverlayPumpTests PRIVATE cxx_std_17)
target_compile_options(CpuOverlayPumpTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlayPumpTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME CpuOverlayPumpTests COMMAND CpuOverlayPumpTests)

add_executable(CpuOverlayFrameRateTests EXCLUDE_FROM_ALL tests/frame_rate_tests.cpp src/frame_rate_estimator.cpp)
target_include_directories(CpuOverlayFrameRateTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(CpuOverlayFrameRateTests PRIVATE cxx_std_17)
target_compile_options(CpuOverlayFrameRateTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlayFrameRateTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME CpuOverlayFrameRateTests COMMAND CpuOverlayFrameRateTests)

add_executable(CpuOverlayGpuTests EXCLUDE_FROM_ALL tests/gpu_tests.cpp)
target_include_directories(CpuOverlayGpuTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(CpuOverlayGpuTests PRIVATE cxx_std_17)
target_compile_options(CpuOverlayGpuTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlayGpuTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME CpuOverlayGpuTests COMMAND CpuOverlayGpuTests)

add_executable(CpuOverlayGraphicsProbe EXCLUDE_FROM_ALL tests/graphics_probe.cpp src/frame_rate_monitor.cpp src/frame_rate_estimator.cpp)
target_include_directories(CpuOverlayGraphicsProbe PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(CpuOverlayGraphicsProbe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(CpuOverlayGraphicsProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(CpuOverlayGraphicsProbe PRIVATE CpuOverlayPresentMon d3d11 dxgi ole32 user32)
set_property(TARGET CpuOverlayGraphicsProbe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(CpuOverlayTopmostProbe EXCLUDE_FROM_ALL tests/topmost_probe.cpp src/overlay_settings.cpp)
target_include_directories(CpuOverlayTopmostProbe PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(CpuOverlayTopmostProbe PRIVATE cxx_std_17)
target_compile_definitions(CpuOverlayTopmostProbe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(CpuOverlayTopmostProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(CpuOverlayTopmostProbe PRIVATE user32 advapi32)
set_property(TARGET CpuOverlayTopmostProbe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(CpuOverlayGameProbe EXCLUDE_FROM_ALL tests/game_overlay_probe.cpp tests/game_overlay_vulkan_probe.cpp
  src/game_overlay_host.cpp resources/GameOverlayProbe.rc)
target_compile_features(CpuOverlayGameProbe PRIVATE cxx_std_17)
target_include_directories(CpuOverlayGameProbe PRIVATE src third_party/game_overlay/vulkan/include)
target_compile_definitions(CpuOverlayGameProbe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0A00 VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR)
target_compile_options(CpuOverlayGameProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_property(TARGET CpuOverlayGameProbe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(CpuOverlayGameProbe PRIVATE user32 gdi32 gdiplus advapi32 d3d11 d3d12 dxgi opengl32)
add_dependencies(CpuOverlayGameProbe CpuOverlayGame CpuOverlayGameFiles)
add_custom_command(TARGET CpuOverlayGameProbe POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CpuOverlayGameProbe>/bin/game"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:CpuOverlayGame>" "$<TARGET_FILE_DIR:CpuOverlayGameProbe>/bin/game/CpuOverlayGame64.dll")

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
