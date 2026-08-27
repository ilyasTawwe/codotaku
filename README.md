# Codotaku

[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)](https://en.cppreference.com/w/cpp/26)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.4-red.svg)](https://www.vulkan.org/)
[![Wayland](https://img.shields.io/badge/Wayland-Protocols-orange.svg)](https://gitlab.freedesktop.org/wayland/wayland-protocols)
[![Slang](https://img.shields.io/badge/Shader-Slang-purple.svg)](https://github.com/shader-slang/slang)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**Codotaku** is a modern, opinionated, swapchain-less C++26 library and engine framework designed for native Wayland and Vulkan 1.4+ rendering.

Inspired by modern Linux presentation paradigms (such as NVIDIA's `egl-wayland2` and Chromium's native Wayland backend), Codotaku drops legacy Vulkan WSI swapchains in favor of **`linux-dmabuf`**, **DRM timeline syncobj explicit synchronization**, **Dynamic Rendering**, **Buffer Device Address (BDA) programmable vertex pulling**, and **runtime Slang shader reflection**.

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
- **Programmable Vertex Pulling via Buffer Device Address (BDA)**:
  - 100% bindless vertex and index fetching via 64-bit GPU pointers (`(Vertex*)pc.vertexBufferAddress`).
  - Completely empty pipeline vertex input state (`vertexBindingDescriptionCount = 0`).
- **Two-Buffer Arena Architecture (`VmaVirtualBlock`)**:
  - **Static Geometry Arena**: 4 MB contiguous VRAM buffer managing suballocations for vertex/index geometry.
  - **Dynamic Frame Arena**: 64 KB host-mapped ring buffer for per-frame scene/transform data.
  - Compact **8-byte push constants** (`uint64_t sceneDataAddress`).
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
│       ├── core/types.hpp                 # Common structs (Vertex, SceneData, WindowConfig)
│       ├── wayland/
│       │   ├── context.hpp                # Wayland display, registry, protocols
│       │   └── window.hpp                 # Multi-window lifecycle, DMA-BUF pool, depth, rendering
│       ├── vulkan/
│       │   ├── context.hpp                # Vulkan 1.4 context, Volk, VMA, DRM node
│       │   ├── arena.hpp                  # GpuBufferArena (VmaVirtualBlock suballocations)
│       │   ├── sync.hpp                   # DRM syncobj timeline explicit synchronization
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
        └── main.cpp                       # Consumer example code (< 70 lines)
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

// Slang Shader with 64-bit Buffer Device Address (BDA) pointer dereferencing
const char* SHADER_CODE = R"(
struct Vertex {
    float3 position;
    float3 color;
    float3 normal;
};

struct SceneData {
    float4x4 mvp;
    float4x4 model;
    float3 tint;
    float _pad;
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
};

struct PushConstants {
    uint64_t sceneDataAddress; // 8 bytes!
};
[[vk::push_constant]] PushConstants pc;

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
    float3 normal : NORMAL;
};

[shader("vertex")]
VertexOutput vsMain(uint indexID : SV_VertexID) {
    SceneData* scene = (SceneData*)pc.sceneDataAddress;
    uint16_t* indices = (uint16_t*)scene.indexBufferAddress;
    Vertex* vertices = (Vertex*)scene.vertexBufferAddress;

    uint vertexIndex = indices[indexID];
    Vertex input = vertices[vertexIndex];

    VertexOutput output;
    output.position = mul(float4(input.position, 1.0), scene.mvp);
    output.color = input.color * scene.tint;
    output.normal = normalize(mul(input.normal, (float3x3)scene.model));
    return output;
}

[shader("fragment")]
float4 fsMain(VertexOutput input) : SV_Target {
    float3 lightDir = normalize(float3(0.5, 0.8, 0.7));
    float diff = max(dot(input.normal, lightDir), 0.0);
    return float4((0.3 + diff) * input.color, 1.0);
}
)";

int main() {
    try {
        codotaku::Application app("Codotaku Engine Demo");

        // 1. Upload geometry to the Static Arena
        app.set_mesh_data(CUBE_VERTICES, CUBE_INDICES);

        // 2. Compile Slang shader & reflect pipeline
        app.set_shader_source(SHADER_CODE);

        // 3. Spawn independent Wayland 3D windows with custom styles
        app.create_window({
            .title = "Window 1 (Cyan Theme)",
            .width = 800, .height = 600,
            .rotation_speed = 1.4f,
            .tint = {0.4f, 1.0f, 1.0f}
        });

        app.create_window({
            .title = "Window 2 (Gold Theme)",
            .width = 600, .height = 600,
            .rotation_speed = -1.0f,
            .tint = {1.0f, 0.75f, 0.3f}
        });

        // 4. Run non-blocking multi-window event loop
        return app.run();

    } catch (const std::exception& e) {
        std::println(std::cerr, "Fatal error: {}", e.what());
        return 1;
    }
}
```

---

## License
MIT License. See [LICENSE](LICENSE) for details.
