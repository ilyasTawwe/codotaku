#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <print>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include <drm/drm_fourcc.h>
#include <xf86drm.h>

#include <glm/glm.hpp>

#include <slang.h>
#include <slang-com-ptr.h>

#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <wayland-client.h>

#include "linux-dmabuf-v1-client-protocol.h"
#include "linux-drm-syncobj-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace {

constexpr uint32_t DEFAULT_WIDTH = 800;
constexpr uint32_t DEFAULT_HEIGHT = 600;
constexpr size_t BUFFER_POOL_SIZE = 3;

std::atomic<bool> g_interrupted{false};

void signal_handler(int) {
    g_interrupted.store(true);
}

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;
};

const std::vector<Vertex> TRIANGLE_VERTICES = {
    { {  0.0f, -0.6f }, { 1.0f, 0.0f, 0.0f } }, // Top (Red)
    { {  0.6f,  0.6f }, { 0.0f, 1.0f, 0.0f } }, // Bottom Right (Green)
    { { -0.6f,  0.6f }, { 0.0f, 0.0f, 1.0f } }, // Bottom Left (Blue)
};

const char* TRIANGLE_SLANG_CODE = R"(
struct VertexInput {
    float2 position : POSITION;
    float3 color : COLOR;
};

struct PushConstants {
    float time;
    float scale;
};
[[vk::push_constant]] PushConstants pc;

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
};

[shader("vertex")]
VertexOutput vsMain(VertexInput input) {
    VertexOutput output;
    float cos_t = cos(pc.time);
    float sin_t = sin(pc.time);
    float2 rotated = float2(
        input.position.x * cos_t - input.position.y * sin_t,
        input.position.x * sin_t + input.position.y * cos_t
    ) * pc.scale;

    output.position = float4(rotated, 0.0, 1.0);
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fsMain(VertexOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
)";

struct WaylandState {
    wl_display* display{nullptr};
    wl_registry* registry{nullptr};
    wl_compositor* compositor{nullptr};
    xdg_wm_base* wm_base{nullptr};
    zwp_linux_dmabuf_v1* dmabuf{nullptr};
    wp_linux_drm_syncobj_manager_v1* syncobj_mgr{nullptr};

    wl_surface* surface{nullptr};
    xdg_surface* xdg_surface{nullptr};
    xdg_toplevel* xdg_toplevel{nullptr};
    wp_linux_drm_syncobj_surface_v1* syncobj_surface{nullptr};

    std::vector<uint64_t> supported_modifiers;

    uint32_t width{DEFAULT_WIDTH};
    uint32_t height{DEFAULT_HEIGHT};
    bool configured{false};
    bool running{true};
    bool need_resize{false};
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

struct VertexBufferResource {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
};

struct VulkanState {
    VkInstance instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debug_messenger{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    uint32_t queue_family_index{0};
    VkQueue queue{VK_NULL_HANDLE};
    VkPhysicalDeviceMemoryProperties memory_properties{};

    VmaAllocator allocator{VK_NULL_HANDLE};

    int drm_fd{-1};
    DrmTimeline acquire_timeline{};
    DrmTimeline release_timeline{};

    std::vector<DmaBufBuffer> buffers;
    size_t current_buffer_idx{0};

    VertexBufferResource vertex_buffer{};
    VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};

    VkCommandPool command_pool{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> command_buffers;
    std::vector<VkFence> in_flight_fences;
    std::vector<VkSemaphore> render_complete_semaphores;
};

// XDG WM Base Ping listener
void xdg_wm_base_ping_handler(void*, xdg_wm_base* wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

const xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping_handler,
};

// XDG Surface listener
void xdg_surface_configure_handler(void* data, xdg_surface* surface, uint32_t serial) {
    auto* app = static_cast<WaylandState*>(data);
    xdg_surface_ack_configure(surface, serial);
    app->configured = true;
}

const xdg_surface_listener surface_listener = {
    .configure = xdg_surface_configure_handler,
};

// XDG Toplevel listener
void xdg_toplevel_configure_handler(void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array*) {
    auto* app = static_cast<WaylandState*>(data);
    if (width > 0 && height > 0) {
        if (static_cast<uint32_t>(width) != app->width || static_cast<uint32_t>(height) != app->height) {
            app->width = static_cast<uint32_t>(width);
            app->height = static_cast<uint32_t>(height);
            app->need_resize = true;
        }
    }
}

void xdg_toplevel_close_handler(void* data, xdg_toplevel*) {
    auto* app = static_cast<WaylandState*>(data);
    app->running = false;
}

const xdg_toplevel_listener toplevel_listener = {
    .configure = xdg_toplevel_configure_handler,
    .close = xdg_toplevel_close_handler,
};

// Dma-buf modifier listener
void dmabuf_format_handler(void*, zwp_linux_dmabuf_v1*, uint32_t) {}

void dmabuf_modifier_handler(void* data, zwp_linux_dmabuf_v1*, uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo) {
    auto* app = static_cast<WaylandState*>(data);
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
    auto* app = static_cast<WaylandState*>(data);
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
    }
}

void registry_global_remove_handler(void*, wl_registry*, uint32_t) {}

const wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler,
};

void init_wayland(WaylandState& wl) {
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

    // Roundtrip again to collect dmabuf modifiers
    wl_display_roundtrip(wl.display);

    wl.surface = wl_compositor_create_surface(wl.compositor);
    if (!wl.surface) {
        throw std::runtime_error("Failed to create Wayland surface");
    }

    wl.xdg_surface = xdg_wm_base_get_xdg_surface(wl.wm_base, wl.surface);
    xdg_surface_add_listener(wl.xdg_surface, &surface_listener, &wl);

    wl.xdg_toplevel = xdg_surface_get_toplevel(wl.xdg_surface);
    xdg_toplevel_add_listener(wl.xdg_toplevel, &toplevel_listener, &wl);
    xdg_toplevel_set_title(wl.xdg_toplevel, "Vulkan Dynamic Rendering Slang Triangle (C++26)");
    xdg_toplevel_set_app_id(wl.xdg_toplevel, "codotaku.vulkan.triangle");

    wl.syncobj_surface = wp_linux_drm_syncobj_manager_v1_get_surface(wl.syncobj_mgr, wl.surface);
    if (!wl.syncobj_surface) {
        throw std::runtime_error("Failed to create wp_linux_drm_syncobj_surface_v1");
    }

    wl_surface_commit(wl.surface);
    wl_display_roundtrip(wl.display);
}

void cleanup_wayland(WaylandState& wl) {
    if (wl.syncobj_surface) wp_linux_drm_syncobj_surface_v1_destroy(wl.syncobj_surface);
    if (wl.xdg_toplevel) xdg_toplevel_destroy(wl.xdg_toplevel);
    if (wl.xdg_surface) xdg_surface_destroy(wl.xdg_surface);
    if (wl.surface) wl_surface_destroy(wl.surface);
    if (wl.syncobj_mgr) wp_linux_drm_syncobj_manager_v1_destroy(wl.syncobj_mgr);
    if (wl.dmabuf) zwp_linux_dmabuf_v1_destroy(wl.dmabuf);
    if (wl.wm_base) xdg_wm_base_destroy(wl.wm_base);
    if (wl.compositor) wl_compositor_destroy(wl.compositor);
    if (wl.registry) wl_registry_destroy(wl.registry);
    if (wl.display) wl_display_disconnect(wl.display);
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

void init_vulkan_instance(VulkanState& vk) {
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize Volk");
    }

    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Vulkan Slang Dynamic Rendering Triangle",
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
}

void select_physical_device_and_queue(VulkanState& vk) {
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
}

void create_logical_device(VulkanState& vk) {
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

    VkPhysicalDeviceFeatures features{};

    VkPhysicalDeviceVulkan13Features vulkan13_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
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
        throw std::runtime_error("Failed to create Vulkan logical device with DMA-BUF, Dynamic Rendering & Explicit Sync");
    }

    volkLoadDevice(vk.device);
    vkGetDeviceQueue(vk.device, vk.queue_family_index, 0, &vk.queue);
}

void create_vma_allocator(VulkanState& vk) {
    VmaVulkanFunctions vulkan_functions{};
    VmaAllocatorCreateInfo allocator_info{
        .flags = 0,
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
    std::println("VMA initialized with Volk successfully.");
}

void init_drm_syncobj_timelines(WaylandState& wl, VulkanState& vk) {
    vk.drm_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (vk.drm_fd < 0) {
        vk.drm_fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
    }
    if (vk.drm_fd < 0) {
        throw std::runtime_error("Failed to open DRM device (/dev/dri/renderD128 or /dev/dri/card1)");
    }

    auto init_timeline = [&](DrmTimeline& timeline) {
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

    init_timeline(vk.acquire_timeline);
    init_timeline(vk.release_timeline);
    std::println("DRM syncobj acquire and release timelines initialized successfully.");
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

void create_dmabuf_buffers(WaylandState& wl, VulkanState& vk) {
    vk.buffers.resize(BUFFER_POOL_SIZE);

    std::vector<uint64_t> modifiers = wl.supported_modifiers;
    if (modifiers.empty()) {
        modifiers.push_back(DRM_FORMAT_MOD_LINEAR);
    }

    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        auto& buf = vk.buffers[i];

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
            .extent = { wl.width, wl.height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        if (vkCreateImage(vk.device, &image_info, nullptr, &buf.image) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create DRM format modifier image");
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
            wl.width,
            wl.height,
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

    std::println("Created {} DMA-BUF present buffers ({}x{})", BUFFER_POOL_SIZE, wl.width, wl.height);
}

void cleanup_dmabuf_buffers(VulkanState& vk) {
    for (auto& buf : vk.buffers) {
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
    vk.buffers.clear();
}

void recreate_buffers(WaylandState& wl, VulkanState& vk) {
    vkDeviceWaitIdle(vk.device);
    cleanup_dmabuf_buffers(vk);
    create_dmabuf_buffers(wl, vk);
}

void create_vertex_buffer(VulkanState& vk) {
    VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(Vertex) * TRIANGLE_VERTICES.size(),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VmaAllocationInfo allocation_info{};
    if (vmaCreateBuffer(vk.allocator, &buffer_info, &alloc_info, &vk.vertex_buffer.buffer, &vk.vertex_buffer.allocation, &allocation_info) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create triangle vertex buffer with VMA");
    }

    std::memcpy(allocation_info.pMappedData, TRIANGLE_VERTICES.data(), sizeof(Vertex) * TRIANGLE_VERTICES.size());
    std::println("Created triangle vertex buffer via VMA ({} vertices)", TRIANGLE_VERTICES.size());
}

uint32_t get_vk_format_size(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32_SINT: case VK_FORMAT_R32_UINT: return 4;
        case VK_FORMAT_R32G32_SFLOAT: case VK_FORMAT_R32G32_SINT: case VK_FORMAT_R32G32_UINT: return 8;
        case VK_FORMAT_R32G32B32_SFLOAT: case VK_FORMAT_R32G32B32_SINT: case VK_FORMAT_R32G32B32_UINT: return 12;
        case VK_FORMAT_R32G32B32A32_SFLOAT: case VK_FORMAT_R32G32B32A32_SINT: case VK_FORMAT_R32G32B32A32_UINT: return 16;
        default: return 0;
    }
}

VkFormat slang_type_to_vk_format(slang::TypeReflection* type) {
    auto kind = type->getKind();
    if (kind == slang::TypeReflection::Kind::Scalar) {
        auto scalar = type->getScalarType();
        if (scalar == slang::TypeReflection::ScalarType::Float32) return VK_FORMAT_R32_SFLOAT;
        if (scalar == slang::TypeReflection::ScalarType::Int32) return VK_FORMAT_R32_SINT;
        if (scalar == slang::TypeReflection::ScalarType::UInt32) return VK_FORMAT_R32_UINT;
    } else if (kind == slang::TypeReflection::Kind::Vector) {
        auto scalar = type->getScalarType();
        auto count = type->getElementCount();
        if (scalar == slang::TypeReflection::ScalarType::Float32) {
            if (count == 2) return VK_FORMAT_R32G32_SFLOAT;
            if (count == 3) return VK_FORMAT_R32G32B32_SFLOAT;
            if (count == 4) return VK_FORMAT_R32G32B32A32_SFLOAT;
        } else if (scalar == slang::TypeReflection::ScalarType::Int32) {
            if (count == 2) return VK_FORMAT_R32G32_SINT;
            if (count == 3) return VK_FORMAT_R32G32B32_SINT;
            if (count == 4) return VK_FORMAT_R32G32B32A32_SINT;
        } else if (scalar == slang::TypeReflection::ScalarType::UInt32) {
            if (count == 2) return VK_FORMAT_R32G32_UINT;
            if (count == 3) return VK_FORMAT_R32G32B32_UINT;
            if (count == 4) return VK_FORMAT_R32G32B32A32_UINT;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

struct ReflectedVertexInput {
    VkVertexInputBindingDescription binding{};
    std::vector<VkVertexInputAttributeDescription> attributes;
};

struct ReflectedPipelineLayoutData {
    std::vector<VkPushConstantRange> push_constants;
};

struct CompiledShadersWithReflection {
    std::vector<uint32_t> vs_spirv;
    std::vector<uint32_t> fs_spirv;
    ReflectedVertexInput vertex_input;
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
        session->loadModuleFromSourceString("triangle_shader", "triangle.slang", source, diagnostic_blob.writeRef()));

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

    // 1. Reflect Vertex Input from Vertex Shader Entry Point
    auto vs_layout = layout->findEntryPointByName("vsMain");
    if (vs_layout && vs_layout->getParameterCount() > 0) {
        auto param = vs_layout->getParameterByIndex(0);
        auto type_layout = param->getTypeLayout();
        uint32_t current_offset = 0;

        for (unsigned f = 0; f < type_layout->getFieldCount(); ++f) {
            auto field = type_layout->getFieldByIndex(f);
            auto location = static_cast<uint32_t>(field->getOffset(slang::ParameterCategory::VaryingInput));
            auto format = slang_type_to_vk_format(field->getType());
            auto size = get_vk_format_size(format);

            result.vertex_input.attributes.push_back({
                .location = location,
                .binding = 0,
                .format = format,
                .offset = current_offset,
            });

            std::println("  [Slang Reflection] Vertex Attribute {}: semantic='{}', location={}, offset={}, format={}",
                f, field->getSemanticName() ? field->getSemanticName() : field->getName(),
                location, current_offset, static_cast<int>(format));

            current_offset += size;
        }

        result.vertex_input.binding = {
            .binding = 0,
            .stride = current_offset,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        std::println("  [Slang Reflection] Vertex Input Stride: {} bytes", current_offset);
    }

    // 2. Reflect Push Constants and Global Parameters
    for (unsigned i = 0; i < layout->getParameterCount(); ++i) {
        auto param = layout->getParameterByIndex(i);
        if (param->getCategory() == slang::ParameterCategory::PushConstantBuffer) {
            auto elem_layout = param->getTypeLayout()->getElementTypeLayout();
            uint32_t size = elem_layout ? static_cast<uint32_t>(elem_layout->getSize(slang::ParameterCategory::Uniform)) : 0;
            if (size == 0) {
                auto type_layout = param->getTypeLayout();
                for (unsigned f = 0; f < type_layout->getFieldCount(); ++f) {
                    auto field = type_layout->getFieldByIndex(f);
                    size += get_vk_format_size(slang_type_to_vk_format(field->getType()));
                }
            }

            result.pipeline_layout_data.push_constants.push_back({
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset = 0,
                .size = size,
            });

            std::println("  [Slang Reflection] Push Constant Range: name='{}', size={} bytes",
                param->getName(), size);
        }
    }

    std::println("Slang compiled shaders at runtime: VS {} bytes, FS {} bytes",
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

void create_graphics_pipeline(VulkanState& vk) {
    auto compiled_shaders = compile_slang_shader_source(TRIANGLE_SLANG_CODE);
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

    // Automated Vertex Input from Slang Reflection
    VkPipelineVertexInputStateCreateInfo vertex_input_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &compiled_shaders.vertex_input.binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(compiled_shaders.vertex_input.attributes.size()),
        .pVertexAttributeDescriptions = compiled_shaders.vertex_input.attributes.data(),
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
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
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

    // Automated Pipeline Layout from Slang Reflection
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
    VkPipelineRenderingCreateInfo pipeline_rendering_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
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
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = vk.pipeline_layout,
        .renderPass = VK_NULL_HANDLE,
    };

    if (vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &vk.pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline for dynamic rendering");
    }

    vkDestroyShaderModule(vk.device, fs_module, nullptr);
    vkDestroyShaderModule(vk.device, vs_module, nullptr);
    std::println("Graphics pipeline created via Slang Reflection automation.");
}

void create_command_resources(VulkanState& vk) {
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.queue_family_index,
    };

    if (vkCreateCommandPool(vk.device, &pool_info, nullptr, &vk.command_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan command pool");
    }

    vk.command_buffers.resize(BUFFER_POOL_SIZE);
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(BUFFER_POOL_SIZE),
    };

    if (vkAllocateCommandBuffers(vk.device, &alloc_info, vk.command_buffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    vk.in_flight_fences.resize(BUFFER_POOL_SIZE);
    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        if (vkCreateFence(vk.device, &fence_info, nullptr, &vk.in_flight_fences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create in-flight fence");
        }
    }

    vk.render_complete_semaphores.resize(BUFFER_POOL_SIZE);
    VkExportSemaphoreCreateInfo export_sem_info{
        .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    VkSemaphoreCreateInfo sem_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &export_sem_info,
    };
    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        if (vkCreateSemaphore(vk.device, &sem_info, nullptr, &vk.render_complete_semaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create persistent exportable semaphore");
        }
    }
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

void render_frame(WaylandState& wl, VulkanState& vk, std::chrono::steady_clock::time_point start_time) {
    if (wl.need_resize) {
        wl.need_resize = false;
        recreate_buffers(wl, vk);
    }

    auto& buf = vk.buffers[vk.current_buffer_idx];

    // Explicit sync: Wait for the compositor to release this specific buffer before rendering to it again
    if (buf.last_release_point > 0) {
        uint32_t release_handle = vk.release_timeline.handle;
        uint64_t release_point = buf.last_release_point;
        drmSyncobjTimelineWait(vk.drm_fd, &release_handle, &release_point, 1, 1000000000ULL, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, nullptr);
    }

    vkWaitForFences(vk.device, 1, &vk.in_flight_fences[vk.current_buffer_idx], VK_TRUE, UINT64_MAX);
    vkResetFences(vk.device, 1, &vk.in_flight_fences[vk.current_buffer_idx]);

    auto cmd = vk.command_buffers[vk.current_buffer_idx];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    auto now = std::chrono::steady_clock::now();
    float time_sec = std::chrono::duration<float>(now - start_time).count();
    VkClearColorValue clear_color = {
        .float32 = {
            0.1f + 0.05f * std::sin(time_sec),
            0.1f + 0.05f * std::sin(time_sec + 2.0f),
            0.15f + 0.05f * std::sin(time_sec + 4.0f),
            1.0f
        }
    };

    VkImageSubresourceRange subresource_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    // Synchronization 2: Transition from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL
    VkImageMemoryBarrier2 barrier_to_attachment{
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
        .subresourceRange = subresource_range,
    };

    VkDependencyInfo dep_to_attachment{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier_to_attachment,
    };

    vkCmdPipelineBarrier2(cmd, &dep_to_attachment);

    // Dynamic Rendering: vkCmdBeginRendering / vkCmdEndRendering with color attachment
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

    VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {
            .offset = {0, 0},
            .extent = {wl.width, wl.height},
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
    };

    vkCmdBeginRendering(cmd, &rendering_info);

    // Draw RGB Triangle with Reflected Push Constants
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline);

    struct ShaderPushConstants {
        float time;
        float scale;
    } pc = {
        .time = time_sec,
        .scale = 0.8f + 0.15f * std::sin(time_sec * 2.0f),
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
        .width = static_cast<float>(wl.width),
        .height = static_cast<float>(wl.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = {wl.width, wl.height},
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &vk.vertex_buffer.buffer, offsets);

    vkCmdDraw(cmd, static_cast<uint32_t>(TRIANGLE_VERTICES.size()), 1, 0, 0);

    vkCmdEndRendering(cmd);

    // Synchronization 2: Transition from COLOR_ATTACHMENT_OPTIMAL to GENERAL
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
        .subresourceRange = subresource_range,
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
        .pSignalSemaphores = &vk.render_complete_semaphores[vk.current_buffer_idx],
    };

    if (vkQueueSubmit(vk.queue, 1, &submit_info, vk.in_flight_fences[vk.current_buffer_idx]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    // Export sync_file from Vulkan semaphore
    VkSemaphoreGetFdInfoKHR get_sem_fd_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore = vk.render_complete_semaphores[vk.current_buffer_idx],
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };

    int sync_file_fd = -1;
    if (vkGetSemaphoreFdKHR(vk.device, &get_sem_fd_info, &sync_file_fd) != VK_SUCCESS || sync_file_fd < 0) {
        throw std::runtime_error("Failed to export sync file fd from semaphore");
    }

    // Attach sync_file to DRM acquire timeline
    timeline_attach_sync_fd(vk.drm_fd, vk.acquire_timeline, sync_file_fd);

    // Advance release timeline point for this buffer
    vk.release_timeline.point++;
    buf.last_release_point = vk.release_timeline.point;

    // Set explicit sync points on Wayland surface
    wp_linux_drm_syncobj_surface_v1_set_acquire_point(
        wl.syncobj_surface,
        vk.acquire_timeline.wtimeline,
        vk.acquire_timeline.point >> 32,
        vk.acquire_timeline.point & 0xffffffff);

    wp_linux_drm_syncobj_surface_v1_set_release_point(
        wl.syncobj_surface,
        vk.release_timeline.wtimeline,
        vk.release_timeline.point >> 32,
        vk.release_timeline.point & 0xffffffff);

    wl_surface_attach(wl.surface, buf.wbuffer, 0, 0);
    wl_surface_damage_buffer(wl.surface, 0, 0, wl.width, wl.height);
    wl_surface_commit(wl.surface);

    vk.current_buffer_idx = (vk.current_buffer_idx + 1) % BUFFER_POOL_SIZE;
}

void cleanup_vulkan(VulkanState& vk) {
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

        if (vk.vertex_buffer.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(vk.allocator, vk.vertex_buffer.buffer, vk.vertex_buffer.allocation);
            vk.vertex_buffer.buffer = VK_NULL_HANDLE;
            vk.vertex_buffer.allocation = VK_NULL_HANDLE;
        }

        for (auto fence : vk.in_flight_fences) {
            if (fence != VK_NULL_HANDLE) vkDestroyFence(vk.device, fence, nullptr);
        }
        vk.in_flight_fences.clear();

        for (auto sem : vk.render_complete_semaphores) {
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(vk.device, sem, nullptr);
        }
        vk.render_complete_semaphores.clear();

        if (vk.command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(vk.device, vk.command_pool, nullptr);
            vk.command_pool = VK_NULL_HANDLE;
        }

        cleanup_dmabuf_buffers(vk);

        if (vk.acquire_timeline.wtimeline) {
            wp_linux_drm_syncobj_timeline_v1_destroy(vk.acquire_timeline.wtimeline);
            vk.acquire_timeline.wtimeline = nullptr;
        }
        if (vk.acquire_timeline.handle != 0) {
            drmSyncobjDestroy(vk.drm_fd, vk.acquire_timeline.handle);
            vk.acquire_timeline.handle = 0;
        }

        if (vk.release_timeline.wtimeline) {
            wp_linux_drm_syncobj_timeline_v1_destroy(vk.release_timeline.wtimeline);
            vk.release_timeline.wtimeline = nullptr;
        }
        if (vk.release_timeline.handle != 0) {
            drmSyncobjDestroy(vk.drm_fd, vk.release_timeline.handle);
            vk.release_timeline.handle = 0;
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
        std::println("Starting Vulkan Slang Triangle Wayland application (C++26)...");

        WaylandState wl{};
        init_wayland(wl);

        VulkanState vk{};
        init_vulkan_instance(vk);
        select_physical_device_and_queue(vk);
        create_logical_device(vk);
        create_vma_allocator(vk);
        init_drm_syncobj_timelines(wl, vk);
        create_dmabuf_buffers(wl, vk);
        create_vertex_buffer(vk);
        create_graphics_pipeline(vk);
        create_command_resources(vk);

        std::println("Initialization successful. Entering render loop...");
        std::fflush(stdout);
        auto start_time = std::chrono::steady_clock::now();

        while (wl.running && !g_interrupted.load()) {
            while (wl_display_prepare_read(wl.display) != 0) {
                wl_display_dispatch_pending(wl.display);
            }
            wl_display_flush(wl.display);

            struct pollfd pfd = {
                .fd = wl_display_get_fd(wl.display),
                .events = POLLIN,
                .revents = 0,
            };

            int timeout_ms = wl.configured ? 0 : 50;
            int ret = poll(&pfd, 1, timeout_ms);
            if (ret > 0) {
                wl_display_read_events(wl.display);
                wl_display_dispatch_pending(wl.display);
            } else {
                wl_display_cancel_read(wl.display);
            }

            if (!wl.running || g_interrupted.load()) {
                break;
            }

            if (wl.configured) {
                render_frame(wl, vk, start_time);
            }
        }

        std::println("Shutting down...");
        cleanup_vulkan(vk);
        cleanup_wayland(wl);
        std::println("Goodbye!");

    } catch (const std::exception& e) {
        std::println(stderr, "Fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
