#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

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

VulkanInstance::VulkanInstance(const std::string& app_name, bool enable_validation) {
    init_instance(app_name, enable_validation);
}

VulkanInstance::~VulkanInstance() {
    cleanup();
}

VulkanInstance::VulkanInstance(VulkanInstance&& other) noexcept
    : m_instance(std::exchange(other.m_instance, VK_NULL_HANDLE)),
      m_vki(other.m_vki),
      m_debug_messenger(std::exchange(other.m_debug_messenger, VK_NULL_HANDLE)) {}

VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_instance = std::exchange(other.m_instance, VK_NULL_HANDLE);
        m_vki = other.m_vki;
        m_debug_messenger = std::exchange(other.m_debug_messenger, VK_NULL_HANDLE);
    }
    return *this;
}

void VulkanInstance::init_instance(const std::string& app_name, bool enable_validation) {
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize Volk loader");
    }

    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = app_name.c_str(),
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
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
    if (enable_validation) {
        for (const auto& layer : available_layers) {
            if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                enabled_layers.push_back("VK_LAYER_KHRONOS_validation");
                validation_found = true;
                log_info("[VulkanInstance] Enabled validation layer: VK_LAYER_KHRONOS_validation");
                break;
            }
        }
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

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
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
    if (validation_found) {
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
