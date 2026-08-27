#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <utility>
#include <xf86drm.h>

#include <codotaku/system/log.hpp>
#include <codotaku/vulkan/instance.hpp>

namespace codotaku {

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        log_error("[Vulkan Validation] {}", pCallbackData->pMessage);
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        log_warn("[Vulkan Validation] {}", pCallbackData->pMessage);
    } else {
        log_debug("[Vulkan Validation] {}", pCallbackData->pMessage);
    }
    return VK_FALSE;
}

} // namespace

bool PhysicalDeviceInfo::has_graphics() const {
    for (const auto& qf : queue_families) {
        if (qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) return true;
    }
    return false;
}

bool PhysicalDeviceInfo::has_compute() const {
    for (const auto& qf : queue_families) {
        if (qf.queueFlags & VK_QUEUE_COMPUTE_BIT) return true;
    }
    return false;
}

bool PhysicalDeviceInfo::has_transfer() const {
    for (const auto& qf : queue_families) {
        if (qf.queueFlags & VK_QUEUE_TRANSFER_BIT) return true;
    }
    return false;
}

bool PhysicalDeviceInfo::supports_extension(const char* ext_name) const {
    for (const auto& ext : available_extensions) {
        if (std::strcmp(ext.extensionName, ext_name) == 0) return true;
    }
    return false;
}

namespace GpuPreference {

int DiscreteFirst(const PhysicalDeviceInfo& info) {
    if (!info.has_graphics()) return -1;
    int score = 100;
    if (info.is_discrete()) {
        score += 1000;
    } else if (info.is_integrated()) {
        score += 200;
    }
    // Boost by maximum single heap VRAM size
    for (uint32_t i = 0; i < info.memory_properties.memoryHeapCount; ++i) {
        if (info.memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            score += static_cast<int>(info.memory_properties.memoryHeaps[i].size / (1024 * 1024 * 100));
        }
    }
    return score;
}

int IntegratedFirst(const PhysicalDeviceInfo& info) {
    if (!info.has_graphics()) return -1;
    int score = 100;
    if (info.is_integrated()) {
        score += 1000;
    } else if (info.is_discrete()) {
        score += 200;
    }
    return score;
}

GpuSelector MatchName(std::string name_substring) {
    return [sub = std::move(name_substring)](const PhysicalDeviceInfo& info) -> int {
        if (!info.has_graphics()) return -1;
        std::string dev_name = info.properties.deviceName;
        if (dev_name.find(sub) != std::string::npos) {
            return 5000;
        }
        return -1;
    };
}

GpuSelector MatchDrmNode(std::string node_path) {
    return [path = std::move(node_path)](const PhysicalDeviceInfo& info) -> int {
        if (!info.has_graphics() || !info.drm_properties.hasRender) return -1;
        dev_t target_dev = makedev(static_cast<uint32_t>(info.drm_properties.renderMajor),
                                   static_cast<uint32_t>(info.drm_properties.renderMinor));
        struct stat st{};
        if (stat(path.c_str(), &st) == 0 && st.st_rdev == target_dev) {
            return 10000;
        }
        return -1;
    };
}

} // namespace GpuPreference

VulkanInstance::VulkanInstance(const InstanceConfig& config) {
    init_instance(config);
}

VulkanInstance::VulkanInstance(const std::string& app_name, bool enable_validation) {
    InstanceConfig config{
        .app_name = app_name,
        .enable_validation = enable_validation,
    };
    init_instance(config);
}

VulkanInstance::~VulkanInstance() {
    cleanup();
}

VulkanInstance::VulkanInstance(VulkanInstance&& other) noexcept
    : m_config(std::move(other.m_config)),
      m_instance(std::exchange(other.m_instance, VK_NULL_HANDLE)),
      m_vki(other.m_vki),
      m_debug_messenger(std::exchange(other.m_debug_messenger, VK_NULL_HANDLE)) {}

VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_config = std::move(other.m_config);
        m_instance = std::exchange(other.m_instance, VK_NULL_HANDLE);
        m_vki = other.m_vki;
        m_debug_messenger = std::exchange(other.m_debug_messenger, VK_NULL_HANDLE);
    }
    return *this;
}

void VulkanInstance::init_instance(const InstanceConfig& config) {
    m_config = config;
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize Volk loader");
    }

    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = m_config.app_name.c_str(),
        .applicationVersion = m_config.app_version,
        .pEngineName = "Codotaku",
        .engineVersion = VK_MAKE_VERSION(1, 4, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    std::vector<const char*> enabled_layers;
    bool validation_found = false;
    if (m_config.enable_validation) {
        for (const auto& layer : available_layers) {
            if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                enabled_layers.push_back("VK_LAYER_KHRONOS_validation");
                validation_found = true;
                log_info("[VulkanInstance] Enabled validation layer: VK_LAYER_KHRONOS_validation");
                break;
            }
        }
    }
    for (const char* extra_layer : m_config.extra_layers) {
        enabled_layers.push_back(extra_layer);
    }

    uint32_t extension_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, available_extensions.data());

    std::vector<const char*> enabled_extensions;
    auto add_extension_if_present = [&](const char* ext_name) {
        for (const auto& ext : available_extensions) {
            if (std::strcmp(ext.extensionName, ext_name) == 0) {
                enabled_extensions.push_back(ext_name);
                return true;
            }
        }
        return false;
    };

    bool debug_utils_found = add_extension_if_present(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    add_extension_if_present(VK_KHR_SURFACE_EXTENSION_NAME);
    add_extension_if_present(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
    add_extension_if_present(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    for (const char* extra_ext : m_config.extra_extensions) {
        add_extension_if_present(extra_ext);
    }

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = m_config.debug_severity,
        .messageType = m_config.debug_types,
        .pfnUserCallback = debug_callback,
        .pUserData = nullptr,
    };

    VkValidationFeatureEnableEXT validation_enables[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
    };

    VkValidationFeaturesEXT validation_features{
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = debug_utils_found ? &debug_create_info : nullptr,
        .enabledValidationFeatureCount = static_cast<uint32_t>(sizeof(validation_enables) / sizeof(validation_enables[0])),
        .pEnabledValidationFeatures = validation_enables,
        .disabledValidationFeatureCount = 0,
        .pDisabledValidationFeatures = nullptr,
    };

    void* instance_pnext = nullptr;
    if (validation_found && m_config.enable_synchronization_validation) {
        instance_pnext = &validation_features;
        log_info("[VulkanInstance] Enabled Synchronization Validation feature.");
    } else if (debug_utils_found) {
        instance_pnext = &debug_create_info;
    }

    VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = instance_pnext,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = static_cast<uint32_t>(enabled_layers.size()),
        .ppEnabledLayerNames = enabled_layers.empty() ? nullptr : enabled_layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size()),
        .ppEnabledExtensionNames = enabled_extensions.empty() ? nullptr : enabled_extensions.data(),
    };

    if (vkCreateInstance(&create_info, nullptr, &m_instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    // Load instance-level dispatch table and load instance functions only (no device trampolines)
    volkLoadInstanceTable(&m_vki, m_instance);
    volkLoadInstanceOnly(m_instance);

    if (debug_utils_found && m_vki.vkCreateDebugUtilsMessengerEXT) {
        m_vki.vkCreateDebugUtilsMessengerEXT(m_instance, &debug_create_info, nullptr, &m_debug_messenger);
    }
}

std::vector<VkPhysicalDevice> VulkanInstance::enumerate_physical_devices() const {
    uint32_t count = 0;
    m_vki.vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) {
        return {};
    }
    std::vector<VkPhysicalDevice> devices(count);
    m_vki.vkEnumeratePhysicalDevices(m_instance, &count, devices.data());
    return devices;
}

std::vector<PhysicalDeviceInfo> VulkanInstance::query_physical_devices() const {
    auto devices = enumerate_physical_devices();
    std::vector<PhysicalDeviceInfo> result;
    result.reserve(devices.size());

    for (auto pdev : devices) {
        PhysicalDeviceInfo info{};
        info.handle = pdev;

        info.drm_properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
            .pNext = nullptr,
        };
        VkPhysicalDeviceProperties2 props2{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &info.drm_properties,
        };
        m_vki.vkGetPhysicalDeviceProperties2(pdev, &props2);
        info.properties = props2.properties;

        m_vki.vkGetPhysicalDeviceFeatures(pdev, &info.features);
        m_vki.vkGetPhysicalDeviceMemoryProperties(pdev, &info.memory_properties);

        uint32_t qf_count = 0;
        m_vki.vkGetPhysicalDeviceQueueFamilyProperties(pdev, &qf_count, nullptr);
        info.queue_families.resize(qf_count);
        m_vki.vkGetPhysicalDeviceQueueFamilyProperties(pdev, &qf_count, info.queue_families.data());

        uint32_t ext_count = 0;
        m_vki.vkEnumerateDeviceExtensionProperties(pdev, nullptr, &ext_count, nullptr);
        info.available_extensions.resize(ext_count);
        m_vki.vkEnumerateDeviceExtensionProperties(pdev, nullptr, &ext_count, info.available_extensions.data());

        result.push_back(std::move(info));
    }
    return result;
}

VkPhysicalDevice VulkanInstance::select_physical_device(const GpuSelector& selector) const {
    auto gpus = query_physical_devices();
    if (gpus.empty()) {
        throw std::runtime_error("No Vulkan physical devices found on system");
    }

    int best_score = -1;
    VkPhysicalDevice best_pdev = VK_NULL_HANDLE;

    for (const auto& gpu : gpus) {
        int score = selector(gpu);
        if (score > best_score) {
            best_score = score;
            best_pdev = gpu.handle;
        }
    }

    if (best_pdev == VK_NULL_HANDLE) {
        throw std::runtime_error("No suitable GPU physical device matched the specified selection criteria");
    }
    return best_pdev;
}

void VulkanInstance::cleanup() {
    if (m_instance != VK_NULL_HANDLE) {
        if (m_debug_messenger != VK_NULL_HANDLE && m_vki.vkDestroyDebugUtilsMessengerEXT) {
            m_vki.vkDestroyDebugUtilsMessengerEXT(m_instance, m_debug_messenger, nullptr);
            m_debug_messenger = VK_NULL_HANDLE;
        }
        m_vki.vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

} // namespace codotaku
