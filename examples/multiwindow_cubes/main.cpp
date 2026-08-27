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

const char* CUBE_SLANG_CODE = R"(
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
    // 1. Dereference Root Scene Data buffer via BDA
    SceneData* scene = (SceneData*)pc.sceneDataAddress;

    // 2. Dereference Index & Vertex buffers via BDA
    uint16_t* indices = (uint16_t*)scene.indexBufferAddress;
    Vertex* vertices  = (Vertex*)scene.vertexBufferAddress;

    uint vertexIndex = indices[indexID];
    Vertex input     = vertices[vertexIndex];

    VertexOutput output;
    output.position = mul(float4(input.position, 1.0), scene.mvp);
    output.color    = input.color * scene.tint;
    output.normal   = normalize(mul(input.normal, (float3x3)scene.model));
    return output;
}

[shader("fragment")]
float4 fsMain(VertexOutput input) : SV_Target {
    float3 lightDir = normalize(float3(0.5, 0.8, 0.7));
    float diff = max(dot(input.normal, lightDir), 0.0);
    float3 ambient = 0.3 * input.color;
    float3 diffuse = diff * input.color;
    return float4(ambient + diffuse, 1.0);
}
)";

int main() {
    try {
        codotaku::Application app("Codotaku Engine Demo");

        // Upload geometry to Static Arena
        app.set_mesh_data(CUBE_VERTICES, CUBE_INDICES);

        // Compile Slang shader and initialize pipeline
        app.set_shader_source(CUBE_SLANG_CODE);

        // Create 3 independent Wayland 3D windows with custom styles
        app.create_window({
            .title = "Window 1 (Cyan Theme - Fast)",
            .width = 800,
            .height = 600,
            .rotation_speed = 1.4f,
            .tint = {0.4f, 1.0f, 1.0f}
        });

        app.create_window({
            .title = "Window 2 (Gold Theme - Reverse)",
            .width = 600,
            .height = 600,
            .rotation_speed = -1.0f,
            .tint = {1.0f, 0.75f, 0.3f}
        });

        app.create_window({
            .title = "Window 3 (Purple Theme - Slow)",
            .width = 500,
            .height = 500,
            .rotation_speed = 0.7f,
            .tint = {1.0f, 0.4f, 1.0f}
        });

        return app.run();

    } catch (const std::exception& e) {
        std::println(std::cerr, "Fatal error: {}", e.what());
        return 1;
    }
}
