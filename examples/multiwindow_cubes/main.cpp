#include <chrono>
#include <iostream>
#include <print>

#include <codotaku/codotaku.hpp>

// User/Application-defined Vertex format matching shader layout
struct CustomVertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec3 normal;
    glm::vec2 uv;
};

// 24 vertices for 6 faces with crisp per-face normals & UV coordinates
const std::vector<CustomVertex> CUBE_VERTICES = {
    // Front face (Z = +0.5) - Red
    { {-0.5f, -0.5f,  0.5f}, {1.0f, 0.25f, 0.25f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f} },
    { { 0.5f, -0.5f,  0.5f}, {1.0f, 0.25f, 0.25f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f} },
    { { 0.5f,  0.5f,  0.5f}, {1.0f, 0.25f, 0.25f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f} },
    { {-0.5f,  0.5f,  0.5f}, {1.0f, 0.25f, 0.25f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f} },

    // Back face (Z = -0.5) - Cyan
    { { 0.5f, -0.5f, -0.5f}, {0.25f, 1.0f, 1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f} },
    { {-0.5f, -0.5f, -0.5f}, {0.25f, 1.0f, 1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f} },
    { {-0.5f,  0.5f, -0.5f}, {0.25f, 1.0f, 1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f} },
    { { 0.5f,  0.5f, -0.5f}, {0.25f, 1.0f, 1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f} },

    // Top face (Y = -0.5) - Green
    { {-0.5f, -0.5f, -0.5f}, {0.25f, 1.0f, 0.25f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f} },
    { { 0.5f, -0.5f, -0.5f}, {0.25f, 1.0f, 0.25f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f} },
    { { 0.5f, -0.5f,  0.5f}, {0.25f, 1.0f, 0.25f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f} },
    { {-0.5f, -0.5f,  0.5f}, {0.25f, 1.0f, 0.25f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f} },

    // Bottom face (Y = +0.5) - Magenta
    { {-0.5f,  0.5f,  0.5f}, {1.0f, 0.25f, 1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f} },
    { { 0.5f,  0.5f,  0.5f}, {1.0f, 0.25f, 1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f} },
    { { 0.5f,  0.5f, -0.5f}, {1.0f, 0.25f, 1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f} },
    { {-0.5f,  0.5f, -0.5f}, {1.0f, 0.25f, 1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f} },

    // Right face (X = +0.5) - Blue
    { { 0.5f, -0.5f,  0.5f}, {0.25f, 0.5f, 1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} },
    { { 0.5f, -0.5f, -0.5f}, {0.25f, 0.5f, 1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f} },
    { { 0.5f,  0.5f, -0.5f}, {0.25f, 0.5f, 1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} },
    { { 0.5f,  0.5f,  0.5f}, {0.25f, 0.5f, 1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f} },

    // Left face (X = -0.5) - Yellow
    { {-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.25f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} },
    { {-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 0.25f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f} },
    { {-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 0.25f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} },
    { {-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 0.25f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f} },
};

const std::vector<uint16_t> CUBE_INDICES = {
     0,  1,  2,   2,  3,  0, // Front
     4,  5,  6,   6,  7,  4, // Back
     8,  9, 10,  10, 11,  8, // Top
    12, 13, 14,  14, 15, 12, // Bottom
    16, 17, 18,  18, 19, 16, // Right
    20, 21, 22,  22, 23, 20  // Left
};

// User/Application-defined Scene Data struct matching shader layout
struct CustomSceneData {
    glm::mat4 view_proj{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::mat4 model{1.0f};
    glm::vec3 camera_pos{0.0f, 0.0f, 0.0f};
    float time{0.0f};

    glm::vec3 light_dir{0.5f, 0.8f, 0.7f};
    float ambient_intensity{0.35f};
    glm::vec3 light_color{1.0f, 1.0f, 1.0f};
    float _pad0{0.0f};

    glm::vec3 tint{1.0f, 1.0f, 1.0f};
    float _pad1{0.0f};

    uint64_t vertexBufferAddress{0};
    uint64_t indexBufferAddress{0};
    uint64_t indirectCommandsAddress{0};
    uint64_t customDataAddress{0};
};

const char* SHADER_SLANG_CODE = R"(
struct Vertex {
    float3 position;
    float3 color;
    float3 normal;
    float2 uv;
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

// Reflected descriptor set texture binding
[[vk::binding(0, 0)]] Sampler2D u_texture;

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
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
    output.uv       = input.uv;
    return output;
}

[shader("fragment")]
float4 fsMain(VertexOutput input) : SV_Target {
    SceneData* scene = (SceneData*)pc.sceneDataAddress;

    float4 texColor = u_texture.Sample(input.uv);
    float3 lightDir = normalize(scene->light_dir);
    float diff = max(dot(input.normal, lightDir), 0.0);
    float3 ambient = scene->ambient_intensity * texColor.rgb * input.color;
    float3 diffuse = diff * texColor.rgb * input.color * scene->light_color;
    return float4(ambient + diffuse, 1.0);
}
)";

int main() {
    try {
        codotaku::Application app("Codotaku Engine Demo");

        // 1. Generic Mesh upload to Static Geometry Arena
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

        // 4. Create Procedural Textures & Descriptor Sets
        auto checkerboard_tex = codotaku::Texture::create_checkerboard(app.get_vulkan(), 256, 256, 32);
        auto grid_tex = codotaku::Texture::create_grid_pattern(app.get_vulkan(), 256, 256);

        VkDescriptorSet checkerboard_desc_set = pipeline.create_texture_descriptor_set(checkerboard_tex, 0, 0);
        VkDescriptorSet grid_desc_set = pipeline.create_texture_descriptor_set(grid_tex, 0, 0);

        // 5. Create 3D Camera
        codotaku::Camera camera({0.0f, 1.4f, 3.2f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 45.0f);

        // Common depth attachment specification defined by user code
        codotaku::AttachmentDesc depth_desc = {
            .name = "depth_buffer",
            .format = VK_FORMAT_D32_SFLOAT,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        };

        // 6. Spawn Windows (Configuring user-defined attachments for each window's GBuffer)
        app.set_close_policy(codotaku::WindowClosePolicy::QuitOnLastWindowClose);

        app.create_window({
            .title = "Window 1 (Primary, Triple Buffer, VSync ON, Checkerboard)",
            .width = 800,
            .height = 600,
            .buffer_count = 3,
            .present_mode = codotaku::PresentMode::Fifo,
            .format_selector = [](std::span<const codotaku::ColorFormat> available) {
                for (const auto& fmt : available) {
                    if (fmt.vk_format == VK_FORMAT_B8G8R8A8_UNORM) return fmt;
                }
                return available.front();
            },
            .attachments = { depth_desc },
            .is_primary = true,
        });

        app.create_window({
            .title = "Window 2 (Double Buffer, Immediate, Grid Texture)",
            .width = 600,
            .height = 600,
            .buffer_count = 2,
            .present_mode = codotaku::PresentMode::Immediate,
            // User populates depth + an extra HDR render target attachment!
            .attachments = {
                depth_desc,
                {
                    .name = "hdr_albedo_rt",
                    .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                }
            }
        });

        app.create_window({
            .title = "Window 3 (Triple Buffer, VSync ON, Custom Close Hook)",
            .width = 500,
            .height = 500,
            .buffer_count = 3,
            .present_mode = codotaku::PresentMode::Fifo,
            .attachments = { depth_desc },
            .on_close = [](codotaku::Window& win) {
                std::println("[Custom Hook] Window '{}' intercepting close event.", win.get_title());
                return true;
            },
        });

        auto start_time = std::chrono::steady_clock::now();

        // 7. Transparent Render Loop (User controls GBuffer attachments, recording & draw calls!)
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

            // Suballocate per-frame custom SceneData inside the window's dynamic frame arena
            auto scene_suballoc = frame.frame_arena.suballocate(sizeof(CustomSceneData), 64);
            auto* scene = reinterpret_cast<CustomSceneData*>(
                static_cast<uint8_t*>(frame.frame_arena.get_mapped_data()) + scene_suballoc.offset);

            scene->view_proj = camera.get_view_projection_matrix();
            scene->view = camera.get_view_matrix();
            scene->proj = camera.get_projection_matrix();
            scene->model = model;
            scene->camera_pos = camera.get_position();
            scene->time = time_sec;
            scene->light_dir = glm::vec3(0.5f, 0.8f, 0.7f);
            scene->ambient_intensity = 0.35f;
            scene->light_color = glm::vec3(1.0f, 1.0f, 1.0f);
            scene->tint = (window.get_title().find("Grid") != std::string::npos)
                              ? glm::vec3(1.0f, 0.8f, 0.4f)
                              : glm::vec3(1.0f, 1.0f, 1.0f);
            scene->vertexBufferAddress = mesh.vertex_address;
            scene->indexBufferAddress = mesh.index_address;
            scene->indirectCommandsAddress = indirect_batch.device_address;

            // Transition user's depth attachment in the window's GBuffer (ID 0)
            frame.gbuffer.transition(
                frame.cmd,
                0, // depth attachment ID
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            // Direct Vulkan Dynamic Rendering pass (using window color DMA-BUF + GBuffer depth!)
            frame.begin_rendering_with_attachment({.float32 = {0.05f, 0.05f, 0.08f, 1.0f}}, 0 /* depth_attachment_id */, 1.0f);
            frame.set_viewport_and_scissor();

            vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.get_pipeline());

            // Bind window texture descriptor set (Grid vs Checkerboard)
            VkDescriptorSet active_desc_set = (window.get_title().find("Grid") != std::string::npos)
                                                  ? grid_desc_set
                                                  : checkerboard_desc_set;
            vkCmdBindDescriptorSets(
                frame.cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline.get_layout(),
                0, 1,
                &active_desc_set,
                0, nullptr);

            // Push 8-byte Root Scene BDA address
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

        checkerboard_tex.cleanup();
        grid_tex.cleanup();
        pipeline.cleanup();
        return ret;

    } catch (const std::exception& e) {
        std::println(std::cerr, "Fatal error: {}", e.what());
        return 1;
    }
}
