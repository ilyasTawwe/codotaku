cmake_minimum_required(VERSION 3.28)
project(codotaku LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Packages
find_package(VulkanHeaders REQUIRED CONFIG)
find_package(volk REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)

pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-protocols)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

# Generate xdg-shell protocol C code and headers
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
set(XDG_SHELL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h")
set(XDG_SHELL_CODE "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c")

add_custom_command(
    OUTPUT "${XDG_SHELL_HEADER}"
    COMMAND ${WAYLAND_SCANNER} client-header "${XDG_SHELL_XML}" "${XDG_SHELL_HEADER}"
    DEPENDS "${XDG_SHELL_XML}"
    COMMENT "Generating xdg-shell-client-protocol.h"
    VERBATIM
)

add_custom_command(
    OUTPUT "${XDG_SHELL_CODE}"
    COMMAND ${WAYLAND_SCANNER} private-code "${XDG_SHELL_XML}" "${XDG_SHELL_CODE}"
    DEPENDS "${XDG_SHELL_XML}"
    COMMENT "Generating xdg-shell-protocol.c"
    VERBATIM
)

find_file(VOLK_C NAMES volk.c PATHS /usr/include /usr/local/include)
if(NOT VOLK_C)
    message(FATAL_ERROR "volk.c source file not found")
endif()

add_executable(codotaku
    main.cpp
    "${VOLK_C}"
    "${XDG_SHELL_HEADER}"
    "${XDG_SHELL_CODE}"
)

target_include_directories(codotaku PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_compile_definitions(codotaku PRIVATE
    VK_USE_PLATFORM_WAYLAND_KHR
)

target_link_libraries(codotaku PRIVATE
    volk::volk_headers
    Vulkan::Headers
    PkgConfig::WAYLAND
)
