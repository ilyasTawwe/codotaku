#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <poll.h>
#include <print>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include <drm/drm_fourcc.h>
#include <xf86drm.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <slang.h>
#include <slang-com-ptr.h>

#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <wayland-client.h>

#include "linux-dmabuf-v1-client-protocol.h"
#include "linux-drm-syncobj-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace {

constexpr size_t BUFFER_POOL_SIZE = 3;
constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

std::atomic<bool> g_interrupted{false};

void signal_handler(int) {
    g_interrupted.store(true);
}

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec3 normal;
};

// 24 vertices for 6 faces with crisp per-face normals
const std::vector<Vertex> CUBE_VERTICES = {
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

const char* CUBE_SLANG_BDA_CODE = R"(
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

struct SceneData {
    glm::mat4 mvp;
    glm::mat4 model;
    glm::vec3 tint;
    float _pad{0.0f};
    uint64_t vertexBufferAddress{0};
    uint64_t indexBufferAddress{0};
};

struct WaylandContext {
    wl_display* display{nullptr};
    wl_registry* registry{nullptr};
    wl_compositor* compositor{nullptr};
    xdg_wm_base* wm_base{nullptr};
    zwp_linux_dmabuf_v1* dmabuf{nullptr};
    wp_linux_drm_syncobj_manager_v1* syncobj_mgr{nullptr};
    zxdg_decoration_manager_v1* decoration_mgr{nullptr};

    std::vector<uint64_t> supported_modifiers;
};

struct DrmTimeline {
    uint32_t handle{0};
    uint64_t point{0};
    wp_linux_drm_syncobj_timeline_v1* wtimeline{nullptr};
};

struct DmaBufBuffer {
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    int dmabuf_fd{-1};
    wl_buffer* wbuffer{nullptr};
    uint64_t last_release_point{0};
};

struct DepthBuffer {
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
};

struct BufferResource {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VkDeviceAddress device_address{0};
    void* mapped_data{nullptr};
};

struct VulkanContext {
    VkInstance instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debug_messenger{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    uint32_t queue_family_index{0};
    VkQueue queue{VK_NULL_HANDLE};
    VkPhysicalDeviceMemoryProperties memory_properties{};

    VmaAllocator allocator{VK_NULL_HANDLE};
    int drm_fd{-1};

    BufferResource vertex_buffer{};
    BufferResource index_buffer{};
    VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
};

VkDeviceAddress get_buffer_address(VkDevice device, VkBuffer buffer) {
    VkBufferDeviceAddressInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer,
    };
    return vkGetBufferDeviceAddress(device, &info);
}

uint32_t find_memory_type(const VkPhysicalDeviceMemoryProperties& mem_props, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if (type_filter & (1 << i)) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

void timeline_attach_sync_fd(int drm_fd, DrmTimeline& timeline, int sync_fd) {
    uint32_t temp_obj = 0;
    if (drmSyncobjCreate(drm_fd, 0, &temp_obj) != 0) {
        close(sync_fd);
        throw std::runtime_error("Failed to create temporary syncobj");
    }

    if (drmSyncobjImportSyncFile(drm_fd, temp_obj, sync_fd) != 0) {
        drmSyncobjDestroy(drm_fd, temp_obj);
        close(sync_fd);
        throw std::runtime_error("Failed to import sync file into DRM syncobj");
    }

    if (drmSyncobjTransfer(drm_fd, timeline.handle, timeline.point + 1, temp_obj, 0, 0) != 0) {
        drmSyncobjDestroy(drm_fd, temp_obj);
        close(sync_fd);
        throw std::runtime_error("Failed to transfer DRM syncobj to timeline point");
    }

    timeline.point++;
    drmSyncobjDestroy(drm_fd, temp_obj);
    close(sync_fd);
}

class AppWindow {
public:
    AppWindow(WaylandContext& wl, VulkanContext& vk, std::string title, uint32_t width, uint32_t height, float rotation_speed, glm::vec3 tint)
        : m_title(std::move(title)), m_width(width), m_height(height), m_rotation_speed(rotation_speed), m_tint(tint) {
        init_wayland_surface(wl);
        init_drm_syncobj_timelines(wl, vk);
        create_dmabuf_buffers(wl, vk);
        create_depth_buffer(vk);
        create_scene_buffers(vk);
        create_command_resources(vk);
    }

    ~AppWindow() = default;

    void cleanup(VulkanContext& vk) {
        if (vk.device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(vk.device);

            for (auto fence : m_in_flight_fences) {
                if (fence != VK_NULL_HANDLE) vkDestroyFence(vk.device, fence, nullptr);
            }
            m_in_flight_fences.clear();

            for (auto sem : m_render_complete_semaphores) {
                if (sem != VK_NULL_HANDLE) vkDestroySemaphore(vk.device, sem, nullptr);
            }
            m_render_complete_semaphores.clear();

            if (m_command_pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(vk.device, m_command_pool, nullptr);
                m_command_pool = VK_NULL_HANDLE;
            }

            cleanup_scene_buffers(vk);
            cleanup_depth_buffer(vk);
            cleanup_dmabuf_buffers(vk);

            if (m_acquire_timeline.wtimeline) {
                wp_linux_drm_syncobj_timeline_v1_destroy(m_acquire_timeline.wtimeline);
                m_acquire_timeline.wtimeline = nullptr;
            }
            if (m_acquire_timeline.handle != 0 && vk.drm_fd >= 0) {
                drmSyncobjDestroy(vk.drm_fd, m_acquire_timeline.handle);
                m_acquire_timeline.handle = 0;
            }

            if (m_release_timeline.wtimeline) {
                wp_linux_drm_syncobj_timeline_v1_destroy(m_release_timeline.wtimeline);
                m_release_timeline.wtimeline = nullptr;
            }
            if (m_release_timeline.handle != 0 && vk.drm_fd >= 0) {
                drmSyncobjDestroy(vk.drm_fd, m_release_timeline.handle);
                m_release_timeline.handle = 0;
            }
        }

        if (m_syncobj_surface) {
            wp_linux_drm_syncobj_surface_v1_destroy(m_syncobj_surface);
            m_syncobj_surface = nullptr;
        }
        if (m_toplevel_decoration) {
            zxdg_toplevel_decoration_v1_destroy(m_toplevel_decoration);
            m_toplevel_decoration = nullptr;
        }
        if (m_xdg_toplevel) {
            xdg_toplevel_destroy(m_xdg_toplevel);
            m_xdg_toplevel = nullptr;
        }
        if (m_xdg_surface) {
            xdg_surface_destroy(m_xdg_surface);
            m_xdg_surface = nullptr;
        }
        if (m_surface) {
            wl_surface_destroy(m_surface);
            m_surface = nullptr;
        }
    }

    bool is_open() const { return m_open; }
    bool is_configured() const { return m_configured; }

    void handle_configure(uint32_t width, uint32_t height) {
        if (width > 0 && height > 0) {
            if (width != m_width || height != m_height) {
                m_width = width;
                m_height = height;
                m_need_resize = true;
            }
        }
        m_configured = true;
    }

    void handle_close() {
        m_open = false;
        std::println("Window '{}' closed by user.", m_title);
    }

    void render_frame(WaylandContext& wl, VulkanContext& vk, std::chrono::steady_clock::time_point start_time) {
        if (!m_open || !m_configured) return;

        if (m_need_resize) {
            m_need_resize = false;
            recreate_buffers(wl, vk);
        }

        auto& buf = m_buffers[m_current_buffer_idx];

        // Explicit sync: Wait for the compositor to release this specific buffer
        if (buf.last_release_point > 0) {
            uint32_t release_handle = m_release_timeline.handle;
            uint64_t release_point = buf.last_release_point;
            drmSyncobjTimelineWait(vk.drm_fd, &release_handle, &release_point, 1, 1000000000ULL, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, nullptr);
        }

        vkWaitForFences(vk.device, 1, &m_in_flight_fences[m_current_buffer_idx], VK_TRUE, UINT64_MAX);
        vkResetFences(vk.device, 1, &m_in_flight_fences[m_current_buffer_idx]);

        auto cmd = m_command_buffers[m_current_buffer_idx];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        vkBeginCommandBuffer(cmd, &begin_info);

        auto now = std::chrono::steady_clock::now();
        float time_sec = std::chrono::duration<float>(now - start_time).count();
        VkClearColorValue clear_color = {
            .float32 = {
                0.06f + 0.03f * std::sin(time_sec),
                0.06f + 0.03f * std::sin(time_sec + 2.0f),
                0.09f + 0.03f * std::sin(time_sec + 4.0f),
                1.0f
            }
        };

        VkImageSubresourceRange color_subresource_range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        VkImageSubresourceRange depth_subresource_range{
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        // Synchronization 2: Transition Color and Depth attachments
        VkImageMemoryBarrier2 barriers[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = buf.image,
                .subresourceRange = color_subresource_range,
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = m_depth.image,
                .subresourceRange = depth_subresource_range,
            }
        };

        VkDependencyInfo dep_pre_render{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 2,
            .pImageMemoryBarriers = barriers,
        };

        vkCmdPipelineBarrier2(cmd, &dep_pre_render);

        // Dynamic Rendering pass
        VkRenderingAttachmentInfo color_attachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = buf.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {
                .color = clear_color,
            },
        };

        VkRenderingAttachmentInfo depth_attachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = m_depth.view,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = {
                .depthStencil = { 1.0f, 0 },
            },
        };

        VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = {0, 0},
                .extent = {m_width, m_height},
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment,
            .pDepthAttachment = &depth_attachment,
        };

        vkCmdBeginRendering(cmd, &rendering_info);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline);

        // 3D Matrix Computation
        float aspect = (m_height > 0) ? (static_cast<float>(m_width) / static_cast<float>(m_height)) : 1.0f;
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 1.2f, 2.8f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::rotate(model, time_sec * m_rotation_speed * 0.9f, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, time_sec * m_rotation_speed * 0.6f, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, time_sec * m_rotation_speed * 0.3f, glm::vec3(0.0f, 0.0f, 1.0f));

        glm::mat4 mvp = proj * view * model;

        // 1. Update Mapped SceneData Root Buffer
        SceneData scene_data{
            .mvp = mvp,
            .model = model,
            .tint = m_tint,
            ._pad = 0.0f,
            .vertexBufferAddress = vk.vertex_buffer.device_address,
            .indexBufferAddress = vk.index_buffer.device_address,
        };
        std::memcpy(m_scene_buffers[m_current_buffer_idx].mapped_data, &scene_data, sizeof(SceneData));

        // 2. Push only 8-byte 64-bit Root Buffer GPU address!
        struct ShaderPushConstants {
            uint64_t sceneDataAddress;
        } pc = {
            .sceneDataAddress = m_scene_buffers[m_current_buffer_idx].device_address,
        };

        vkCmdPushConstants(
            cmd,
            vk.pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(ShaderPushConstants),
            &pc);

        VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(m_width),
            .height = static_cast<float>(m_height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{
            .offset = {0, 0},
            .extent = {m_width, m_height},
        };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // 3. 100% Bindless Draw! (Vertices and indices are pulled directly in shader via BDA)
        vkCmdDraw(cmd, static_cast<uint32_t>(CUBE_INDICES.size()), 1, 0, 0);

        vkCmdEndRendering(cmd);

        // Synchronization 2: Transition Color to GENERAL
        VkImageMemoryBarrier2 barrier_to_general{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = buf.image,
            .subresourceRange = color_subresource_range,
        };

        VkDependencyInfo dep_to_general{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier_to_general,
        };

        vkCmdPipelineBarrier2(cmd, &dep_to_general);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &m_render_complete_semaphores[m_current_buffer_idx],
        };

        if (vkQueueSubmit(vk.queue, 1, &submit_info, m_in_flight_fences[m_current_buffer_idx]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit draw command buffer");
        }

        // Export sync_file from Vulkan semaphore
        VkSemaphoreGetFdInfoKHR get_sem_fd_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
            .semaphore = m_render_complete_semaphores[m_current_buffer_idx],
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        };

        int sync_file_fd = -1;
        if (vkGetSemaphoreFdKHR(vk.device, &get_sem_fd_info, &sync_file_fd) != VK_SUCCESS || sync_file_fd < 0) {
            throw std::runtime_error("Failed to export sync file fd from semaphore");
        }

        // Attach sync_file to DRM acquire timeline
        timeline_attach_sync_fd(vk.drm_fd, m_acquire_timeline, sync_file_fd);

        // Advance release timeline point for this buffer
        m_release_timeline.point++;
        buf.last_release_point = m_release_timeline.point;

        // Set explicit sync points on Wayland surface
        wp_linux_drm_syncobj_surface_v1_set_acquire_point(
            m_syncobj_surface,
            m_acquire_timeline.wtimeline,
            m_acquire_timeline.point >> 32,
            m_acquire_timeline.point & 0xffffffff);

        wp_linux_drm_syncobj_surface_v1_set_release_point(
            m_syncobj_surface,
            m_release_timeline.wtimeline,
            m_release_timeline.point >> 32,
            m_release_timeline.point & 0xffffffff);

        wl_surface_attach(m_surface, buf.wbuffer, 0, 0);
        wl_surface_damage_buffer(m_surface, 0, 0, m_width, m_height);
        wl_surface_commit(m_surface);

        m_current_buffer_idx = (m_current_buffer_idx + 1) % BUFFER_POOL_SIZE;
    }

private:
    void init_wayland_surface(WaylandContext& wl);
    void init_drm_syncobj_timelines(WaylandContext& wl, VulkanContext& vk);
    void create_dmabuf_buffers(WaylandContext& wl, VulkanContext& vk);
    void cleanup_dmabuf_buffers(VulkanContext& vk);
    void create_depth_buffer(VulkanContext& vk);
    void cleanup_depth_buffer(VulkanContext& vk);
    void create_scene_buffers(VulkanContext& vk);
    void cleanup_scene_buffers(VulkanContext& vk);
    void recreate_buffers(WaylandContext& wl, VulkanContext& vk);
    void create_command_resources(VulkanContext& vk);

    std::string m_title;
    uint32_t m_width{800};
    uint32_t m_height{600};
    float m_rotation_speed{1.0f};
    glm::vec3 m_tint{1.0f, 1.0f, 1.0f};

    bool m_configured{false};
    bool m_open{true};
    bool m_need_resize{false};

    wl_surface* m_surface{nullptr};
    xdg_surface* m_xdg_surface{nullptr};
    xdg_toplevel* m_xdg_toplevel{nullptr};
    zxdg_toplevel_decoration_v1* m_toplevel_decoration{nullptr};
    wp_linux_drm_syncobj_surface_v1* m_syncobj_surface{nullptr};

    DrmTimeline m_acquire_timeline{};
    DrmTimeline m_release_timeline{};

    std::vector<DmaBufBuffer> m_buffers;
    DepthBuffer m_depth{};
    std::vector<BufferResource> m_scene_buffers;
    size_t m_current_buffer_idx{0};

    VkCommandPool m_command_pool{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> m_command_buffers;
    std::vector<VkFence> m_in_flight_fences;
    std::vector<VkSemaphore> m_render_complete_semaphores;
};

// XDG Surface listener
void xdg_surface_configure_handler(void* data, xdg_surface* surface, uint32_t serial) {
    auto* win = static_cast<AppWindow*>(data);
    xdg_surface_ack_configure(surface, serial);
    win->handle_configure(0, 0);
}

const xdg_surface_listener surface_listener = {
    .configure = xdg_surface_configure_handler,
};

// XDG Toplevel listener
void xdg_toplevel_configure_handler(void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array*) {
    auto* win = static_cast<AppWindow*>(data);
    if (width > 0 && height > 0) {
        win->handle_configure(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }
}

void xdg_toplevel_close_handler(void* data, xdg_toplevel*) {
    auto* win = static_cast<AppWindow*>(data);
    win->handle_close();
}

const xdg_toplevel_listener toplevel_listener = {
    .configure = xdg_toplevel_configure_handler,
    .close = xdg_toplevel_close_handler,
};

// XDG Toplevel Decoration listener
void toplevel_decoration_configure_handler(void*, zxdg_toplevel_decoration_v1*, uint32_t) {}

const zxdg_toplevel_decoration_v1_listener decoration_listener = {
    .configure = toplevel_decoration_configure_handler,
};

// XDG WM Base Ping listener
void xdg_wm_base_ping_handler(void*, xdg_wm_base* wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

const xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping_handler,
};

// Dma-buf modifier listener
void dmabuf_format_handler(void*, zwp_linux_dmabuf_v1*, uint32_t) {}

void dmabuf_modifier_handler(void* data, zwp_linux_dmabuf_v1*, uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo) {
    auto* app = static_cast<WaylandContext*>(data);
    if (format == DRM_FORMAT_ARGB8888 || format == DRM_FORMAT_XRGB8888) {
        uint64_t mod = (static_cast<uint64_t>(modifier_hi) << 32) | modifier_lo;
        if (mod != DRM_FORMAT_MOD_INVALID) {
            app->supported_modifiers.push_back(mod);
        }
    }
}

const zwp_linux_dmabuf_v1_listener dmabuf_listener = {
    .format = dmabuf_format_handler,
    .modifier = dmabuf_modifier_handler,
};

// Wayland Registry listener
void registry_global_handler(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto* app = static_cast<WaylandContext*>(data);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 1u)));
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    } else if (std::strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0) {
        app->dmabuf = static_cast<zwp_linux_dmabuf_v1*>(
            wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, std::min(version, 3u)));
        zwp_linux_dmabuf_v1_add_listener(app->dmabuf, &dmabuf_listener, app);
    } else if (std::strcmp(interface, wp_linux_drm_syncobj_manager_v1_interface.name) == 0) {
        app->syncobj_mgr = static_cast<wp_linux_drm_syncobj_manager_v1*>(
            wl_registry_bind(registry, name, &wp_linux_drm_syncobj_manager_v1_interface, 1));
    } else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        app->decoration_mgr = static_cast<zxdg_decoration_manager_v1*>(
            wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
    }
}

void registry_global_remove_handler(void*, wl_registry*, uint32_t) {}

const wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler,
};

void init_wayland_context(WaylandContext& wl) {
    wl.display = wl_display_connect(nullptr);
    if (!wl.display) {
        throw std::runtime_error("Failed to connect to Wayland display server");
    }

    wl.registry = wl_display_get_registry(wl.display);
    wl_registry_add_listener(wl.registry, &registry_listener, &wl);
    wl_display_roundtrip(wl.display);

    if (!wl.compositor || !wl.wm_base || !wl.dmabuf || !wl.syncobj_mgr) {
        throw std::runtime_error("Compositor missing required interfaces: wl_compositor, xdg_wm_base, zwp_linux_dmabuf_v1, or wp_linux_drm_syncobj_manager_v1");
    }

    wl_display_roundtrip(wl.display);
}

void cleanup_wayland_context(WaylandContext& wl) {
    if (wl.decoration_mgr) zxdg_decoration_manager_v1_destroy(wl.decoration_mgr);
    if (wl.syncobj_mgr) wp_linux_drm_syncobj_manager_v1_destroy(wl.syncobj_mgr);
    if (wl.dmabuf) zwp_linux_dmabuf_v1_destroy(wl.dmabuf);
    if (wl.wm_base) xdg_wm_base_destroy(wl.wm_base);
    if (wl.compositor) wl_compositor_destroy(wl.compositor);
    if (wl.registry) wl_registry_destroy(wl.registry);
    if (wl.display) wl_display_disconnect(wl.display);
}

void AppWindow::init_wayland_surface(WaylandContext& wl) {
    m_surface = wl_compositor_create_surface(wl.compositor);
    if (!m_surface) {
        throw std::runtime_error("Failed to create Wayland surface for window: " + m_title);
    }

    m_xdg_surface = xdg_wm_base_get_xdg_surface(wl.wm_base, m_surface);
    xdg_surface_add_listener(m_xdg_surface, &surface_listener, this);

    m_xdg_toplevel = xdg_surface_get_toplevel(m_xdg_surface);
    xdg_toplevel_add_listener(m_xdg_toplevel, &toplevel_listener, this);
    xdg_toplevel_set_title(m_xdg_toplevel, m_title.c_str());
    xdg_toplevel_set_app_id(m_xdg_toplevel, "codotaku.vulkan.multiwindow");
    xdg_toplevel_set_min_size(m_xdg_toplevel, 100, 100);

    if (wl.decoration_mgr) {
        m_toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
            wl.decoration_mgr, m_xdg_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(m_toplevel_decoration, &decoration_listener, this);
        zxdg_toplevel_decoration_v1_set_mode(
            m_toplevel_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    m_syncobj_surface = wp_linux_drm_syncobj_manager_v1_get_surface(wl.syncobj_mgr, m_surface);
    if (!m_syncobj_surface) {
        throw std::runtime_error("Failed to create wp_linux_drm_syncobj_surface_v1 for window: " + m_title);
    }

    wl_surface_commit(m_surface);
    wl_display_roundtrip(wl.display);
}

void AppWindow::init_drm_syncobj_timelines(WaylandContext& wl, VulkanContext& vk) {
    auto init_tl = [&](DrmTimeline& timeline) {
        if (drmSyncobjCreate(vk.drm_fd, 0, &timeline.handle) != 0) {
            throw std::runtime_error("Failed to create DRM syncobj");
        }

        int fd = -1;
        if (drmSyncobjHandleToFD(vk.drm_fd, timeline.handle, &fd) != 0 || fd < 0) {
            throw std::runtime_error("Failed to export DRM syncobj to fd");
        }

        timeline.wtimeline = wp_linux_drm_syncobj_manager_v1_import_timeline(wl.syncobj_mgr, fd);
        close(fd);

        if (!timeline.wtimeline) {
            throw std::runtime_error("Failed to import timeline into Wayland syncobj manager");
        }
        timeline.point = 0;
    };

    init_tl(m_acquire_timeline);
    init_tl(m_release_timeline);
}

void AppWindow::create_dmabuf_buffers(WaylandContext& wl, VulkanContext& vk) {
    m_buffers.resize(BUFFER_POOL_SIZE);

    std::vector<uint64_t> modifiers = wl.supported_modifiers;
    if (modifiers.empty()) {
        modifiers.push_back(DRM_FORMAT_MOD_LINEAR);
    }

    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        auto& buf = m_buffers[i];

        VkExternalMemoryImageCreateInfo external_img_info{
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };

        VkImageDrmFormatModifierListCreateInfoEXT mod_list{
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
            .pNext = &external_img_info,
            .drmFormatModifierCount = static_cast<uint32_t>(modifiers.size()),
            .pDrmFormatModifiers = modifiers.data(),
        };

        VkImageCreateInfo image_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = &mod_list,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .extent = { m_width, m_height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        if (vkCreateImage(vk.device, &image_info, nullptr, &buf.image) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create DRM format modifier image for window: " + m_title);
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(vk.device, buf.image, &mem_reqs);

        uint32_t mem_type_index = find_memory_type(
            vk.memory_properties,
            mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkMemoryDedicatedAllocateInfo dedicated_alloc_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .image = buf.image,
            .buffer = VK_NULL_HANDLE,
        };

        VkExportMemoryAllocateInfo export_alloc_info{
            .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
            .pNext = &dedicated_alloc_info,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };

        VkMemoryAllocateInfo alloc_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = &export_alloc_info,
            .allocationSize = mem_reqs.size,
            .memoryTypeIndex = mem_type_index,
        };

        if (vkAllocateMemory(vk.device, &alloc_info, nullptr, &buf.memory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate external DMA-BUF memory");
        }

        if (vkBindImageMemory(vk.device, buf.image, buf.memory, 0) != VK_SUCCESS) {
            throw std::runtime_error("Failed to bind image memory");
        }

        VkMemoryGetFdInfoKHR get_fd_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
            .memory = buf.memory,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };

        if (vkGetMemoryFdKHR(vk.device, &get_fd_info, &buf.dmabuf_fd) != VK_SUCCESS || buf.dmabuf_fd < 0) {
            throw std::runtime_error("Failed to export DMA-BUF fd from Vulkan memory");
        }

        VkImageDrmFormatModifierPropertiesEXT mod_props{
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT,
        };
        if (vkGetImageDrmFormatModifierPropertiesEXT(vk.device, buf.image, &mod_props) != VK_SUCCESS) {
            throw std::runtime_error("Failed to query image DRM format modifier properties");
        }

        VkImageSubresource subresource{
            .aspectMask = VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT,
            .mipLevel = 0,
            .arrayLayer = 0,
        };
        VkSubresourceLayout layout{};
        vkGetImageSubresourceLayout(vk.device, buf.image, &subresource, &layout);

        zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(wl.dmabuf);
        zwp_linux_buffer_params_v1_add(
            params,
            buf.dmabuf_fd,
            0,
            layout.offset,
            layout.rowPitch,
            mod_props.drmFormatModifier >> 32,
            mod_props.drmFormatModifier & 0xffffffff);

        buf.wbuffer = zwp_linux_buffer_params_v1_create_immed(
            params,
            m_width,
            m_height,
            DRM_FORMAT_ARGB8888,
            0);
        zwp_linux_buffer_params_v1_destroy(params);

        if (!buf.wbuffer) {
            throw std::runtime_error("Failed to create wl_buffer via zwp_linux_dmabuf_v1_create_immed");
        }

        VkImageViewCreateInfo view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = buf.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        if (vkCreateImageView(vk.device, &view_info, nullptr, &buf.view) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image view for DMA-BUF buffer");
        }

        buf.last_release_point = 0;
    }

    std::println("Window '{}': Created {} DMA-BUF present buffers ({}x{})", m_title, BUFFER_POOL_SIZE, m_width, m_height);
}

void AppWindow::create_depth_buffer(VulkanContext& vk) {
    VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = DEPTH_FORMAT,
        .extent = { m_width, m_height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_info{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    if (vmaCreateImage(vk.allocator, &image_info, &alloc_info, &m_depth.image, &m_depth.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate depth image via VMA for window: " + m_title);
    }

    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_depth.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = DEPTH_FORMAT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    if (vkCreateImageView(vk.device, &view_info, nullptr, &m_depth.view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view for window: " + m_title);
    }
}

void AppWindow::cleanup_depth_buffer(VulkanContext& vk) {
    if (m_depth.view != VK_NULL_HANDLE) {
        vkDestroyImageView(vk.device, m_depth.view, nullptr);
        m_depth.view = VK_NULL_HANDLE;
    }
    if (m_depth.image != VK_NULL_HANDLE) {
        vmaDestroyImage(vk.allocator, m_depth.image, m_depth.allocation);
        m_depth.image = VK_NULL_HANDLE;
        m_depth.allocation = VK_NULL_HANDLE;
    }
}

void AppWindow::create_scene_buffers(VulkanContext& vk) {
    m_scene_buffers.resize(BUFFER_POOL_SIZE);
    VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(SceneData),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        VmaAllocationInfo allocation_info{};
        if (vmaCreateBuffer(vk.allocator, &buffer_info, &alloc_info, &m_scene_buffers[i].buffer, &m_scene_buffers[i].allocation, &allocation_info) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate SceneData root buffer via VMA");
        }
        m_scene_buffers[i].mapped_data = allocation_info.pMappedData;
        m_scene_buffers[i].device_address = get_buffer_address(vk.device, m_scene_buffers[i].buffer);
    }
}

void AppWindow::cleanup_scene_buffers(VulkanContext& vk) {
    for (auto& buf : m_scene_buffers) {
        if (buf.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(vk.allocator, buf.buffer, buf.allocation);
            buf.buffer = VK_NULL_HANDLE;
            buf.allocation = VK_NULL_HANDLE;
        }
    }
    m_scene_buffers.clear();
}

void AppWindow::cleanup_dmabuf_buffers(VulkanContext& vk) {
    for (auto& buf : m_buffers) {
        if (buf.wbuffer) {
            wl_buffer_destroy(buf.wbuffer);
            buf.wbuffer = nullptr;
        }
        if (buf.view != VK_NULL_HANDLE) {
            vkDestroyImageView(vk.device, buf.view, nullptr);
            buf.view = VK_NULL_HANDLE;
        }
        if (buf.dmabuf_fd >= 0) {
            close(buf.dmabuf_fd);
            buf.dmabuf_fd = -1;
        }
        if (buf.image != VK_NULL_HANDLE) {
            vkDestroyImage(vk.device, buf.image, nullptr);
            buf.image = VK_NULL_HANDLE;
        }
        if (buf.memory != VK_NULL_HANDLE) {
            vkFreeMemory(vk.device, buf.memory, nullptr);
            buf.memory = VK_NULL_HANDLE;
        }
    }
    m_buffers.clear();
}

void AppWindow::recreate_buffers(WaylandContext& wl, VulkanContext& vk) {
    vkDeviceWaitIdle(vk.device);
    cleanup_depth_buffer(vk);
    cleanup_dmabuf_buffers(vk);
    create_dmabuf_buffers(wl, vk);
    create_depth_buffer(vk);
    m_current_buffer_idx = 0;
}

void AppWindow::create_command_resources(VulkanContext& vk) {
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.queue_family_index,
    };

    if (vkCreateCommandPool(vk.device, &pool_info, nullptr, &m_command_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan command pool for window: " + m_title);
    }

    m_command_buffers.resize(BUFFER_POOL_SIZE);
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(BUFFER_POOL_SIZE),
    };

    if (vkAllocateCommandBuffers(vk.device, &alloc_info, m_command_buffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers for window: " + m_title);
    }

    m_in_flight_fences.resize(BUFFER_POOL_SIZE);
    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        if (vkCreateFence(vk.device, &fence_info, nullptr, &m_in_flight_fences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create in-flight fence");
        }
    }

    m_render_complete_semaphores.resize(BUFFER_POOL_SIZE);
    VkExportSemaphoreCreateInfo export_sem_info{
        .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    VkSemaphoreCreateInfo sem_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &export_sem_info,
    };
    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        if (vkCreateSemaphore(vk.device, &sem_info, nullptr, &m_render_complete_semaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create persistent exportable semaphore");
        }
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::println(stderr, "[Vulkan Error]: {}", callback_data->pMessage);
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::println(stderr, "[Vulkan Warning]: {}", callback_data->pMessage);
    }
    return VK_FALSE;
}

void init_vulkan_context(VulkanContext& vk) {
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize Volk");
    }

    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Vulkan 3D Cube Multi-Window App (BDA Vertex Pulling)",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    std::vector<const char*> enabled_layers;
    const char* validation_layer = "VK_LAYER_KHRONOS_validation";
    bool validation_found = std::ranges::any_of(available_layers, [&](const auto& layer) {
        return std::strcmp(layer.layerName, validation_layer) == 0;
    });

    if (validation_found) {
        enabled_layers.push_back(validation_layer);
        std::println("Enabled Vulkan validation layer: {}", validation_layer);
    }

    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> available_extensions(ext_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, available_extensions.data());

    std::vector<const char*> enabled_extensions;
    bool debug_utils_found = std::ranges::any_of(available_extensions, [&](const auto& ext) {
        return std::strcmp(ext.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
    });

    if (debug_utils_found) {
        enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };

    VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = debug_utils_found ? &debug_create_info : nullptr,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = static_cast<uint32_t>(enabled_layers.size()),
        .ppEnabledLayerNames = enabled_layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size()),
        .ppEnabledExtensionNames = enabled_extensions.data(),
    };

    if (vkCreateInstance(&create_info, nullptr, &vk.instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    volkLoadInstance(vk.instance);

    if (debug_utils_found && vkCreateDebugUtilsMessengerEXT) {
        vkCreateDebugUtilsMessengerEXT(vk.instance, &debug_create_info, nullptr, &vk.debug_messenger);
    }

    // Pick GPU
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(vk.instance, &device_count, nullptr);
    if (device_count == 0) {
        throw std::runtime_error("No Vulkan capable GPU found");
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(vk.instance, &device_count, devices.data());

    for (const auto& device : devices) {
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

        for (uint32_t i = 0; i < queue_family_count; ++i) {
            if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                vk.physical_device = device;
                vk.queue_family_index = i;
                break;
            }
        }
        if (vk.physical_device != VK_NULL_HANDLE) {
            break;
        }
    }

    if (vk.physical_device == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable GPU with Graphics support");
    }

    vkGetPhysicalDeviceMemoryProperties(vk.physical_device, &vk.memory_properties);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk.physical_device, &props);
    std::println("Using GPU: {}", props.deviceName);

    // Create Logical Device with Buffer Device Address, Synchronization 2, and Dynamic Rendering
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const std::vector<const char*> device_extensions = {
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
    };

    VkPhysicalDeviceFeatures features{
        .shaderInt64 = VK_TRUE,
        .shaderInt16 = VK_TRUE,
    };

    VkPhysicalDeviceVulkan11Features vulkan11_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .shaderDrawParameters = VK_TRUE,
    };

    VkPhysicalDeviceVulkan12Features vulkan12_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vulkan11_features,
        .scalarBlockLayout = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE,
    };

    VkPhysicalDeviceVulkan13Features vulkan13_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &vulkan12_features,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    VkDeviceCreateInfo device_create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan13_features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures = &features,
    };

    if (vkCreateDevice(vk.physical_device, &device_create_info, nullptr, &vk.device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan logical device with Buffer Device Address");
    }

    volkLoadDevice(vk.device);
    vkGetDeviceQueue(vk.device, vk.queue_family_index, 0, &vk.queue);

    // Initialize VMA with Buffer Device Address support
    VmaVulkanFunctions vulkan_functions{};
    VmaAllocatorCreateInfo allocator_info{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = vk.physical_device,
        .device = vk.device,
        .instance = vk.instance,
        .vulkanApiVersion = VK_API_VERSION_1_4,
    };

    if (vmaImportVulkanFunctionsFromVolk(&allocator_info, &vulkan_functions) != VK_SUCCESS) {
        throw std::runtime_error("Failed to import Vulkan functions for VMA from Volk");
    }

    allocator_info.pVulkanFunctions = &vulkan_functions;

    if (vmaCreateAllocator(&allocator_info, &vk.allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan Memory Allocator (VMA)");
    }
    std::println("VMA initialized with Volk and Buffer Device Address successfully.");

    // Open DRM Node
    vk.drm_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (vk.drm_fd < 0) {
        vk.drm_fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
    }
    if (vk.drm_fd < 0) {
        throw std::runtime_error("Failed to open DRM device (/dev/dri/renderD128 or /dev/dri/card1)");
    }
}

void create_shared_mesh_buffers(VulkanContext& vk) {
    // 1. Vertex Buffer (Storage Buffer with BDA)
    VkBufferCreateInfo vb_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(Vertex) * CUBE_VERTICES.size(),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VmaAllocationInfo vb_alloc_info{};
    if (vmaCreateBuffer(vk.allocator, &vb_info, &alloc_info, &vk.vertex_buffer.buffer, &vk.vertex_buffer.allocation, &vb_alloc_info) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create 3D cube vertex buffer with VMA");
    }
    std::memcpy(vb_alloc_info.pMappedData, CUBE_VERTICES.data(), sizeof(Vertex) * CUBE_VERTICES.size());
    vk.vertex_buffer.device_address = get_buffer_address(vk.device, vk.vertex_buffer.buffer);

    // 2. Index Buffer (Storage Buffer with BDA)
    VkBufferCreateInfo ib_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(uint16_t) * CUBE_INDICES.size(),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationInfo ib_alloc_info{};
    if (vmaCreateBuffer(vk.allocator, &ib_info, &alloc_info, &vk.index_buffer.buffer, &vk.index_buffer.allocation, &ib_alloc_info) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create 3D cube index buffer with VMA");
    }
    std::memcpy(ib_alloc_info.pMappedData, CUBE_INDICES.data(), sizeof(uint16_t) * CUBE_INDICES.size());
    vk.index_buffer.device_address = get_buffer_address(vk.device, vk.index_buffer.buffer);

    std::println("Created shared 3D cube mesh (VB Address: {:#x}, IB Address: {:#x})",
        vk.vertex_buffer.device_address, vk.index_buffer.device_address);
}

struct ReflectedPipelineLayoutData {
    std::vector<VkPushConstantRange> push_constants;
};

struct CompiledShadersWithReflection {
    std::vector<uint32_t> vs_spirv;
    std::vector<uint32_t> fs_spirv;
    ReflectedPipelineLayoutData pipeline_layout_data;
};

CompiledShadersWithReflection compile_slang_shader_source(const char* source) {
    Slang::ComPtr<slang::IGlobalSession> global_session;
    if (SLANG_FAILED(slang::createGlobalSession(global_session.writeRef()))) {
        throw std::runtime_error("Failed to create Slang global session");
    }

    slang::SessionDesc session_desc = {};
    slang::TargetDesc target_desc = {};
    target_desc.format = SLANG_SPIRV;
    target_desc.profile = global_session->findProfile("spirv_1_5");
    session_desc.targets = &target_desc;
    session_desc.targetCount = 1;

    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(global_session->createSession(session_desc, session.writeRef()))) {
        throw std::runtime_error("Failed to create Slang session");
    }

    Slang::ComPtr<slang::IBlob> diagnostic_blob;
    Slang::ComPtr<slang::IModule> module(
        session->loadModuleFromSourceString("cube_bda_shader", "cube_bda.slang", source, diagnostic_blob.writeRef()));

    if (!module) {
        std::string err = diagnostic_blob ? static_cast<const char*>(diagnostic_blob->getBufferPointer()) : "Unknown Slang error";
        throw std::runtime_error("Slang compilation failed: " + err);
    }

    Slang::ComPtr<slang::IEntryPoint> vs_entry;
    module->findEntryPointByName("vsMain", vs_entry.writeRef());

    Slang::ComPtr<slang::IEntryPoint> fs_entry;
    module->findEntryPointByName("fsMain", fs_entry.writeRef());

    slang::IComponentType* components[] = { module.get(), vs_entry.get(), fs_entry.get() };
    Slang::ComPtr<slang::IComponentType> program;
    session->createCompositeComponentType(components, 3, program.writeRef(), diagnostic_blob.writeRef());

    Slang::ComPtr<slang::IComponentType> linked_program;
    program->link(linked_program.writeRef(), diagnostic_blob.writeRef());

    Slang::ComPtr<slang::IBlob> vs_blob;
    linked_program->getEntryPointCode(0, 0, vs_blob.writeRef(), diagnostic_blob.writeRef());

    Slang::ComPtr<slang::IBlob> fs_blob;
    linked_program->getEntryPointCode(1, 0, fs_blob.writeRef(), diagnostic_blob.writeRef());

    CompiledShadersWithReflection result{};
    auto copy_blob = [](slang::IBlob* blob, std::vector<uint32_t>& out) {
        size_t size_bytes = blob->getBufferSize();
        out.resize(size_bytes / sizeof(uint32_t));
        std::memcpy(out.data(), blob->getBufferPointer(), size_bytes);
    };

    copy_blob(vs_blob.get(), result.vs_spirv);
    copy_blob(fs_blob.get(), result.fs_spirv);

    auto layout = linked_program->getLayout();

    // Reflect Push Constants (8-byte root buffer pointer)
    for (unsigned i = 0; i < layout->getParameterCount(); ++i) {
        auto param = layout->getParameterByIndex(i);
        if (param->getCategory() == slang::ParameterCategory::PushConstantBuffer) {
            uint32_t size = sizeof(uint64_t); // 8 bytes for 64-bit BDA pointer

            result.pipeline_layout_data.push_constants.push_back({
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset = 0,
                .size = size,
            });

            std::println("  [Slang Reflection] Reflected BDA Root Push Constant Range: size={} bytes", size);
        }
    }

    std::println("Slang compiled BDA shaders: VS {} bytes, FS {} bytes",
        result.vs_spirv.size() * sizeof(uint32_t),
        result.fs_spirv.size() * sizeof(uint32_t));

    return result;
}

VkShaderModule create_shader_module(VkDevice device, const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size() * sizeof(uint32_t),
        .pCode = spirv.data(),
    };
    VkShaderModule module{VK_NULL_HANDLE};
    if (vkCreateShaderModule(device, &create_info, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }
    return module;
}

void create_shared_graphics_pipeline(VulkanContext& vk) {
    auto compiled_shaders = compile_slang_shader_source(CUBE_SLANG_BDA_CODE);
    VkShaderModule vs_module = create_shader_module(vk.device, compiled_shaders.vs_spirv);
    VkShaderModule fs_module = create_shader_module(vk.device, compiled_shaders.fs_spirv);

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs_module,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fs_module,
            .pName = "main",
        },
    };

    // Programmable Vertex Pulling: Completely empty vertex input state!
    VkPipelineVertexInputStateCreateInfo vertex_input_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment{
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo color_blending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };

    const auto& push_constants = compiled_shaders.pipeline_layout_data.push_constants;
    VkPipelineLayoutCreateInfo pipeline_layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = static_cast<uint32_t>(push_constants.size()),
        .pPushConstantRanges = push_constants.data(),
    };

    if (vkCreatePipelineLayout(vk.device, &pipeline_layout_info, nullptr, &vk.pipeline_layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    VkFormat color_format = VK_FORMAT_B8G8R8A8_UNORM;
    VkFormat depth_format = DEPTH_FORMAT;
    VkPipelineRenderingCreateInfo pipeline_rendering_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
        .depthAttachmentFormat = depth_format,
    };

    VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipeline_rendering_info,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = vk.pipeline_layout,
        .renderPass = VK_NULL_HANDLE,
    };

    if (vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &vk.pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline for 3D dynamic rendering with BDA");
    }

    vkDestroyShaderModule(vk.device, fs_module, nullptr);
    vkDestroyShaderModule(vk.device, vs_module, nullptr);
    std::println("Shared 3D graphics pipeline created with Programmable Vertex Pulling.");
}

void cleanup_vulkan_context(VulkanContext& vk) {
    if (vk.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vk.device);

        if (vk.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(vk.device, vk.pipeline, nullptr);
            vk.pipeline = VK_NULL_HANDLE;
        }

        if (vk.pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(vk.device, vk.pipeline_layout, nullptr);
            vk.pipeline_layout = VK_NULL_HANDLE;
        }

        if (vk.index_buffer.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(vk.allocator, vk.index_buffer.buffer, vk.index_buffer.allocation);
            vk.index_buffer.buffer = VK_NULL_HANDLE;
            vk.index_buffer.allocation = VK_NULL_HANDLE;
        }

        if (vk.vertex_buffer.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(vk.allocator, vk.vertex_buffer.buffer, vk.vertex_buffer.allocation);
            vk.vertex_buffer.buffer = VK_NULL_HANDLE;
            vk.vertex_buffer.allocation = VK_NULL_HANDLE;
        }

        if (vk.drm_fd >= 0) {
            close(vk.drm_fd);
            vk.drm_fd = -1;
        }

        if (vk.allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(vk.allocator);
            vk.allocator = VK_NULL_HANDLE;
        }

        vkDestroyDevice(vk.device, nullptr);
        vk.device = VK_NULL_HANDLE;
    }

    if (vk.instance != VK_NULL_HANDLE) {
        if (vk.debug_messenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT) {
            vkDestroyDebugUtilsMessengerEXT(vk.instance, vk.debug_messenger, nullptr);
            vk.debug_messenger = VK_NULL_HANDLE;
        }
        vkDestroyInstance(vk.instance, nullptr);
        vk.instance = VK_NULL_HANDLE;
    }
}

} // namespace

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        std::println("Starting Vulkan 3D Cube Multi-Window App (Buffer Device Address Vertex Pulling)...");

        WaylandContext wl{};
        init_wayland_context(wl);

        VulkanContext vk{};
        init_vulkan_context(vk);
        create_shared_mesh_buffers(vk);
        create_shared_graphics_pipeline(vk);

        // Spawn 3 independent 3D rotating cube windows
        std::vector<std::unique_ptr<AppWindow>> windows;

        // Window 1: 800x600, fast rotating cube with vibrant cyan/blue tint
        windows.push_back(std::make_unique<AppWindow>(
            wl, vk, "Window 1 (3D BDA - Cyan)", 800, 600, 1.4f, glm::vec3(0.5f, 1.0f, 1.0f)));

        // Window 2: 600x600, reverse rotating cube with warm golden/orange tint
        windows.push_back(std::make_unique<AppWindow>(
            wl, vk, "Window 2 (3D BDA - Gold)", 600, 600, -1.0f, glm::vec3(1.0f, 0.75f, 0.3f)));

        // Window 3: 500x500, slow rotating cube with magenta/purple tint
        windows.push_back(std::make_unique<AppWindow>(
            wl, vk, "Window 3 (3D BDA - Purple)", 500, 500, 0.7f, glm::vec3(1.0f, 0.4f, 1.0f)));

        std::println("Created {} independent 3D BDA Wayland windows. Entering main event loop...", windows.size());
        std::fflush(stdout);
        auto start_time = std::chrono::steady_clock::now();

        while (!windows.empty() && !g_interrupted.load()) {
            while (wl_display_prepare_read(wl.display) != 0) {
                wl_display_dispatch_pending(wl.display);
            }
            wl_display_flush(wl.display);

            struct pollfd pfd = {
                .fd = wl_display_get_fd(wl.display),
                .events = POLLIN,
                .revents = 0,
            };

            int ret = poll(&pfd, 1, 10);
            if (ret > 0) {
                wl_display_read_events(wl.display);
                wl_display_dispatch_pending(wl.display);
            } else {
                wl_display_cancel_read(wl.display);
            }

            if (g_interrupted.load()) {
                break;
            }

            // Render each active window independently
            for (auto& win : windows) {
                if (win->is_open() && win->is_configured()) {
                    win->render_frame(wl, vk, start_time);
                }
            }

            // Clean up and remove closed windows dynamically
            for (auto it = windows.begin(); it != windows.end();) {
                if (!(*it)->is_open()) {
                    (*it)->cleanup(vk);
                    it = windows.erase(it);
                } else {
                    ++it;
                }
            }
        }

        std::println("Shutting down windows and context...");
        for (auto& win : windows) {
            win->cleanup(vk);
        }
        windows.clear();

        cleanup_vulkan_context(vk);
        cleanup_wayland_context(wl);
        std::println("Goodbye!");

    } catch (const std::exception& e) {
        std::println(stderr, "Fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
