#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <print>
#include <stdexcept>
#include <vector>

#include <volk.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

namespace {

std::atomic<bool> g_interrupted{false};

void signal_handler(int) {
    g_interrupted.store(true);
}

constexpr uint32_t DEFAULT_WIDTH = 800;
constexpr uint32_t DEFAULT_HEIGHT = 600;
constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;

struct WaylandState {
    wl_display* display{nullptr};
    wl_registry* registry{nullptr};
    wl_compositor* compositor{nullptr};
    xdg_wm_base* wm_base{nullptr};
    wl_surface* surface{nullptr};
    xdg_surface* xdg_surface{nullptr};
    xdg_toplevel* xdg_toplevel{nullptr};

    uint32_t width{DEFAULT_WIDTH};
    uint32_t height{DEFAULT_HEIGHT};
    bool configured{false};
    bool running{true};
    bool need_resize{false};
};

struct VulkanState {
    VkInstance instance{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    uint32_t queue_family_index{0};
    VkQueue queue{VK_NULL_HANDLE};

    VkSurfaceFormatKHR surface_format{};
    VkExtent2D extent{};
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    std::vector<VkImage> swapchain_images;

    VkCommandPool command_pool{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> command_buffers;

    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;
    size_t current_frame{0};
};

// XDG WM Base Ping listener
void xdg_wm_base_ping_handler(void* data, xdg_wm_base* wm_base, uint32_t serial) {
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

    if (!wl.compositor || !wl.wm_base) {
        throw std::runtime_error("Failed to bind Wayland compositor or XDG WM Base");
    }

    wl.surface = wl_compositor_create_surface(wl.compositor);
    if (!wl.surface) {
        throw std::runtime_error("Failed to create Wayland surface");
    }

    wl.xdg_surface = xdg_wm_base_get_xdg_surface(wl.wm_base, wl.surface);
    xdg_surface_add_listener(wl.xdg_surface, &surface_listener, &wl);

    wl.xdg_toplevel = xdg_surface_get_toplevel(wl.xdg_surface);
    xdg_toplevel_add_listener(wl.xdg_toplevel, &toplevel_listener, &wl);
    xdg_toplevel_set_title(wl.xdg_toplevel, "Vulkan Wayland Clear Screen (C++26)");
    xdg_toplevel_set_app_id(wl.xdg_toplevel, "codotaku.vulkan.wayland");

    wl_surface_commit(wl.surface);
    wl_display_roundtrip(wl.display);
}

void cleanup_wayland(WaylandState& wl) {
    if (wl.xdg_toplevel) xdg_toplevel_destroy(wl.xdg_toplevel);
    if (wl.xdg_surface) xdg_surface_destroy(wl.xdg_surface);
    if (wl.surface) wl_surface_destroy(wl.surface);
    if (wl.wm_base) xdg_wm_base_destroy(wl.wm_base);
    if (wl.compositor) wl_compositor_destroy(wl.compositor);
    if (wl.registry) wl_registry_destroy(wl.registry);
    if (wl.display) wl_display_disconnect(wl.display);
}

void init_vulkan_instance(VulkanState& vk) {
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize Volk");
    }

    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Vulkan Wayland App",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    const std::vector<const char*> instance_extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data(),
    };

    if (vkCreateInstance(&create_info, nullptr, &vk.instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    volkLoadInstance(vk.instance);
}

void create_vulkan_surface(WaylandState& wl, VulkanState& vk) {
    VkWaylandSurfaceCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = wl.display,
        .surface = wl.surface,
    };

    if (vkCreateWaylandSurfaceKHR(vk.instance, &create_info, nullptr, &vk.surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Wayland Vulkan surface");
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
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, vk.surface, &present_support);

            if ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support) {
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
        throw std::runtime_error("Failed to find a suitable GPU with Graphics & Present support");
    }

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
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkDeviceCreateInfo device_create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
    };

    if (vkCreateDevice(vk.physical_device, &device_create_info, nullptr, &vk.device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan logical device");
    }

    volkLoadDevice(vk.device);
    vkGetDeviceQueue(vk.device, vk.queue_family_index, 0, &vk.queue);
}

void create_swapchain(const WaylandState& wl, VulkanState& vk) {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical_device, vk.surface, &capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, vk.surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, vk.surface, &format_count, formats.data());

    vk.surface_format = formats[0];
    for (const auto& available_format : formats) {
        if (available_format.format == VK_FORMAT_B8G8R8A8_UNORM ||
            available_format.format == VK_FORMAT_R8G8B8A8_UNORM ||
            available_format.format == VK_FORMAT_B8G8R8A8_SRGB ||
            available_format.format == VK_FORMAT_R8G8B8A8_SRGB) {
            vk.surface_format = available_format;
            break;
        }
    }

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical_device, vk.surface, &present_mode_count, nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical_device, vk.surface, &present_mode_count, present_modes.data());

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& available_mode : present_modes) {
        if (available_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = available_mode;
            break;
        }
    }

    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        vk.extent = capabilities.currentExtent;
    } else {
        vk.extent.width = std::clamp(wl.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        vk.extent.height = std::clamp(wl.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk.surface,
        .minImageCount = image_count,
        .imageFormat = vk.surface_format.format,
        .imageColorSpace = vk.surface_format.colorSpace,
        .imageExtent = vk.extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    if (vkCreateSwapchainKHR(vk.device, &create_info, nullptr, &vk.swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan swapchain");
    }

    uint32_t swapchain_image_count = 0;
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &swapchain_image_count, nullptr);
    vk.swapchain_images.resize(swapchain_image_count);
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &swapchain_image_count, vk.swapchain_images.data());
}

void cleanup_swapchain(VulkanState& vk) {
    if (vk.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vk.device, vk.swapchain, nullptr);
        vk.swapchain = VK_NULL_HANDLE;
    }
    vk.swapchain_images.clear();
}

void recreate_swapchain(const WaylandState& wl, VulkanState& vk) {
    vkDeviceWaitIdle(vk.device);
    cleanup_swapchain(vk);
    create_swapchain(wl, vk);
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

    vk.command_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
    };

    if (vkAllocateCommandBuffers(vk.device, &alloc_info, vk.command_buffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }
}

void create_sync_objects(VulkanState& vk) {
    vk.image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    vk.render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    vk.in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphore_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(vk.device, &semaphore_info, nullptr, &vk.image_available_semaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(vk.device, &semaphore_info, nullptr, &vk.render_finished_semaphores[i]) != VK_SUCCESS ||
            vkCreateFence(vk.device, &fence_info, nullptr, &vk.in_flight_fences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects for frame");
        }
    }
}

void render_frame(WaylandState& wl, VulkanState& vk, std::chrono::steady_clock::time_point start_time) {
    vkWaitForFences(vk.device, 1, &vk.in_flight_fences[vk.current_frame], VK_TRUE, UINT64_MAX);

    uint32_t image_index = 0;
    VkResult result = vkAcquireNextImageKHR(
        vk.device,
        vk.swapchain,
        UINT64_MAX,
        vk.image_available_semaphores[vk.current_frame],
        VK_NULL_HANDLE,
        &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain(wl, vk);
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swapchain image");
    }

    vkResetFences(vk.device, 1, &vk.in_flight_fences[vk.current_frame]);

    auto cmd = vk.command_buffers[vk.current_frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    // Calculate dynamic clear color (smooth cyclic color shift)
    auto now = std::chrono::steady_clock::now();
    float time_sec = std::chrono::duration<float>(now - start_time).count();
    VkClearColorValue clear_color = {
        .float32 = {
            0.2f + 0.2f * std::sin(time_sec * 1.5f),
            0.3f + 0.3f * std::sin(time_sec * 1.5f + 2.0f),
            0.6f + 0.3f * std::sin(time_sec * 1.5f + 4.0f),
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

    // Transition image from UNDEFINED to TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier_to_transfer{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk.swapchain_images[image_index],
        .subresourceRange = subresource_range,
    };

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier_to_transfer);

    // Clear the swapchain image using vkCmdClearColorImage
    vkCmdClearColorImage(
        cmd,
        vk.swapchain_images[image_index],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clear_color,
        1,
        &subresource_range);

    // Transition image from TRANSFER_DST_OPTIMAL to PRESENT_SRC_KHR
    VkImageMemoryBarrier barrier_to_present{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk.swapchain_images[image_index],
        .subresourceRange = subresource_range,
    };

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier_to_present);

    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &vk.image_available_semaphores[vk.current_frame],
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &vk.render_finished_semaphores[vk.current_frame],
    };

    if (vkQueueSubmit(vk.queue, 1, &submit_info, vk.in_flight_fences[vk.current_frame]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &vk.render_finished_semaphores[vk.current_frame],
        .swapchainCount = 1,
        .pSwapchains = &vk.swapchain,
        .pImageIndices = &image_index,
    };

    result = vkQueuePresentKHR(vk.queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || wl.need_resize) {
        wl.need_resize = false;
        recreate_swapchain(wl, vk);
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image");
    }

    vk.current_frame = (vk.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void cleanup_vulkan(VulkanState& vk) {
    if (vk.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vk.device);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (i < vk.image_available_semaphores.size() && vk.image_available_semaphores[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(vk.device, vk.image_available_semaphores[i], nullptr);
            }
            if (i < vk.render_finished_semaphores.size() && vk.render_finished_semaphores[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(vk.device, vk.render_finished_semaphores[i], nullptr);
            }
            if (i < vk.in_flight_fences.size() && vk.in_flight_fences[i] != VK_NULL_HANDLE) {
                vkDestroyFence(vk.device, vk.in_flight_fences[i], nullptr);
            }
        }

        if (vk.command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(vk.device, vk.command_pool, nullptr);
        }

        cleanup_swapchain(vk);
        vkDestroyDevice(vk.device, nullptr);
    }

    if (vk.instance != VK_NULL_HANDLE) {
        if (vk.surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(vk.instance, vk.surface, nullptr);
        }
        vkDestroyInstance(vk.instance, nullptr);
    }
}

} // namespace

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        std::println("Starting Vulkan Wayland application (C++26 with volk)...");

        WaylandState wl{};
        init_wayland(wl);

        VulkanState vk{};
        init_vulkan_instance(vk);
        create_vulkan_surface(wl, vk);
        select_physical_device_and_queue(vk);
        create_logical_device(vk);
        create_swapchain(wl, vk);
        create_command_resources(vk);
        create_sync_objects(vk);

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
