#include <chrono>
#include <iostream>
#include <print>

#include <codotaku/codotaku.hpp>

// User-defined Vertex format matching shader layout
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

// User-defined Scene Data struct matching Slang shader layout
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

// Compute Shader: Generates textures directly on the GPU on the fly
const char* COMPUTE_TEXTURE_GEN_SLANG = R"(
[format("rgba8")]
[[vk::binding(0, 0)]] RWTexture2D<float4> u_outputImage;

struct ComputePushConstants {
    uint2 resolution;
    uint pattern_type; // 0 = checkerboard, 1 = glowing grid
    float time;
};
[[vk::push_constant]] ComputePushConstants pc;

[shader("compute")]
[numthreads(16, 16, 1)]
void csMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    if (dispatchThreadID.x >= pc.resolution.x || dispatchThreadID.y >= pc.resolution.y) return;

    float2 uv = float2(dispatchThreadID.xy) / float2(pc.resolution);
    float4 color = float4(1, 1, 1, 1);

    if (pc.pattern_type == 0) {
        // High-contrast vibrant checkerboard with subtle lighting vignette
        uint2 tile = dispatchThreadID.xy / 32;
        bool check = ((tile.x + tile.y) % 2) == 0;
        float dist = length(uv - 0.5);
        float vignette = 1.0 - 0.25 * dist;
        color = check ? float4(0.95 * vignette, 0.95 * vignette, 0.95 * vignette, 1.0)
                      : float4(0.20 * vignette, 0.55 * vignette, 0.90 * vignette, 1.0);
    } else if (pc.pattern_type == 1) {
        // High-contrast glowing grid pattern
        bool edge = (dispatchThreadID.x % 32 == 0) || (dispatchThreadID.y % 32 == 0);
        float pulse = 0.85 + 0.15 * sin(uv.x * 6.28 + uv.y * 6.28);
        color = edge ? float4(1.0, 1.0, 1.0, 1.0)
                     : float4(uv.x * pulse, uv.y * pulse, 0.75, 1.0);
    }

    u_outputImage[dispatchThreadID.xy] = color;
}
)";

// Graphics Shader: 3D Cube rendering with BDA and Slang texture sampling
const char* GRAPHICS_SHADER_SLANG = R"(
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
    float3 baseColor = texColor.rgb * input.color;
    float3 ambient = scene->ambient_intensity * baseColor;
    float3 diffuse = diff * baseColor * scene->light_color;
    return float4(ambient + diffuse, 1.0);
}
)";

int main() {
    try {
        codotaku::Application app("Codotaku Engine Demo (Compute Texture Generation on the Fly)");
        auto& vk = app.get_vulkan();

        // 1. Create Uploader immediately at startup
        codotaku::Uploader uploader(vk);

        // 2. Allocate Static Geometry Arena (4 MB)
        codotaku::GpuBufferArena geometry_arena;
        geometry_arena.init(
            vk.get_allocator(),
            vk.get_device(),
            4 * 1024 * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        // 3. Enqueue geometry uploads into Geometry Arena (returns BDA suballocation immediately)
        auto vb_sub = uploader.upload_to_arena(geometry_arena, std::span(CUBE_VERTICES));
        auto ib_sub = uploader.upload_to_arena(geometry_arena, std::span(CUBE_INDICES));

        // 4. Enqueue indirect draw command upload into Geometry Arena
        VkDrawIndirectCommand indirect_cmd{
            .vertexCount = static_cast<uint32_t>(CUBE_INDICES.size()),
            .instanceCount = 1,
            .firstVertex = 0,
            .firstInstance = 0,
        };
        auto cmd_sub = uploader.upload_to_arena(geometry_arena, indirect_cmd);

        // 5. Submit geometry DMA copies to GPU in background
        uploader.upload();

        // ---------------------------------------------------------------------
        // COMPUTE SHADER: Generate textures on the GPU on the fly!
        // ---------------------------------------------------------------------
        auto compute_pipeline = app.create_compute_pipeline(COMPUTE_TEXTURE_GEN_SLANG);

        // Create 2 storage images (256x256)
        codotaku::TextureDesc tex_desc{
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .min_filter = VK_FILTER_LINEAR,
            .mag_filter = VK_FILTER_LINEAR,
        };
        auto checkerboard_tex = codotaku::Texture::create_uninitialized(vk, 256, 256, tex_desc);
        auto grid_tex = codotaku::Texture::create_uninitialized(vk, 256, 256, tex_desc);

        VkDescriptorSet checkerboard_storage_set = compute_pipeline.create_storage_image_descriptor_set(checkerboard_tex.get_view(), 0, 0);
        VkDescriptorSet grid_storage_set = compute_pipeline.create_storage_image_descriptor_set(grid_tex.get_view(), 0, 0);

        // Execute compute dispatches on the GPU to generate both textures directly into VRAM
        vk.execute_single_time_commands([&](VkCommandBuffer cmd) {
            VkImageSubresourceRange range{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };

            // Transition both images: UNDEFINED -> GENERAL
            VkImageMemoryBarrier2 pre_barriers[2] = {
                {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                    .srcAccessMask = VK_ACCESS_2_NONE,
                    .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = checkerboard_tex.get_image(),
                    .subresourceRange = range,
                },
                {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                    .srcAccessMask = VK_ACCESS_2_NONE,
                    .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = grid_tex.get_image(),
                    .subresourceRange = range,
                }
            };

            VkDependencyInfo pre_dep{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 2,
                .pImageMemoryBarriers = pre_barriers,
            };
            vkCmdPipelineBarrier2(cmd, &pre_dep);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline.get_pipeline());

            // Dispatch 1: Generate Checkerboard (pattern_type = 0)
            struct { uint32_t width, height, pattern_type; float time; } pc0 = { 256, 256, 0, 0.0f };
            vkCmdPushConstants(cmd, compute_pipeline.get_layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc0), &pc0);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline.get_layout(), 0, 1, &checkerboard_storage_set, 0, nullptr);
            vkCmdDispatch(cmd, 256 / 16, 256 / 16, 1);

            // Dispatch 2: Generate Grid Pattern (pattern_type = 1)
            struct { uint32_t width, height, pattern_type; float time; } pc1 = { 256, 256, 1, 0.0f };
            vkCmdPushConstants(cmd, compute_pipeline.get_layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc1), &pc1);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline.get_layout(), 0, 1, &grid_storage_set, 0, nullptr);
            vkCmdDispatch(cmd, 256 / 16, 256 / 16, 1);

            // Transition both images: GENERAL -> SHADER_READ_ONLY_OPTIMAL for fragment sampling
            VkImageMemoryBarrier2 post_barriers[2] = {
                {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = checkerboard_tex.get_image(),
                    .subresourceRange = range,
                },
                {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = grid_tex.get_image(),
                    .subresourceRange = range,
                }
            };

            VkDependencyInfo post_dep{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 2,
                .pImageMemoryBarriers = post_barriers,
            };
            vkCmdPipelineBarrier2(cmd, &post_dep);
        });

        compute_pipeline.cleanup();
        std::println("[Main] Successfully generated textures on the fly using Slang compute shader!");

        // ---------------------------------------------------------------------
        // GRAPHICS PIPELINE & WINDOW CREATION
        // ---------------------------------------------------------------------
        auto graphics_pipeline = app.create_pipeline(GRAPHICS_SHADER_SLANG);

        VkDescriptorSet checkerboard_sampled_set = graphics_pipeline.create_texture_descriptor_set(checkerboard_tex, 0, 0);
        VkDescriptorSet grid_sampled_set = graphics_pipeline.create_texture_descriptor_set(grid_tex, 0, 0);

        codotaku::Camera camera({0.0f, 1.4f, 3.2f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 45.0f);

        codotaku::AttachmentDesc depth_desc = {
            .name = "depth_buffer",
            .format = VK_FORMAT_D32_SFLOAT,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        };

        app.set_close_policy(codotaku::WindowClosePolicy::QuitOnLastWindowClose);

        app.create_window({
            .title = "Window 1 (Primary, Compute Checkerboard)",
            .width = 800,
            .height = 600,
            .buffer_count = 3,
            .present_mode = codotaku::PresentMode::Fifo,
            .attachments = { depth_desc },
            .is_primary = true,
        });

        app.create_window({
            .title = "Window 2 (Immediate, Compute Grid Pattern)",
            .width = 600,
            .height = 600,
            .buffer_count = 2,
            .present_mode = codotaku::PresentMode::Immediate,
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
            .title = "Window 3 (Compute Checkerboard, Custom Close Hook)",
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

        // Wait on geometry upload fence right before starting render loop
        uploader.wait();

        auto start_time = std::chrono::steady_clock::now();

        // Transparent Render Loop
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
            scene->vertexBufferAddress = vb_sub.device_address;
            scene->indexBufferAddress = ib_sub.device_address;
            scene->indirectCommandsAddress = cmd_sub.device_address;

            // Transition depth attachment in the window's GBuffer (ID 0) to GENERAL layout
            frame.gbuffer.transition(
                frame.cmd,
                0, // depth attachment ID
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            // Direct Vulkan Dynamic Rendering pass (operating in unified GENERAL layout!)
            frame.begin_rendering_with_attachment({.float32 = {0.05f, 0.05f, 0.08f, 1.0f}}, 0, 1.0f);
            frame.set_viewport_and_scissor();

            vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline.get_pipeline());

            // Bind window texture descriptor set (Grid vs Checkerboard)
            VkDescriptorSet active_desc_set = (window.get_title().find("Grid") != std::string::npos)
                                                  ? grid_sampled_set
                                                  : checkerboard_sampled_set;
            vkCmdBindDescriptorSets(
                frame.cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                graphics_pipeline.get_layout(),
                0, 1,
                &active_desc_set,
                0, nullptr);

            // Push 8-byte Root Scene BDA address
            uint64_t scene_addr = scene_suballoc.device_address;
            vkCmdPushConstants(
                frame.cmd,
                graphics_pipeline.get_layout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(uint64_t),
                &scene_addr);

            // GPU-Driven Indirect Draw!
            vkCmdDrawIndirect(
                frame.cmd,
                geometry_arena.get_buffer(),
                cmd_sub.offset,
                1,
                sizeof(VkDrawIndirectCommand));

            frame.end_rendering();
        });

        checkerboard_tex.cleanup();
        grid_tex.cleanup();
        geometry_arena.cleanup(vk.get_allocator());
        graphics_pipeline.cleanup();
        return ret;

    } catch (const std::exception& e) {
        std::println(std::cerr, "Fatal error: {}", e.what());
        return 1;
    }
}
