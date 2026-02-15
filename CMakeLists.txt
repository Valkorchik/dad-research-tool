cmake_minimum_required(VERSION 3.25)
project(dad-research-tool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg integration
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# Dependencies
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ImGui sources (vendored)
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/external/imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# Core sources
set(CORE_SOURCES
    src/core/process.cpp
    src/core/pattern_scanner.cpp
    src/core/config.cpp
    src/core/integrity.cpp
)

# SDK sources
set(SDK_SOURCES
    src/sdk/gnames.cpp
    src/sdk/gworld.cpp
)

# Game sources
set(GAME_SOURCES
    src/game/actor_manager.cpp
    src/game/item_database.cpp
)

# Render sources
set(RENDER_SOURCES
    src/render/overlay.cpp
    src/render/imgui_manager.cpp
    src/render/drawing.cpp
    src/render/world_to_screen.cpp
    src/render/menu.cpp
)

add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    ${CORE_SOURCES}
    ${SDK_SOURCES}
    ${GAME_SOURCES}
    ${RENDER_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    dwmapi.lib
)

# Copy config files to build output
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/config" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config"
)

# =========================================================================
# Launcher / Dashboard executable
# =========================================================================
set(LAUNCHER_SOURCES
    src/launcher/launcher_main.cpp
    src/launcher/dashboard_window.cpp
    src/launcher/util/process_launcher.cpp
    src/launcher/util/dll_injector.cpp
    src/launcher/panels/tool_launcher_panel.cpp
    src/launcher/panels/log_panel.cpp
    src/launcher/panels/game_status_panel.cpp
    src/launcher/panels/offset_panel.cpp
    src/launcher/panels/settings_panel.cpp
    src/launcher/panels/injector_panel.cpp
)

add_executable(dad-launcher WIN32
    ${LAUNCHER_SOURCES}
    ${CORE_SOURCES}
    src/core/manual_mapper.cpp
    src/render/imgui_manager.cpp
    ${IMGUI_SOURCES}
    resources/launcher.rc
)

target_include_directories(dad-launcher PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(dad-launcher PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    dwmapi.lib
    comdlg32.lib
)

# Copy config files for launcher too
add_custom_command(TARGET dad-launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/config" "$<TARGET_FILE_DIR:dad-launcher>/config"
)

# Copy resources (icon) for launcher
add_custom_command(TARGET dad-launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/resources" "$<TARGET_FILE_DIR:dad-launcher>/../../resources"
)

# =========================================================================
# CLI DLL injector utility (with manual mapping support)
# =========================================================================
add_executable(inject
    tools/inject.cpp
    src/core/manual_mapper.cpp
)
target_include_directories(inject PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(inject PRIVATE kernel32.lib)

# =========================================================================
# version.dll proxy — DLL search order hijacking
# Bypasses kernel-level anti-cheat by running inside the game process.
# Place in game directory to auto-load before anti-cheat initializes.
# =========================================================================
add_library(version-proxy SHARED
    src/proxy/version_proxy.cpp
    src/proxy/version.def
)
set_target_properties(version-proxy PROPERTIES
    OUTPUT_NAME "version"
    PREFIX ""
    SUFFIX ".dll"
)
target_include_directories(version-proxy PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(version-proxy PRIVATE kernel32.lib version.lib)
