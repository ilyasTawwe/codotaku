#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <print>
#include <stdexcept>
#include <unistd.h>

#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <codotaku/vulkan/context.hpp>

namespace codotaku {

namespace {

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

} // namespace

VulkanContext::VulkanContext() {
    init_instance();
    select_physical_device();
    create_logical_device();
    init_vma();
    open_drm_node();
}

VulkanContext::~VulkanContext() {
    cleanup();
}

void VulkanContext::init_instance() {
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize Volk");
    }

    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Codotaku App",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Codotaku Engine",
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
        std::println("[Codotaku] Enabled Vulkan validation layer: {}", validation_layer);
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

    // Enable Synchronization Validation feature
    VkValidationFeatureEnableEXT validation_enables[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
    };

    VkValidationFeaturesEXT validation_features{
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = debug_utils_found ? &debug_create_info : nullptr,
        .enabledValidationFeatureCount = static_cast<uint32_t>(sizeof(validation_enables) / sizeof(validation_enables[0])),
        .pEnabledValidationFeatures = validation_enables,
    };

    void* instance_pnext = nullptr;
    if (validation_found) {
        instance_pnext = &validation_features;
        std::println("[Codotaku] Enabled Synchronization Validation layer feature.");
    } else if (debug_utils_found) {
        instance_pnext = &debug_create_info;
    }

    VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = instance_pnext,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = static_cast<uint32_t>(enabled_layers.size()),
        .ppEnabledLayerNames = enabled_layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size()),
        .ppEnabledExtensionNames = enabled_extensions.data(),
    };

    if (vkCreateInstance(&create_info, nullptr, &m_instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    volkLoadInstance(m_instance);

    if (debug_utils_found && vkCreateDebugUtilsMessengerEXT) {
        vkCreateDebugUtilsMessengerEXT(m_instance, &debug_create_info, nullptr, &m_debug_messenger);
    }
}

void VulkanContext::select_physical_device() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr);
    if (device_count == 0) {
        throw std::runtime_error("No Vulkan capable GPU found");
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data());

    for (const auto& device : devices) {
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

        for (uint32_t i = 0; i < queue_family_count; ++i) {
            if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                m_physical_device = device;
                m_queue_family_index = i;
                break;
            }
        }
        if (m_physical_device != VK_NULL_HANDLE) {
            break;
        }
    }

    if (m_physical_device == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable GPU with Graphics support");
    }

    vkGetPhysicalDeviceMemoryProperties(m_physical_device, &m_memory_properties);

    // Query Physical Device Properties, Descriptor Heap Properties & Alignment Limits
    m_descriptor_heap_properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT,
    };
    VkPhysicalDeviceProperties2 props2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &m_descriptor_heap_properties,
    };
    vkGetPhysicalDeviceProperties2(m_physical_device, &props2);

    m_alignment_limits.min_storage_buffer_offset_alignment = props2.properties.limits.minStorageBufferOffsetAlignment;
    m_alignment_limits.min_uniform_buffer_offset_alignment = props2.properties.limits.minUniformBufferOffsetAlignment;
    m_alignment_limits.min_memory_map_alignment = props2.properties.limits.minMemoryMapAlignment;
    m_alignment_limits.optimal_buffer_copy_offset_alignment = std::max(VkDeviceSize(16), props2.properties.limits.optimalBufferCopyOffsetAlignment);
    m_alignment_limits.optimal_buffer_copy_row_pitch_alignment = std::max(VkDeviceSize(16), props2.properties.limits.optimalBufferCopyRowPitchAlignment);

    std::println("[Codotaku] Selected GPU: {} (Storage Align: {} B, Descriptor Heap: ResAlign {} B / SampAlign {} B)",
        props2.properties.deviceName,
        m_alignment_limits.min_storage_buffer_offset_alignment,
        m_descriptor_heap_properties.resourceHeapAlignment,
        m_descriptor_heap_properties.samplerHeapAlignment);
}

void VulkanContext::create_logical_device() {
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = m_queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const std::vector<const char*> device_extensions = {
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
    };

    VkPhysicalDeviceFeatures features{
        .samplerAnisotropy = VK_TRUE,
        .shaderInt64 = VK_TRUE,
        .shaderInt16 = VK_TRUE,
    };

    VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptor_heap_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT,
        .descriptorHeap = VK_TRUE,
    };

    VkPhysicalDeviceVulkan11Features vulkan11_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &descriptor_heap_features,
        .shaderDrawParameters = VK_TRUE,
    };

    VkPhysicalDeviceVulkan12Features vulkan12_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vulkan11_features,
        .scalarBlockLayout = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE,
    };

    VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR unified_layouts_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR,
        .pNext = &vulkan12_features,
        .unifiedImageLayouts = VK_TRUE,
    };

    VkPhysicalDeviceVulkan13Features vulkan13_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &unified_layouts_features,
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

    if (vkCreateDevice(m_physical_device, &device_create_info, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan logical device with Buffer Device Address");
    }

    volkLoadDevice(m_device);
    vkGetDeviceQueue(m_device, m_queue_family_index, 0, &m_queue);
}

void VulkanContext::init_vma() {
    VmaVulkanFunctions vulkan_functions{};
    VmaAllocatorCreateInfo allocator_info{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = m_physical_device,
        .device = m_device,
        .instance = m_instance,
        .vulkanApiVersion = VK_API_VERSION_1_4,
    };

    if (vmaImportVulkanFunctionsFromVolk(&allocator_info, &vulkan_functions) != VK_SUCCESS) {
        throw std::runtime_error("Failed to import Vulkan functions for VMA from Volk");
    }

    allocator_info.pVulkanFunctions = &vulkan_functions;

    if (vmaCreateAllocator(&allocator_info, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan Memory Allocator (VMA)");
    }
}

void VulkanContext::open_drm_node() {
    m_drm_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (m_drm_fd < 0) {
        m_drm_fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
    }
    if (m_drm_fd < 0) {
        throw std::runtime_error("Failed to open DRM device node");
    }
}

uint32_t VulkanContext::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
    for (uint32_t i = 0; i < m_memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) && (m_memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    for (uint32_t i = 0; i < m_memory_properties.memoryTypeCount; ++i) {
        if (type_filter & (1 << i)) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

VkDeviceAddress VulkanContext::get_buffer_device_address(VkBuffer buffer) const {
    VkBufferDeviceAddressInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer,
    };
    return vkGetBufferDeviceAddress(m_device, &info);
}

void VulkanContext::execute_single_time_commands(const std::function<void(VkCommandBuffer cmd)>& record_fn) const {
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = m_queue_family_index,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(m_device, &pool_info, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create transient command pool for one-time submission");
    }

    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &alloc_info, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, pool, nullptr);
        throw std::runtime_error("Failed to allocate command buffer for one-time submission");
    }

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    record_fn(cmd);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    vkQueueSubmit(m_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);

    vkDestroyCommandPool(m_device, pool, nullptr);
}

void VulkanContext::cleanup() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        if (m_drm_fd >= 0) {
            close(m_drm_fd);
            m_drm_fd = -1;
        }

        if (m_allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_allocator);
            m_allocator = VK_NULL_HANDLE;
        }

        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE) {
        if (m_debug_messenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT) {
            vkDestroyDebugUtilsMessengerEXT(m_instance, m_debug_messenger, nullptr);
            m_debug_messenger = VK_NULL_HANDLE;
        }
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

} // namespace codotaku
