cmake_minimum_required(VERSION 3.20)
project(pointblank VERSION 2.0.0 LANGUAGES CXX)

# ============================================================================
# Project Configuration
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Performance Optimization Flags
# ============================================================================

# Base compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -Werror=return-type
        -Werror=switch
    )
endif()

# Release optimization flags for maximum performance
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -mtune=native")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto -ffat-lto-objects")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fomit-frame-pointer")
# Note: Exceptions and RTTI are required by the codebase (try/catch blocks, std::variant)

# Link-time optimization for release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_AR "gcc-ar")
    set(CMAKE_NM "gcc-nm")
    set(CMAKE_RANLIB "gcc-ranlib")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s -Wl,--gc-sections -Wl,--as-needed -Wl,-z,relro,-z,now -Wl,--exclude-libs=ALL")
endif()

# Debug flags with sanitizers
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -fsanitize=address,undefined")

# ============================================================================
# Find Required Packages
# ============================================================================

find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(XFT REQUIRED xft)
pkg_check_modules(XRENDER REQUIRED xrender)
pkg_check_modules(XRANDR REQUIRED xrandr)
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(XCB REQUIRED xcb)
pkg_check_modules(XCB_AUX REQUIRED xcb-aux)
pkg_check_modules(XCB_EWMH REQUIRED xcb-ewmh)
pkg_check_modules(XCB_ICCCM REQUIRED xcb-icccm)
pkg_check_modules(X11_XCB REQUIRED x11-xcb)

# ============================================================================
# Source Files (Organized by Module)
# ============================================================================

# Core sources
set(CORE_SOURCES
    src/main.cpp
    src/core/WindowManager.cpp
    src/core/XServerManager.cpp
    src/core/SessionManager.cpp
    src/core/Toaster.cpp
)

# Configuration system
set(CONFIG_SOURCES
    src/config/ConfigParser.cpp
    src/config/LayoutConfigParser.cpp
    src/config/StartupApps.cpp
    src/config/ConfigWatcher.cpp
)

# Window management
set(WINDOW_SOURCES
    src/window/FloatingWindowManager.cpp
    src/window/KeybindManager.cpp
    src/window/PreselectionWindow.cpp
    src/window/ScratchpadManager.cpp
    src/window/SizeConstraints.cpp
    src/window/WindowSwallower.cpp
)

# IPC
set(IPC_SOURCES
    src/ipc/IPCServer.cpp
)

# Display and EWMH
set(DISPLAY_SOURCES
    src/display/EWMHManager.cpp
    src/display/MonitorManager.cpp
    src/display/SyncManager.cpp
)

# Performance optimization
set(PERFORMANCE_SOURCES
    src/performance/PerformanceTuner.cpp
    src/performance/RenderPipeline.cpp
)

# Layout system
set(LAYOUT_SOURCES
    src/layout/LayoutEngine.cpp
    src/layout/LayoutProvider.cpp
)

# Utilities
set(UTILS_SOURCES
    src/utils/GapConfig.cpp
    src/utils/SpatialGrid.cpp
)

# Combine all sources
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${CONFIG_SOURCES}
    ${WINDOW_SOURCES}
    ${IPC_SOURCES}
    ${DISPLAY_SOURCES}
    ${PERFORMANCE_SOURCES}
    ${LAYOUT_SOURCES}
    ${UTILS_SOURCES}
)

# ============================================================================
# Main Executable
# ============================================================================

add_executable(pointblank ${ALL_SOURCES})

# Include directories
target_include_directories(pointblank PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${X11_INCLUDE_DIR}
    ${CAIRO_INCLUDE_DIRS}
    ${XFT_INCLUDE_DIRS}
    ${XRENDER_INCLUDE_DIRS}
    ${XRANDR_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${XCB_EWMH_INCLUDE_DIRS}
    ${XCB_ICCCM_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(pointblank PRIVATE
    ${X11_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${XFT_LIBRARIES}
    ${XRENDER_LIBRARIES}
    ${XRANDR_LIBRARIES}
    ${GIO_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${XCB_LIBRARIES}
    ${XCB_AUX_LIBRARIES}
    ${XCB_EWMH_LIBRARIES}
    ${XCB_ICCCM_LIBRARIES}
    ${X11_XCB_LIBRARIES}
    ${CMAKE_DL_LIBS}
    Threads::Threads
    rt  # Real-time library for shm_open, etc.
)

# ============================================================================
# Compiler Definitions
# ============================================================================

target_compile_definitions(pointblank PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    PB_API_VERSION_MAJOR=2
    PB_API_VERSION_MINOR=0
    PB_API_VERSION_PATCH=0
)

# ============================================================================
# Installation
# ============================================================================

install(TARGETS pointblank
    RUNTIME DESTINATION bin
)

# Desktop file for display managers
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/contrib/pointblank.desktop")
    install(FILES
        contrib/pointblank.desktop
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/xsessions/
    )
endif()

# Default config file
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config/pointblank.wmi")
    install(FILES
        config/pointblank.wmi
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/pointblank/
    )
endif()

# Install extension development headers
install(DIRECTORY include/pointblank
    DESTINATION ${CMAKE_INSTALL_PREFIX}/include
    FILES_MATCHING PATTERN "*.hpp"
)

# ============================================================================
# Unit Tests (Optional)
# ============================================================================

option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Documentation (Optional)
# ============================================================================

option(BUILD_DOCS "Build documentation" OFF)
if(BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs)
        set(DOXYGEN_EXTRACT_ALL YES)
        set(DOXYGEN_EXTRACT_PRIVATE YES)
        set(DOXYGEN_GENERATE_HTML YES)
        doxygen_add_docs(docs
            ${ALL_SOURCES}
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            COMMENT "Generate documentation"
        )
    endif()
endif()

# ============================================================================
# Performance Benchmarking (Optional)
# ============================================================================

option(BUILD_BENCHMARKS "Build performance benchmarks" OFF)
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ============================================================================
# Print Configuration Summary
# ============================================================================

message(STATUS "")
message(STATUS "==========================================")
message(STATUS "Point Blank Window Manager v${PROJECT_VERSION}")
message(STATUS "==========================================")
message(STATUS "Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:          ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Install Prefix:    ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Optional Features:")
message(STATUS "  Tests:           ${BUILD_TESTS}")
message(STATUS "  Documentation:   ${BUILD_DOCS}")
message(STATUS "  Benchmarks:      ${BUILD_BENCHMARKS}")
message(STATUS "")
message(STATUS "Source Organization:")
message(STATUS "  Core:            ${CMAKE_CURRENT_LIST_DIR}/src/core/")
message(STATUS "  Config:          ${CMAKE_CURRENT_LIST_DIR}/src/config/")
message(STATUS "  Window:          ${CMAKE_CURRENT_LIST_DIR}/src/window/")
message(STATUS "  Display:         ${CMAKE_CURRENT_LIST_DIR}/src/display/")
message(STATUS "  Performance:     ${CMAKE_CURRENT_LIST_DIR}/src/performance/")
message(STATUS "  Utils:           ${CMAKE_CURRENT_LIST_DIR}/src/utils/")
message(STATUS "==========================================")
message(STATUS "")