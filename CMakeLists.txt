cmake_minimum_required(VERSION 3.28)
project(codotaku LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Packages
find_package(VulkanHeaders REQUIRED CONFIG)
find_package(volk REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
find_package(glm REQUIRED CONFIG)

# Slang compiler library and headers
find_library(SLANG_COMPILER_LIBRARY NAMES slang-compiler REQUIRED)
find_path(SLANG_INCLUDE_DIR NAMES slang.h PATH_SUFFIXES shader-slang REQUIRED)

include(FetchContent)
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-protocols)
pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

find_file(VOLK_C NAMES volk.c PATHS /usr/include /usr/local/include)
if(NOT VOLK_C)
    message(FATAL_ERROR "volk.c source file not found")
endif()

# 1. xdg-shell protocol
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

# 2. linux-dmabuf-v1 protocol
set(DMABUF_XML "${WAYLAND_PROTOCOLS_DIR}/stable/linux-dmabuf/linux-dmabuf-v1.xml")
set(DMABUF_HEADER "${CMAKE_CURRENT_BINARY_DIR}/linux-dmabuf-v1-client-protocol.h")
set(DMABUF_CODE "${CMAKE_CURRENT_BINARY_DIR}/linux-dmabuf-v1-protocol.c")

add_custom_command(
    OUTPUT "${DMABUF_HEADER}"
    COMMAND ${WAYLAND_SCANNER} client-header "${DMABUF_XML}" "${DMABUF_HEADER}"
    DEPENDS "${DMABUF_XML}"
    COMMENT "Generating linux-dmabuf-v1-client-protocol.h"
    VERBATIM
)

add_custom_command(
    OUTPUT "${DMABUF_CODE}"
    COMMAND ${WAYLAND_SCANNER} private-code "${DMABUF_XML}" "${DMABUF_CODE}"
    DEPENDS "${DMABUF_XML}"
    COMMENT "Generating linux-dmabuf-v1-protocol.c"
    VERBATIM
)

# 3. linux-drm-syncobj-v1 protocol
set(SYNCOBJ_XML "${WAYLAND_PROTOCOLS_DIR}/staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml")
set(SYNCOBJ_HEADER "${CMAKE_CURRENT_BINARY_DIR}/linux-drm-syncobj-v1-client-protocol.h")
set(SYNCOBJ_CODE "${CMAKE_CURRENT_BINARY_DIR}/linux-drm-syncobj-v1-protocol.c")

add_custom_command(
    OUTPUT "${SYNCOBJ_HEADER}"
    COMMAND ${WAYLAND_SCANNER} client-header "${SYNCOBJ_XML}" "${SYNCOBJ_HEADER}"
    DEPENDS "${SYNCOBJ_XML}"
    COMMENT "Generating linux-drm-syncobj-v1-client-protocol.h"
    VERBATIM
)

add_custom_command(
    OUTPUT "${SYNCOBJ_CODE}"
    COMMAND ${WAYLAND_SCANNER} private-code "${SYNCOBJ_XML}" "${SYNCOBJ_CODE}"
    DEPENDS "${SYNCOBJ_XML}"
    COMMENT "Generating linux-drm-syncobj-v1-protocol.c"
    VERBATIM
)

add_executable(codotaku
    main.cpp
    "${VOLK_C}"
    "${XDG_SHELL_HEADER}"
    "${XDG_SHELL_CODE}"
    "${DMABUF_HEADER}"
    "${DMABUF_CODE}"
    "${SYNCOBJ_HEADER}"
    "${SYNCOBJ_CODE}"
)

target_include_directories(codotaku PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${SLANG_INCLUDE_DIR}"
)

target_compile_definitions(codotaku PRIVATE
    VK_USE_PLATFORM_WAYLAND_KHR
)

target_compile_options(codotaku PRIVATE
    -Wno-nullability-completeness
)

target_link_libraries(codotaku PRIVATE
    volk::volk_headers
    Vulkan::Headers
    GPUOpen::VulkanMemoryAllocator
    glm::glm
    "${SLANG_COMPILER_LIBRARY}"
    PkgConfig::WAYLAND
    PkgConfig::DRM
)
