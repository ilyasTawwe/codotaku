#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <print>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <xf86drm.h>

#include <codotaku/system/log.hpp>
#include <codotaku/vulkan/device.hpp>

namespace codotaku {

VulkanDevice::VulkanDevice(VulkanInstance& instance, VkPhysicalDevice preferred_gpu) {
    select_physical_device(instance, preferred_gpu);
    setup_drm_and_gbm();
    create_logical_device(instance);
    init_vma(instance);
}

VulkanDevice::~VulkanDevice() {
    cleanup();
}

VulkanDevice::VulkanDevice(VulkanDevice&& other) noexcept
    : m_physical_device(std::exchange(other.m_physical_device, VK_NULL_HANDLE)),
      m_device(std::exchange(other.m_device, VK_NULL_HANDLE)),
      m_vkd(other.m_vkd),
      m_pfnSetDebugUtilsObjectNameEXT(other.m_pfnSetDebugUtilsObjectNameEXT),
      m_pfnCmdBeginDebugUtilsLabelEXT(other.m_pfnCmdBeginDebugUtilsLabelEXT),
      m_pfnCmdEndDebugUtilsLabelEXT(other.m_pfnCmdEndDebugUtilsLabelEXT),
      m_pfnCmdInsertDebugUtilsLabelEXT(other.m_pfnCmdInsertDebugUtilsLabelEXT),
      m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE)),
      m_graphics_queue(std::exchange(other.m_graphics_queue, {})),
      m_transfer_queue(std::exchange(other.m_transfer_queue, {})),
      m_compute_queue(std::exchange(other.m_compute_queue, {})),
      m_video_decode_queue(std::exchange(other.m_video_decode_queue, {})),
      m_video_encode_queue(std::exchange(other.m_video_encode_queue, {})),
      m_memory_properties(other.m_memory_properties),
      m_alignment_limits(other.m_alignment_limits),
      m_descriptor_heap_properties(other.m_descriptor_heap_properties),
      m_drm_properties(other.m_drm_properties),
      m_drm_fd(std::exchange(other.m_drm_fd, -1)),
      m_gbm_device(std::exchange(other.m_gbm_device, nullptr)),
      m_drm_node_path(std::move(other.m_drm_node_path)) {}

VulkanDevice& VulkanDevice::operator=(VulkanDevice&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_physical_device = std::exchange(other.m_physical_device, VK_NULL_HANDLE);
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_vkd = other.m_vkd;
        m_pfnSetDebugUtilsObjectNameEXT = other.m_pfnSetDebugUtilsObjectNameEXT;
        m_pfnCmdBeginDebugUtilsLabelEXT = other.m_pfnCmdBeginDebugUtilsLabelEXT;
        m_pfnCmdEndDebugUtilsLabelEXT = other.m_pfnCmdEndDebugUtilsLabelEXT;
        m_pfnCmdInsertDebugUtilsLabelEXT = other.m_pfnCmdInsertDebugUtilsLabelEXT;
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_graphics_queue = std::exchange(other.m_graphics_queue, {});
        m_transfer_queue = std::exchange(other.m_transfer_queue, {});
        m_compute_queue = std::exchange(other.m_compute_queue, {});
        m_video_decode_queue = std::exchange(other.m_video_decode_queue, {});
        m_video_encode_queue = std::exchange(other.m_video_encode_queue, {});
        m_memory_properties = other.m_memory_properties;
        m_alignment_limits = other.m_alignment_limits;
        m_descriptor_heap_properties = other.m_descriptor_heap_properties;
        m_drm_properties = other.m_drm_properties;
        m_drm_fd = std::exchange(other.m_drm_fd, -1);
        m_gbm_device = std::exchange(other.m_gbm_device, nullptr);
        m_drm_node_path = std::move(other.m_drm_node_path);
    }
    return *this;
}

void VulkanDevice::select_physical_device(VulkanInstance& instance, VkPhysicalDevice preferred_gpu) {
    auto devices = instance.enumerate_physical_devices();
    if (devices.empty()) {
        throw std::runtime_error("No Vulkan capable GPU found");
    }

    if (preferred_gpu != VK_NULL_HANDLE) {
        m_physical_device = preferred_gpu;
    } else {
        // Choose discrete GPU with graphics support first
        VkPhysicalDevice fallback_gpu = VK_NULL_HANDLE;
        for (auto pdev : devices) {
            VkPhysicalDeviceProperties props;
            instance.vki().vkGetPhysicalDeviceProperties(pdev, &props);

            uint32_t qf_count = 0;
            instance.vki().vkGetPhysicalDeviceQueueFamilyProperties(pdev, &qf_count, nullptr);
            std::vector<VkQueueFamilyProperties> qf_props(qf_count);
            instance.vki().vkGetPhysicalDeviceQueueFamilyProperties(pdev, &qf_count, qf_props.data());

            bool has_graphics = false;
            for (const auto& qf : qf_props) {
                if (qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    has_graphics = true;
                    break;
                }
            }

            if (has_graphics) {
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    m_physical_device = pdev;
                    break;
                }
                if (fallback_gpu == VK_NULL_HANDLE) {
                    fallback_gpu = pdev;
                }
            }
        }
        if (m_physical_device == VK_NULL_HANDLE) {
            m_physical_device = fallback_gpu;
        }
    }

    if (m_physical_device == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable GPU with Graphics support");
    }

    instance.vki().vkGetPhysicalDeviceMemoryProperties(m_physical_device, &m_memory_properties);

    // Query Physical Device Properties, DRM properties, Descriptor Heap Properties & Alignment Limits
    m_drm_properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
    };
    m_descriptor_heap_properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT,
        .pNext = &m_drm_properties,
    };
    VkPhysicalDeviceProperties2 props2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &m_descriptor_heap_properties,
    };
    instance.vki().vkGetPhysicalDeviceProperties2(m_physical_device, &props2);

    m_alignment_limits.min_storage_buffer_offset_alignment = props2.properties.limits.minStorageBufferOffsetAlignment;
    m_alignment_limits.min_uniform_buffer_offset_alignment = props2.properties.limits.minUniformBufferOffsetAlignment;
    m_alignment_limits.min_memory_map_alignment = props2.properties.limits.minMemoryMapAlignment;
    m_alignment_limits.optimal_buffer_copy_offset_alignment = std::max(VkDeviceSize(16), props2.properties.limits.optimalBufferCopyOffsetAlignment);
    m_alignment_limits.optimal_buffer_copy_row_pitch_alignment = std::max(VkDeviceSize(16), props2.properties.limits.optimalBufferCopyRowPitchAlignment);

    log_info("[VulkanDevice] Selected GPU: {} (Type: {}, Storage Align: {} B, Optimal Copy Align: {} B)",
        props2.properties.deviceName,
        static_cast<uint32_t>(props2.properties.deviceType),
        m_alignment_limits.min_storage_buffer_offset_alignment,
        m_alignment_limits.optimal_buffer_copy_offset_alignment);
}

void VulkanDevice::setup_drm_and_gbm() {
    if (m_drm_properties.hasRender) {
        dev_t target_render_dev = makedev(static_cast<uint32_t>(m_drm_properties.renderMajor),
                                          static_cast<uint32_t>(m_drm_properties.renderMinor));
        drmDevicePtr devices[64];
        int num_devs = drmGetDevices2(0, devices, 64);
        if (num_devs > 0) {
            for (int i = 0; i < num_devs; ++i) {
                if (devices[i]->available_nodes & (1 << DRM_NODE_RENDER)) {
                    struct stat st{};
                    if (stat(devices[i]->nodes[DRM_NODE_RENDER], &st) == 0 && st.st_rdev == target_render_dev) {
                        m_drm_node_path = devices[i]->nodes[DRM_NODE_RENDER];
                        break;
                    }
                }
            }
            drmFreeDevices(devices, num_devs);
        }
    }

    if (m_drm_node_path.empty()) {
        m_drm_node_path = "/dev/dri/renderD128";
    }

    m_drm_fd = open(m_drm_node_path.c_str(), O_RDWR | O_CLOEXEC);
    if (m_drm_fd < 0) {
        m_drm_node_path = "/dev/dri/card1";
        m_drm_fd = open(m_drm_node_path.c_str(), O_RDWR | O_CLOEXEC);
    }

    if (m_drm_fd < 0) {
        log_warn("[VulkanDevice] Could not open DRM node '{}', falling back without DRM syncobj.", m_drm_node_path);
    } else {
        m_gbm_device = gbm_create_device(m_drm_fd);
        log_info("[VulkanDevice] Matched DRM render node '{}' (FD: {}, GBM Device: 0x{:x})",
            m_drm_node_path, m_drm_fd, reinterpret_cast<uintptr_t>(m_gbm_device));
    }
}

void VulkanDevice::create_logical_device(VulkanInstance& instance) {
    uint32_t queue_family_count = 0;
    instance.vki().vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    instance.vki().vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_count, queue_families.data());

    int graphics_family = -1;
    int transfer_family = -1;
    int compute_family = -1;
    int video_decode_family = -1;
    int video_encode_family = -1;

    // 1. Find Graphics Family
    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_family = static_cast<int>(i);
            break;
        }
    }

    // 2. Find Dedicated Transfer Family (prefer transfer without graphics)
    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if ((queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            transfer_family = static_cast<int>(i);
            break;
        }
    }
    if (transfer_family == -1) transfer_family = graphics_family;

    // 3. Find Dedicated Compute Family (prefer compute without graphics)
    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if ((queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            compute_family = static_cast<int>(i);
            break;
        }
    }
    if (compute_family == -1) compute_family = graphics_family;

    // 4. Find Video Decode & Encode Families
    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (queue_families[i].queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) {
            video_decode_family = static_cast<int>(i);
        }
        if (queue_families[i].queueFlags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) {
            video_encode_family = static_cast<int>(i);
        }
    }

    // Build unique queue create infos
    std::map<uint32_t, std::vector<float>> family_queues;
    family_queues[static_cast<uint32_t>(graphics_family)].push_back(1.0f);
    if (transfer_family != graphics_family) {
        family_queues[static_cast<uint32_t>(transfer_family)].push_back(1.0f);
    }
    if (compute_family != graphics_family && compute_family != transfer_family) {
        family_queues[static_cast<uint32_t>(compute_family)].push_back(1.0f);
    }
    if (video_decode_family != -1 && !family_queues.contains(static_cast<uint32_t>(video_decode_family))) {
        family_queues[static_cast<uint32_t>(video_decode_family)].push_back(1.0f);
    }
    if (video_encode_family != -1 && !family_queues.contains(static_cast<uint32_t>(video_encode_family))) {
        family_queues[static_cast<uint32_t>(video_encode_family)].push_back(1.0f);
    }

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    for (auto& [family_idx, priorities] : family_queues) {
        queue_create_infos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = family_idx,
            .queueCount = static_cast<uint32_t>(priorities.size()),
            .pQueuePriorities = priorities.data(),
        });
    }

    // Available device extensions check
    uint32_t ext_count = 0;
    instance.vki().vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> available_exts(ext_count);
    instance.vki().vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &ext_count, available_exts.data());

    std::vector<const char*> device_extensions;
    auto enable_dev_ext = [&](const char* ext_name) {
        for (const auto& e : available_exts) {
            if (std::strcmp(e.extensionName, ext_name) == 0) {
                device_extensions.push_back(ext_name);
                return true;
            }
        }
        return false;
    };

    enable_dev_ext(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    enable_dev_ext(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    enable_dev_ext(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
    enable_dev_ext(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
    enable_dev_ext(VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME);
    enable_dev_ext(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME);
    enable_dev_ext(VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME);
    if (video_decode_family != -1) {
        enable_dev_ext(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
        enable_dev_ext(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
    }
    if (video_encode_family != -1) {
        enable_dev_ext(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
        enable_dev_ext(VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME);
    }

    VkPhysicalDeviceFeatures features{
        .samplerAnisotropy = VK_TRUE,
        .shaderInt64 = VK_TRUE,
        .shaderInt16 = VK_TRUE,
    };

    VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptor_heap_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT,
        .pNext = nullptr,
        .descriptorHeap = VK_TRUE,
        .descriptorHeapCaptureReplay = VK_FALSE,
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
        .flags = 0,
        .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
        .pQueueCreateInfos = queue_create_infos.data(),
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures = &features,
    };

    if (instance.vki().vkCreateDevice(m_physical_device, &device_create_info, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan logical device");
    }

    // Load device-level dispatch table
    volkLoadDeviceTable(&m_vkd, m_device);

    m_pfnSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(m_device, "vkSetDebugUtilsObjectNameEXT"));
    m_pfnCmdBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(m_device, "vkCmdBeginDebugUtilsLabelEXT"));
    m_pfnCmdEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(m_device, "vkCmdEndDebugUtilsLabelEXT"));
    m_pfnCmdInsertDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(m_device, "vkCmdInsertDebugUtilsLabelEXT"));

    // Retrieve Queue handles
    m_graphics_queue.family_index = static_cast<uint32_t>(graphics_family);
    m_graphics_queue.queue_index = 0;
    m_graphics_queue.is_dedicated = false;
    m_vkd.vkGetDeviceQueue(m_device, m_graphics_queue.family_index, 0, &m_graphics_queue.handle);
    set_name(m_graphics_queue.handle, "Main Graphics Queue");

    m_transfer_queue.family_index = static_cast<uint32_t>(transfer_family);
    m_transfer_queue.queue_index = 0;
    m_transfer_queue.is_dedicated = (transfer_family != graphics_family);
    m_vkd.vkGetDeviceQueue(m_device, m_transfer_queue.family_index, 0, &m_transfer_queue.handle);
    set_name(m_transfer_queue.handle, "Dedicated Transfer DMA Queue");

    m_compute_queue.family_index = static_cast<uint32_t>(compute_family);
    m_compute_queue.queue_index = 0;
    m_compute_queue.is_dedicated = (compute_family != graphics_family);
    m_vkd.vkGetDeviceQueue(m_device, m_compute_queue.family_index, 0, &m_compute_queue.handle);
    set_name(m_compute_queue.handle, "Async Compute Queue");

    if (video_decode_family != -1) {
        m_video_decode_queue.family_index = static_cast<uint32_t>(video_decode_family);
        m_video_decode_queue.queue_index = 0;
        m_video_decode_queue.is_dedicated = true;
        m_vkd.vkGetDeviceQueue(m_device, m_video_decode_queue.family_index, 0, &m_video_decode_queue.handle);
        set_name(m_video_decode_queue.handle, "Vulkan Video Decode Queue");
    }

    if (video_encode_family != -1) {
        m_video_encode_queue.family_index = static_cast<uint32_t>(video_encode_family);
        m_video_encode_queue.queue_index = 0;
        m_video_encode_queue.is_dedicated = true;
        m_vkd.vkGetDeviceQueue(m_device, m_video_encode_queue.family_index, 0, &m_video_encode_queue.handle);
        set_name(m_video_encode_queue.handle, "Vulkan Video Encode Queue");
    }

    log_info("[VulkanDevice] Logical Device initialized (Graphics Q: {}, Dedicated Transfer Q: {}, Compute Q: {})",
        m_graphics_queue.family_index, m_transfer_queue.family_index, m_compute_queue.family_index);
}

void VulkanDevice::init_vma(VulkanInstance& instance) {
    VmaVulkanFunctions vulkan_functions{};
    vulkan_functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkan_functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    vulkan_functions.vkGetPhysicalDeviceProperties = instance.get_vki().vkGetPhysicalDeviceProperties;
    vulkan_functions.vkGetPhysicalDeviceMemoryProperties = instance.get_vki().vkGetPhysicalDeviceMemoryProperties;
    vulkan_functions.vkAllocateMemory = m_vkd.vkAllocateMemory;
    vulkan_functions.vkFreeMemory = m_vkd.vkFreeMemory;
    vulkan_functions.vkMapMemory = m_vkd.vkMapMemory;
    vulkan_functions.vkUnmapMemory = m_vkd.vkUnmapMemory;
    vulkan_functions.vkFlushMappedMemoryRanges = m_vkd.vkFlushMappedMemoryRanges;
    vulkan_functions.vkInvalidateMappedMemoryRanges = m_vkd.vkInvalidateMappedMemoryRanges;
    vulkan_functions.vkBindBufferMemory = m_vkd.vkBindBufferMemory;
    vulkan_functions.vkBindImageMemory = m_vkd.vkBindImageMemory;
    vulkan_functions.vkGetBufferMemoryRequirements = m_vkd.vkGetBufferMemoryRequirements;
    vulkan_functions.vkGetImageMemoryRequirements = m_vkd.vkGetImageMemoryRequirements;
    vulkan_functions.vkCreateBuffer = m_vkd.vkCreateBuffer;
    vulkan_functions.vkDestroyBuffer = m_vkd.vkDestroyBuffer;
    vulkan_functions.vkCreateImage = m_vkd.vkCreateImage;
    vulkan_functions.vkDestroyImage = m_vkd.vkDestroyImage;
    vulkan_functions.vkCmdCopyBuffer = m_vkd.vkCmdCopyBuffer;
    vulkan_functions.vkGetBufferMemoryRequirements2KHR = m_vkd.vkGetBufferMemoryRequirements2;
    vulkan_functions.vkGetImageMemoryRequirements2KHR = m_vkd.vkGetImageMemoryRequirements2;
    vulkan_functions.vkBindBufferMemory2KHR = m_vkd.vkBindBufferMemory2;
    vulkan_functions.vkBindImageMemory2KHR = m_vkd.vkBindImageMemory2;
    vulkan_functions.vkGetPhysicalDeviceMemoryProperties2KHR = instance.get_vki().vkGetPhysicalDeviceMemoryProperties2;
    vulkan_functions.vkGetPhysicalDeviceProperties2KHR = instance.get_vki().vkGetPhysicalDeviceProperties2;
    vulkan_functions.vkGetDeviceBufferMemoryRequirements = m_vkd.vkGetDeviceBufferMemoryRequirements;
    vulkan_functions.vkGetDeviceImageMemoryRequirements = m_vkd.vkGetDeviceImageMemoryRequirements;

    VmaAllocatorCreateInfo allocator_info{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = m_physical_device,
        .device = m_device,
        .preferredLargeHeapBlockSize = 0,
        .pAllocationCallbacks = nullptr,
        .pDeviceMemoryCallbacks = nullptr,
        .pHeapSizeLimit = nullptr,
        .pVulkanFunctions = &vulkan_functions,
        .instance = instance.get_instance(),
        .vulkanApiVersion = VK_API_VERSION_1_4,
        .pTypeExternalMemoryHandleTypes = nullptr,
    };

    if (vmaCreateAllocator(&allocator_info, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan Memory Allocator (VMA)");
    }
}

const QueueInfo& VulkanDevice::get_queue(QueueType type) const {
    switch (type) {
        case QueueType::Graphics:    return m_graphics_queue;
        case QueueType::Transfer:    return m_transfer_queue;
        case QueueType::Compute:     return m_compute_queue;
        case QueueType::VideoDecode: return m_video_decode_queue;
        case QueueType::VideoEncode: return m_video_encode_queue;
    }
    return m_graphics_queue;
}

uint32_t VulkanDevice::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
    for (uint32_t i = 0; i < m_memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) && (m_memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    for (uint32_t i = 0; i < m_memory_properties.memoryTypeCount; ++i) {
        if (type_filter & (1u << i)) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

VkDeviceAddress VulkanDevice::get_buffer_device_address(VkBuffer buffer) const {
    VkBufferDeviceAddressInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .pNext = nullptr,
        .buffer = buffer,
    };
    return m_vkd.vkGetBufferDeviceAddress(m_device, &info);
}

void VulkanDevice::execute_single_time_commands(
    const std::function<void(VkCommandBuffer cmd)>& record_fn,
    QueueType queue_type) const {
    const auto& queue_info = get_queue(queue_type);

    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = queue_info.family_index,
    };

    VkCommandPool pool = VK_NULL_HANDLE;
    if (m_vkd.vkCreateCommandPool(m_device, &pool_info, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create single time command pool");
    }

    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    m_vkd.vkAllocateCommandBuffers(m_device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    m_vkd.vkBeginCommandBuffer(cmd, &begin_info);

    record_fn(cmd);

    m_vkd.vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    m_vkd.vkQueueSubmit(queue_info.handle, 1, &submit_info, VK_NULL_HANDLE);
    m_vkd.vkQueueWaitIdle(queue_info.handle);

    m_vkd.vkDestroyCommandPool(m_device, pool, nullptr);
}

void VulkanDevice::set_debug_name(VkObjectType type, uint64_t handle, const char* name) const {
    if (m_pfnSetDebugUtilsObjectNameEXT && handle != 0 && name != nullptr) {
        VkDebugUtilsObjectNameInfoEXT info{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .pNext = nullptr,
            .objectType = type,
            .objectHandle = handle,
            .pObjectName = name,
        };
        m_pfnSetDebugUtilsObjectNameEXT(m_device, &info);
    }
}

void VulkanDevice::begin_debug_label(VkCommandBuffer cmd, const char* label_name, glm::vec4 color) const {
    if (m_pfnCmdBeginDebugUtilsLabelEXT && cmd != VK_NULL_HANDLE && label_name != nullptr) {
        VkDebugUtilsLabelEXT label{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pNext = nullptr,
            .pLabelName = label_name,
            .color = {color.r, color.g, color.b, color.a},
        };
        m_pfnCmdBeginDebugUtilsLabelEXT(cmd, &label);
    }
}

void VulkanDevice::end_debug_label(VkCommandBuffer cmd) const {
    if (m_pfnCmdEndDebugUtilsLabelEXT && cmd != VK_NULL_HANDLE) {
        m_pfnCmdEndDebugUtilsLabelEXT(cmd);
    }
}

void VulkanDevice::insert_debug_label(VkCommandBuffer cmd, const char* label_name, glm::vec4 color) const {
    if (m_pfnCmdInsertDebugUtilsLabelEXT && cmd != VK_NULL_HANDLE && label_name != nullptr) {
        VkDebugUtilsLabelEXT label{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pNext = nullptr,
            .pLabelName = label_name,
            .color = {color.r, color.g, color.b, color.a},
        };
        m_pfnCmdInsertDebugUtilsLabelEXT(cmd, &label);
    }
}

void VulkanDevice::cleanup() {
    if (m_device != VK_NULL_HANDLE) {
        m_vkd.vkDeviceWaitIdle(m_device);

        if (m_allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_allocator);
            m_allocator = VK_NULL_HANDLE;
        }

        m_vkd.vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_gbm_device) {
        gbm_device_destroy(m_gbm_device);
        m_gbm_device = nullptr;
    }

    if (m_drm_fd >= 0) {
        close(m_drm_fd);
        m_drm_fd = -1;
    }
}

} // namespace codotaku
