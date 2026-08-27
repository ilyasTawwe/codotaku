#include <chrono>
#include <iostream>
#include <print>

#include <codotaku/codotaku.hpp>

// 24 vertices for 6 faces with crisp per-face normals
const std::vector<codotaku::Vertex> CUBE_VERTICES = {
    // Front face (Z = +0.5) - Red
    { {-0.5f, -0.5f,  0.5f}, {1.0f, 0.25f, 0.25f}, { 0.0f,  0.0f,  1.0f} },
    { { 0.5f, -0.5f,  0.5f}, {1.0f, 0.25f, 0.25f}, { 0.0f,  0.0f,  1.0f} },
    { { 0.5f,  0.5f,  0.5f}, {1.0f, 0.25f, 0.25f}, { 0.0f,  0.0f,  1.0f} },
    { {-0.5f,  0.5f,  0.5f}, {1.0f, 0.25f, 0.25f}, { 0.0f,  0.0f,  1.0f} },

    // Back face (Z = -0.5) - Cyan
    { { 0.5f, -0.5f, -0.5f}, {0.25f, 1.0f, 1.0f}, { 0.0f,  0.0f, -1.0f} },
    { {-0.5f, -0.5f, -0.5f}, {0.25f, 1.0f, 1.0f}, { 0.0f,  0.0f, -1.0f} },
    { {-0.5f,  0.5f, -0.5f}, {0.25f, 1.0f, 1.0f}, { 0.0f,  0.0f, -1.0f} },
    { { 0.5f,  0.5f, -0.5f}, {0.25f, 1.0f, 1.0f}, { 0.0f,  0.0f, -1.0f} },

    // Top face (Y = -0.5) - Green
    { {-0.5f, -0.5f, -0.5f}, {0.25f, 1.0f, 0.25f}, { 0.0f, -1.0f,  0.0f} },
    { { 0.5f, -0.5f, -0.5f}, {0.25f, 1.0f, 0.25f}, { 0.0f, -1.0f,  0.0f} },
    { { 0.5f, -0.5f,  0.5f}, {0.25f, 1.0f, 0.25f}, { 0.0f, -1.0f,  0.0f} },
    { {-0.5f, -0.5f,  0.5f}, {0.25f, 1.0f, 0.25f}, { 0.0f, -1.0f,  0.0f} },

    // Bottom face (Y = +0.5) - Magenta
    { {-0.5f,  0.5f,  0.5f}, {1.0f, 0.25f, 1.0f}, { 0.0f,  1.0f,  0.0f} },
    { { 0.5f,  0.5f,  0.5f}, {1.0f, 0.25f, 1.0f}, { 0.0f,  1.0f,  0.0f} },
    { { 0.5f,  0.5f, -0.5f}, {1.0f, 0.25f, 1.0f}, { 0.0f,  1.0f,  0.0f} },
    { {-0.5f,  0.5f, -0.5f}, {1.0f, 0.25f, 1.0f}, { 0.0f,  1.0f,  0.0f} },

    // Right face (X = +0.5) - Blue
    { { 0.5f, -0.5f,  0.5f}, {0.25f, 0.5f, 1.0f}, { 1.0f,  0.0f,  0.0f} },
    { { 0.5f, -0.5f, -0.5f}, {0.25f, 0.5f, 1.0f}, { 1.0f,  0.0f,  0.0f} },
    { { 0.5f,  0.5f, -0.5f}, {0.25f, 0.5f, 1.0f}, { 1.0f,  0.0f,  0.0f} },
    { { 0.5f,  0.5f,  0.5f}, {0.25f, 0.5f, 1.0f}, { 1.0f,  0.0f,  0.0f} },

    // Left face (X = -0.5) - Yellow
    { {-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.25f}, {-1.0f,  0.0f,  0.0f} },
    { {-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 0.25f}, {-1.0f,  0.0f,  0.0f} },
    { {-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 0.25f}, {-1.0f,  0.0f,  0.0f} },
    { {-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 0.25f}, {-1.0f,  0.0f,  0.0f} },
};

const std::vector<uint16_t> CUBE_INDICES = {
     0,  1,  2,   2,  3,  0, // Front
     4,  5,  6,   6,  7,  4, // Back
     8,  9, 10,  10, 11,  8, // Top
    12, 13, 14,  14, 15, 12, // Bottom
    16, 17, 18,  18, 19, 16, // Right
    20, 21, 22,  22, 23, 20  // Left
};

const char* SHADER_SLANG_CODE = R"(
struct Vertex {
    float3 position;
    float3 color;
    float3 normal;
};

struct SceneData {
    float4x4 view_proj;
    float4x4 view;
    float4x4 proj;
    float4x4 model;
    float3 camera_pos;
    float time;

    float3 light_dir;
    float ambient_intensity;
    float3 light_color;
    float _pad0;

    float3 tint;
    float _pad1;

    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint64_t indirectCommandsAddress;
    uint64_t customDataAddress;
};

// 8-byte 64-bit Root Buffer GPU pointer
struct PushConstants {
    uint64_t sceneDataAddress;
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
    uint16_t* indices = (uint16_t*)scene->indexBufferAddress;
    Vertex* vertices  = (Vertex*)scene->vertexBufferAddress;

    uint vertexIndex = indices[indexID];
    Vertex input     = vertices[vertexIndex];

    VertexOutput output;
    float4 worldPos = mul(float4(input.position, 1.0), scene->model);
    output.position = mul(worldPos, scene->view_proj);
    output.color    = input.color * scene->tint;
    output.normal   = normalize(mul(input.normal, (float3x3)scene->model));
    return output;
}

[shader("fragment")]
float4 fsMain(VertexOutput input) : SV_Target {
    SceneData* scene = (SceneData*)pc.sceneDataAddress;
    float3 lightDir = normalize(scene->light_dir);
    float diff = max(dot(input.normal, lightDir), 0.0);
    float3 ambient = scene->ambient_intensity * input.color;
    float3 diffuse = diff * input.color * scene->light_color;
    return float4(ambient + diffuse, 1.0);
}
)";

int main() {
    try {
        codotaku::Application app("Codotaku Engine Demo (GPU-Driven Indirect BDA)");

        // 1. Upload Mesh to Static Geometry Arena
        auto mesh = app.upload_mesh(CUBE_VERTICES, CUBE_INDICES);

        // 2. Upload Indirect Draw Command to Static Arena
        auto indirect_batch = app.upload_indirect_command({
            .vertexCount = mesh.index_count,
            .instanceCount = 1,
            .firstVertex = 0,
            .firstInstance = 0,
        });

        // 3. Compile Slang Shader & Create Dynamic Rendering Pipeline
        auto pipeline = app.create_pipeline(SHADER_SLANG_CODE);

        // 4. Create 3D Camera
        codotaku::Camera camera({0.0f, 1.4f, 3.2f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 45.0f);

        // 5. Spawn Windows with configurable Buffer Count, VSync mode, and Format Selector
        app.create_window({
            .title = "Window 1 (Triple Buffer, VSync ON, Cyan)",
            .width = 800,
            .height = 600,
            .buffer_count = 3,
            .present_mode = codotaku::PresentMode::Fifo,
            .format_selector = [](std::span<const codotaku::ColorFormat> available) {
                // User lambda: pick standard B8G8R8A8_UNORM format
                for (const auto& fmt : available) {
                    if (fmt.vk_format == VK_FORMAT_B8G8R8A8_UNORM) return fmt;
                }
                return available.front();
            }
        });

        app.create_window({
            .title = "Window 2 (Double Buffer, Immediate, Gold)",
            .width = 600,
            .height = 600,
            .buffer_count = 2,
            .present_mode = codotaku::PresentMode::Immediate,
        });

        app.create_window({
            .title = "Window 3 (Triple Buffer, VSync ON, Purple)",
            .width = 500,
            .height = 500,
            .buffer_count = 3,
            .present_mode = codotaku::PresentMode::Fifo,
        });

        auto start_time = std::chrono::steady_clock::now();

        // 6. Transparent, Non-intrusive Render Loop (User controls command recording & submission!)
        int ret = app.run([&](codotaku::Window& window, codotaku::FrameContext& frame) {
            auto now = std::chrono::steady_clock::now();
            float time_sec = std::chrono::duration<float>(now - start_time).count();

            // Set camera aspect ratio matching the active window
            camera.set_aspect_ratio(frame.aspect_ratio);

            // Compute 3D Model transformation
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::rotate(model, time_sec * 0.9f, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, time_sec * 0.6f, glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, time_sec * 0.3f, glm::vec3(0.0f, 0.0f, 1.0f));

            // Suballocate per-frame SceneData inside the window's dynamic frame arena
            auto scene_suballoc = frame.frame_arena.suballocate(sizeof(codotaku::SceneData), 64);
            auto* scene = reinterpret_cast<codotaku::SceneData*>(
                static_cast<uint8_t*>(frame.frame_arena.get_mapped_data()) + scene_suballoc.offset);

            scene->view_proj = camera.get_view_projection_matrix();
            scene->view = camera.get_view_matrix();
            scene->proj = camera.get_projection_matrix();
            scene->model = model;
            scene->camera_pos = camera.get_position();
            scene->time = time_sec;
            scene->light_dir = glm::vec3(0.5f, 0.8f, 0.7f);
            scene->ambient_intensity = 0.3f;
            scene->light_color = glm::vec3(1.0f, 1.0f, 1.0f);
            scene->tint = (window.get_title().find("Gold") != std::string::npos)
                              ? glm::vec3(1.0f, 0.75f, 0.3f)
                              : ((window.get_title().find("Purple") != std::string::npos)
                                     ? glm::vec3(1.0f, 0.4f, 1.0f)
                                     : glm::vec3(0.4f, 1.0f, 1.0f));
            scene->vertexBufferAddress = mesh.vertex_address;
            scene->indexBufferAddress = mesh.index_address;
            scene->indirectCommandsAddress = indirect_batch.device_address;

            // Direct Vulkan Dynamic Rendering pass!
            frame.begin_rendering({.float32 = {0.05f, 0.05f, 0.08f, 1.0f}}, 1.0f);
            frame.set_viewport_and_scissor();

            vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.get_pipeline());

            // Push the 8-byte 64-bit Root Scene BDA address
            uint64_t scene_addr = scene_suballoc.device_address;
            vkCmdPushConstants(
                frame.cmd,
                pipeline.get_layout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(uint64_t),
                &scene_addr);

            // GPU-Driven Indirect Draw!
            vkCmdDrawIndirect(
                frame.cmd,
                app.get_geometry_arena().get_buffer(),
                indirect_batch.offset,
                indirect_batch.draw_count,
                indirect_batch.stride);

            frame.end_rendering();
        });

        pipeline.cleanup();
        return ret;

    } catch (const std::exception& e) {
        std::println(std::cerr, "Fatal error: {}", e.what());
        return 1;
    }
}
