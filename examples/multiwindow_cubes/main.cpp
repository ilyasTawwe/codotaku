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
        codotaku::Application app("Codotaku Engine Demo (Multi-Device Multi-GPU Capable)");

        // ---------------------------------------------------------------------
        // 1. CREATE INDEPENDENT LOGICAL DEVICES EXPLICITLY
        // ---------------------------------------------------------------------
        auto dev1 = app.create_device(); // Logical Device 1 (Primary GPU)
        auto dev2 = app.create_device(); // Logical Device 2 (Separate Logical GPU)

        // ---------------------------------------------------------------------
        // 2. INITIALIZE RESOURCES ON DEVICE 1 (DEV1)
        // ---------------------------------------------------------------------
        codotaku::Uploader uploader(*dev1);

        codotaku::GpuBufferArena geometry_arena;
        geometry_arena.init(
            *dev1,
            4 * 1024 * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "Dev1 Static Geometry Arena");

        auto vb_sub = uploader.upload_to_arena(geometry_arena, std::span(CUBE_VERTICES));
        auto ib_sub = uploader.upload_to_arena(geometry_arena, std::span(CUBE_INDICES));

        VkDrawIndirectCommand indirect_cmd{
            .vertexCount = static_cast<uint32_t>(CUBE_INDICES.size()),
            .instanceCount = 1,
            .firstVertex = 0,
            .firstInstance = 0,
        };
        auto cmd_sub = uploader.upload_to_arena(geometry_arena, indirect_cmd);
        uploader.upload();

        codotaku::TextureDesc tex_desc = {
            .name = "procedural_gpu_texture",
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .min_filter = VK_FILTER_LINEAR,
            .mag_filter = VK_FILTER_LINEAR,
        };
        auto checkerboard_tex = codotaku::Texture::create_uninitialized(*dev1, 256, 256, tex_desc);
        auto grid_tex = codotaku::Texture::create_uninitialized(*dev1, 256, 256, tex_desc);

        // Write image and sampler descriptors to DEV1 descriptor heap
        checkerboard_tex.write_to_descriptor_heap(dev1->descriptor_heap());
        grid_tex.write_to_descriptor_heap(dev1->descriptor_heap());

        // Compute Shader Texture Generation on DEV1
        auto compute_pipeline_checker = codotaku::Pipeline{};
        compute_pipeline_checker.init_compute(
            *dev1,
            app.get_slang().compile_source(COMPUTE_TEXTURE_GEN_SLANG, "Dev1 Checker Compute"),
            { codotaku::Pipeline::map_storage_image(0, 0, checkerboard_tex.get_storage_heap_offset()) },
            "Dev1 Checker Compute");

        auto compute_pipeline_grid = codotaku::Pipeline{};
        compute_pipeline_grid.init_compute(
            *dev1,
            app.get_slang().compile_source(COMPUTE_TEXTURE_GEN_SLANG, "Dev1 Grid Compute"),
            { codotaku::Pipeline::map_storage_image(0, 0, grid_tex.get_storage_heap_offset()) },
            "Dev1 Grid Compute");

        dev1->execute_single_time_commands([&](VkCommandBuffer cmd) {
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
            dev1->vkd().vkCmdPipelineBarrier2(cmd, &pre_dep);

            dev1->descriptor_heap().bind(cmd);

            // Dispatch 1: Generate Checkerboard (pattern_type = 0)
            dev1->vkd().vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_checker.get_pipeline());
            struct { uint32_t width, height, pattern_type; float time; } pc0 = { 256, 256, 0, 0.0f };
            dev1->descriptor_heap().push_data(cmd, pc0);
            dev1->vkd().vkCmdDispatch(cmd, 256 / 16, 256 / 16, 1);

            // Dispatch 2: Generate Grid Pattern (pattern_type = 1)
            dev1->vkd().vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_grid.get_pipeline());
            struct { uint32_t width, height, pattern_type; float time; } pc1 = { 256, 256, 1, 0.0f };
            dev1->descriptor_heap().push_data(cmd, pc1);
            dev1->vkd().vkCmdDispatch(cmd, 256 / 16, 256 / 16, 1);

            // Coalesced memory barrier: Ensures compute storage image writes are visible to fragment shader sampling
            VkMemoryBarrier2 compute_to_graphics_barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            };

            VkDependencyInfo post_dep{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .memoryBarrierCount = 1,
                .pMemoryBarriers = &compute_to_graphics_barrier,
            };
            dev1->vkd().vkCmdPipelineBarrier2(cmd, &post_dep);
        });

        compute_pipeline_checker.cleanup();
        compute_pipeline_grid.cleanup();
        std::println("[Main] Successfully generated textures on DEV1 using descriptor heap compute pipeline!");

        // ---------------------------------------------------------------------
        // 3. INITIALIZE RESOURCES ON SEPARATE LOGICAL DEVICE 2 (DEV2)
        // ---------------------------------------------------------------------
        codotaku::Uploader dev2_uploader(*dev2);

        codotaku::GpuBufferArena dev2_geometry_arena;
        dev2_geometry_arena.init(
            *dev2,
            4 * 1024 * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "Dev2 Geometry Arena");

        auto dev2_vb_sub = dev2_uploader.upload_to_arena(dev2_geometry_arena, std::span(CUBE_VERTICES));
        auto dev2_ib_sub = dev2_uploader.upload_to_arena(dev2_geometry_arena, std::span(CUBE_INDICES));
        auto dev2_cmd_sub = dev2_uploader.upload_to_arena(dev2_geometry_arena, indirect_cmd);
        dev2_uploader.upload();

        auto dev2_grid_tex = codotaku::Texture::create_uninitialized(*dev2, 256, 256, tex_desc);
        dev2_grid_tex.write_to_descriptor_heap(dev2->descriptor_heap());

        // DEV2 Compute Texture Generation Pipeline
        auto dev2_compute_pipeline = codotaku::Pipeline{};
        dev2_compute_pipeline.init_compute(
            *dev2,
            app.get_slang().compile_source(COMPUTE_TEXTURE_GEN_SLANG, "Dev2 Compute Pipeline"),
            { codotaku::Pipeline::map_storage_image(0, 0, dev2_grid_tex.get_storage_heap_offset()) },
            "Dev2 Compute Pipeline");

        dev2->execute_single_time_commands([&](VkCommandBuffer cmd) {
            VkImageSubresourceRange range{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };

            VkImageMemoryBarrier2 pre_barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dev2_grid_tex.get_image(),
                .subresourceRange = range,
            };

            VkDependencyInfo pre_dep{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &pre_barrier,
            };
            dev2->vkd().vkCmdPipelineBarrier2(cmd, &pre_dep);

            dev2->descriptor_heap().bind(cmd);

            dev2->vkd().vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dev2_compute_pipeline.get_pipeline());
            struct { uint32_t width, height, pattern_type; float time; } pc = { 256, 256, 1, 0.0f };
            dev2->descriptor_heap().push_data(cmd, pc);
            dev2->vkd().vkCmdDispatch(cmd, 256 / 16, 256 / 16, 1);

            VkMemoryBarrier2 post_barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            };
            VkDependencyInfo post_dep{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .memoryBarrierCount = 1,
                .pMemoryBarriers = &post_barrier,
            };
            dev2->vkd().vkCmdPipelineBarrier2(cmd, &post_dep);
        });

        dev2_compute_pipeline.cleanup();

        // ---------------------------------------------------------------------
        // 4. GRAPHICS PIPELINES FOR DEVICE 1 (DEV1) AND DEVICE 2 (DEV2)
        // ---------------------------------------------------------------------
        auto graphics_pipeline_checker = codotaku::Pipeline{};
        graphics_pipeline_checker.init_dynamic_rendering_bda(
            *dev1,
            app.get_slang().compile_source(GRAPHICS_SHADER_SLANG, "Dev1 Checker Graphics Pipeline"),
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_D32_SFLOAT,
            { codotaku::Pipeline::map_sampled_texture(0, 0, checkerboard_tex.get_sampled_heap_offset(), checkerboard_tex.get_sampler_heap_offset()) },
            VK_CULL_MODE_BACK_BIT,
            VK_FRONT_FACE_COUNTER_CLOCKWISE,
            "Dev1 Checker Graphics Pipeline");

        auto dev2_graphics_pipeline = codotaku::Pipeline{};
        dev2_graphics_pipeline.init_dynamic_rendering_bda(
            *dev2,
            app.get_slang().compile_source(GRAPHICS_SHADER_SLANG, "Dev2 Graphics Pipeline"),
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_D32_SFLOAT,
            { codotaku::Pipeline::map_sampled_texture(0, 0, dev2_grid_tex.get_sampled_heap_offset(), dev2_grid_tex.get_sampler_heap_offset()) },
            VK_CULL_MODE_BACK_BIT,
            VK_FRONT_FACE_COUNTER_CLOCKWISE,
            "Dev2 Graphics Pipeline");

        codotaku::Camera camera({0.0f, 1.4f, 3.2f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 45.0f);

        codotaku::AttachmentDesc depth_desc = {
            .name = "depth_buffer",
            .format = VK_FORMAT_D32_SFLOAT,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        };

        app.set_close_policy(codotaku::WindowClosePolicy::QuitOnLastWindowClose);

        // Window 1 -> Bound to Logical Device 1 (dev1)
        app.create_window({
            .title = "Window 1 (Primary Dev1, Checkerboard)",
            .width = 800,
            .height = 600,
            .buffer_count = 3,
            .present_mode = codotaku::PresentMode::Fifo,
            .attachments = { depth_desc },
            .is_primary = true,
        }, *dev1);

        // Window 2 -> Bound to SEPARATE Logical Device (dev2)!
        auto* win2 = app.create_window({
            .title = "Window 2 (Separate Logical Device Dev2, Grid Pattern)",
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
        }, *dev2);

        // Window 3 -> Bound to Logical Device 1 (dev1) with custom close hook demonstrating live switch_device!
        app.create_window({
            .title = "Window 3 (Primary Dev1, Custom Close Hook)",
            .width = 500,
            .height = 500,
            .buffer_count = 3,
            .present_mode = codotaku::PresentMode::Fifo,
            .attachments = { depth_desc },
            .on_close = [&](codotaku::Window& win) {
                std::println("[Custom Hook] Window '{}' switching to DEV2 live before closing...", win.get_title());
                win.switch_device(*dev2);
                return true;
            },
        }, *dev1);

        // Wait on geometry upload fences right before starting render loop
        uploader.wait();
        dev2_uploader.wait();

        auto start_time = std::chrono::steady_clock::now();

        // Transparent Multi-Device Multi-Window Render Loop!
        int ret = app.run([&](codotaku::Window& window, codotaku::FrameContext& frame) {
            auto now = std::chrono::steady_clock::now();
            float time_sec = std::chrono::duration<float>(now - start_time).count();

            bool is_dev2 = (&frame.device == dev2.get());

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
            scene->tint = is_dev2
                              ? glm::vec3(1.0f, 0.8f, 0.4f)
                              : glm::vec3(1.0f, 1.0f, 1.0f);
            scene->vertexBufferAddress = is_dev2 ? dev2_vb_sub.device_address : vb_sub.device_address;
            scene->indexBufferAddress = is_dev2 ? dev2_ib_sub.device_address : ib_sub.device_address;
            scene->indirectCommandsAddress = is_dev2 ? dev2_cmd_sub.device_address : cmd_sub.device_address;

            // Direct Vulkan Dynamic Rendering pass (operating uniformly in GENERAL layout)
            frame.begin_rendering_with_attachment({.float32 = {0.05f, 0.05f, 0.08f, 1.0f}}, 0, 1.0f);
            frame.set_viewport_and_scissor();

            uint64_t scene_addr = scene_suballoc.device_address;

            if (is_dev2) {
                // Window 2 -> Renders with separate logical device DEV2!
                dev2->descriptor_heap().bind(frame.cmd);
                frame.vkd.vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dev2_graphics_pipeline.get_pipeline());
                dev2->descriptor_heap().push_data(frame.cmd, scene_addr);
                frame.vkd.vkCmdDrawIndirect(frame.cmd, dev2_geometry_arena.get_buffer(), dev2_cmd_sub.offset, 1, sizeof(VkDrawIndirectCommand));
            } else {
                // Windows 1 & 3 -> Render with logical device DEV1!
                dev1->descriptor_heap().bind(frame.cmd);
                frame.vkd.vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_checker.get_pipeline());
                dev1->descriptor_heap().push_data(frame.cmd, scene_addr);
                frame.vkd.vkCmdDrawIndirect(frame.cmd, geometry_arena.get_buffer(), cmd_sub.offset, 1, sizeof(VkDrawIndirectCommand));
            }

            frame.end_rendering();
        });

        checkerboard_tex.cleanup();
        grid_tex.cleanup();
        dev2_grid_tex.cleanup();
        geometry_arena.cleanup(dev1->get_allocator());
        dev2_geometry_arena.cleanup(dev2->get_allocator());
        graphics_pipeline_checker.cleanup();
        dev2_graphics_pipeline.cleanup();
        return ret;

    } catch (const std::exception& e) {
        std::println(std::cerr, "Fatal error: {}", e.what());
        return 1;
    }
}
