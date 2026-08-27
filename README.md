# Codotaku

[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)](https://en.cppreference.com/w/cpp/26)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.4-red.svg)](https://www.vulkan.org/)
[![Wayland](https://img.shields.io/badge/Wayland-Protocols-orange.svg)](https://gitlab.freedesktop.org/wayland/wayland-protocols)
[![Slang](https://img.shields.io/badge/Shader-Slang-purple.svg)](https://github.com/shader-slang/slang)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**Codotaku** is a modern, opinionated, swapchain-less C++26 library and engine framework designed for native Wayland and Vulkan 1.4+ rendering.

Inspired by modern Linux presentation paradigms (such as NVIDIA's `egl-wayland2` and Chromium's native Wayland backend), Codotaku removes platform and driver boilerplate while **giving users transparent, raw access to Vulkan command buffers, queues, and draw calls**.

---

## Key Highlights & Architectural Decisions

- **Swapchain-less Native Wayland Presentation**:
  - Direct buffer presentation using `zwp_linux_dmabuf_v1` with negotiated DRM format modifiers.
  - Direct-scanout ready, zero-copy compositing without window flickering.
- **Explicit GPU Synchronization (`wp_linux_drm_syncobj_manager_v1`)**:
  - Eliminates micro-stutter, tearing, and race conditions using 64-bit DRM syncobj timeline points.
  - Transparent `sync_file` export/import bridging (`VK_KHR_external_semaphore_fd` $\to$ `drmSyncobjImportSyncFile` $\to$ `drmSyncobjTransfer`).
- **Strictly Modern Vulkan (1.4+ Core)**:
  - **Dynamic Rendering** (`vkCmdBeginRendering` / `vkCmdEndRendering`) — zero legacy render passes or framebuffers.
  - **Synchronization 2** (`vkCmdPipelineBarrier2`, `VkDependencyInfo`, `VkImageMemoryBarrier2`).
  - **Volk & VMA**: Volk meta-loader and Vulkan Memory Allocator integrated with Buffer Device Address support.
- **GPU-Driven Indirect Drawing & Programmable Vertex Pulling (BDA)**:
  - Supports `vkCmdDrawIndirect` and `vkCmdDrawIndexedIndirect` from GPU buffers.
  - 100% bindless vertex and index fetching via 64-bit GPU pointers (`(Vertex*)pc.vertexBufferAddress`).
  - Completely empty pipeline vertex input state (`vertexBindingDescriptionCount = 0`).
- **Two-Buffer Arena Architecture (`VmaVirtualBlock`)**:
  - **Static Geometry Arena**: 4 MB contiguous VRAM buffer managing suballocations for vertex/index geometry and indirect draw commands.
  - **Dynamic Frame Arena**: 64 KB host-mapped ring buffer for per-frame scene/transform data.
  - Compact **8-byte push constants** (`uint64_t sceneDataAddress`).
- **Built-in 3D Camera & Scene Abstractions**:
  - `codotaku::Camera`: Full perspective, view matrix (`lookAt`), orbit, zoom, and Vulkan NDC clip space alignment.
  - `codotaku::SceneData`: Camera matrices, lighting parameters, and hierarchical BDA addresses.
- **Configurable Presentation & Windowing Options**:
  - Configurable double/triple buffering (`buffer_count`).
  - VSync control (`PresentMode::Fifo` vs `PresentMode::Immediate`).
  - Format selection lambda (`FormatSelector`) to negotiate custom 10-bit HDR or sRGB formats.
- **Runtime Slang Shader Compilation & Reflection**:
  - Compiles Slang shaders to SPIR-V 1.5 at runtime.
  - Automatically reflects push constant ranges and memory structures.
- **Native Multi-Window & Server-Side Decorations (SSD)**:
  - Supports spawning independent 3D windows with dedicated buffer pools and timelines.
  - Automatically requests and negotiates server-side decorations via `zxdg_decoration_manager_v1`.

---

## Directory Structure

```
codotaku/
├── CMakeLists.txt                         # Defines 'codotaku' library & 'codotaku_example'
├── CMakePresets.json                      # CMake Presets (Debug, Release, RelWithDebInfo)
├── include/
│   └── codotaku/
│       ├── codotaku.hpp                   # Master convenience include
│       ├── core/
│       │   ├── types.hpp                  # Common structs (Vertex, WindowConfig, ColorFormat)
│       │   ├── camera.hpp                 # 3D Camera (Perspective, LookAt, Orbit, Zoom)
│       │   └── scene.hpp                  # SceneData & MeshHandle
│       ├── wayland/
│       │   ├── context.hpp                # Wayland display, registry, protocols
│       │   └── window.hpp                 # Multi-window lifecycle, FrameContext, DMA-BUF pool
│       ├── vulkan/
│       │   ├── context.hpp                # Vulkan 1.4 context, Volk, VMA, DRM node
│       │   ├── arena.hpp                  # GpuBufferArena (VmaVirtualBlock suballocations)
│       │   ├── sync.hpp                   # DRM syncobj timeline explicit synchronization
│       │   ├── indirect.hpp               # Indirect draw command batch helper
│       │   └── pipeline.hpp               # Dynamic rendering BDA graphics pipeline
│       ├── shader/
│       │   └── slang_compiler.hpp         # Runtime Slang compiler & push constant reflection
│       └── app/
│           └── application.hpp            # Application framework & non-blocking event loop
├── src/
│   ├── wayland/ (context.cpp, window.cpp)
│   ├── vulkan/  (context.cpp, arena.cpp, sync.cpp, pipeline.cpp)
│   ├── shader/  (slang_compiler.cpp)
│   └── app/     (application.cpp)
└── examples/
    └── multiwindow_cubes/
        └── main.cpp                       # Consumer example code (< 80 lines)
```

---

## Dependencies

Ensure the following dependencies are installed on your Linux system:

### Arch Linux / CachyOS
```bash
sudo pacman -S clang llvm ninja pkgconf libdrm wayland wayland-protocols \
               vulkan-devel vulkan-headers vulkan-validation-layers \
               shader-slang glm
```

---

## Building and Running

### Using CMake Presets (Command Line)
```bash
# Configure and build Debug preset
cmake --preset debug
cmake --build --preset debug

# Run the multi-window 3D cube example
./build/debug/codotaku_example

# Build and run Release preset
cmake --preset release
cmake --build --preset release
./build/release/codotaku_example
```

### In CLion / IDEs
1. Open the project in CLion.
2. Select the **`Debug (Clang)`** or **`Release (Clang)`** profile detected from `CMakePresets.json`.
3. Hit **Build** or **Run** `codotaku_example`.

---

## Example Usage

```cpp
#include <codotaku/codotaku.hpp>

int main() {
    try {
        codotaku::Application app("Codotaku Engine Demo");

        // 1. Upload Mesh to the Static Geometry Arena
        auto mesh = app.upload_mesh(CUBE_VERTICES, CUBE_INDICES);

        // 2. Upload Indirect Draw Command to Static Arena
        auto indirect_batch = app.upload_indirect_command({
            .vertexCount = mesh.index_count,
        });

        // 3. Compile Slang Shader & Create Dynamic Rendering Pipeline
        auto pipeline = app.create_pipeline(SHADER_SLANG_CODE);

        // 4. Create 3D Camera
        codotaku::Camera camera({0.0f, 1.4f, 3.2f}, {0.0f, 0.0f, 0.0f});

        // 5. Spawn Windows with configurable Buffer Count, VSync mode, and Format Selector
        app.create_window({
            .title = "Window 1 (Triple Buffer, VSync ON, Cyan)",
            .width = 800, .height = 600,
            .buffer_count = 3,
            .present_mode = codotaku::PresentMode::Fifo,
            .format_selector = [](auto available) { return available.front(); }
        });

        app.create_window({
            .title = "Window 2 (Double Buffer, Immediate, Gold)",
            .width = 600, .height = 600,
            .buffer_count = 2,
            .present_mode = codotaku::PresentMode::Immediate,
        });

        auto start_time = std::chrono::steady_clock::now();

        // 6. Transparent, Non-intrusive Render Loop (User controls command recording & submission!)
        return app.run([&](codotaku::Window& window, codotaku::FrameContext& frame) {
            float time_sec = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_time).count();

            // Set camera aspect ratio matching the active window
            camera.set_aspect_ratio(frame.aspect_ratio);

            // Suballocate per-frame SceneData inside the window's dynamic frame arena
            auto scene_suballoc = frame.frame_arena.suballocate(sizeof(codotaku::SceneData), 64);
            auto* scene = reinterpret_cast<codotaku::SceneData*>(
                static_cast<uint8_t*>(frame.frame_arena.get_mapped_data()) + scene_suballoc.offset);

            scene->view_proj = camera.get_view_projection_matrix();
            scene->view = camera.get_view_matrix();
            scene->proj = camera.get_projection_matrix();
            scene->camera_pos = camera.get_position();
            scene->time = time_sec;
            scene->vertexBufferAddress = mesh.vertex_address;
            scene->indexBufferAddress = mesh.index_address;

            // Direct Vulkan Dynamic Rendering pass!
            frame.begin_rendering({.float32 = {0.05f, 0.05f, 0.08f, 1.0f}}, 1.0f);
            frame.set_viewport_and_scissor();

            vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.get_pipeline());

            // Push 8-byte Root Scene BDA address
            uint64_t scene_addr = scene_suballoc.device_address;
            vkCmdPushConstants(frame.cmd, pipeline.get_layout(), 
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                               0, sizeof(uint64_t), &scene_addr);

            // GPU-Driven Indirect Draw!
            vkCmdDrawIndirect(frame.cmd, app.get_geometry_arena().get_buffer(),
                              indirect_batch.offset, indirect_batch.draw_count, indirect_batch.stride);

            frame.end_rendering();
        });

    } catch (const std::exception& e) {
        std::println(std::cerr, "Fatal error: {}", e.what());
        return 1;
    }
}
```

---

## License
MIT License. See [LICENSE](LICENSE) for details.
