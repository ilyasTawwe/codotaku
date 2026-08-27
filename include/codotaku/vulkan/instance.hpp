#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <volk.h>

namespace codotaku {

struct InstanceConfig {
    std::string app_name{"Codotaku App"};
    uint32_t app_version{VK_MAKE_VERSION(1, 0, 0)};
    bool enable_validation{true};
    bool enable_synchronization_validation{true};
    std::vector<const char*> extra_layers{};
    std::vector<const char*> extra_extensions{};
    VkDebugUtilsMessageSeverityFlagsEXT debug_severity{
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
    };
    VkDebugUtilsMessageTypeFlagsEXT debug_types{
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT
    };
};

struct PhysicalDeviceInfo {
    VkPhysicalDevice handle{VK_NULL_HANDLE};
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    VkPhysicalDeviceDrmPropertiesEXT drm_properties{};
    std::vector<VkQueueFamilyProperties> queue_families{};
    std::vector<VkExtensionProperties> available_extensions{};

    bool has_graphics() const;
    bool has_compute() const;
    bool has_transfer() const;
    bool is_discrete() const { return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU; }
    bool is_integrated() const { return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU; }
    bool supports_extension(const char* ext_name) const;
};

// Returns a rating/score for GPU selection. A return value <= 0 indicates unsuitable GPU.
using GpuSelector = std::function<int(const PhysicalDeviceInfo& info)>;

namespace GpuPreference {
    // Default smart selector: prefers discrete GPU with highest VRAM
    int DiscreteFirst(const PhysicalDeviceInfo& info);
    // Prefers integrated GPU
    int IntegratedFirst(const PhysicalDeviceInfo& info);
    // Matches specific GPU device name substring
    GpuSelector MatchName(std::string name_substring);
    // Matches specific DRM render node (e.g. "/dev/dri/renderD128")
    GpuSelector MatchDrmNode(std::string node_path);
}

class VulkanInstance {
public:
    explicit VulkanInstance(const InstanceConfig& config = {});
    explicit VulkanInstance(const std::string& app_name, bool enable_validation = true);
    ~VulkanInstance();

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    VulkanInstance(VulkanInstance&& other) noexcept;
    VulkanInstance& operator=(VulkanInstance&& other) noexcept;

    void cleanup();

    VkInstance get_instance() const { return m_instance; }
    const VolkInstanceTable& get_vki() const { return m_vki; }
    const VolkInstanceTable& vki() const { return m_vki; }

    std::vector<VkPhysicalDevice> enumerate_physical_devices() const;
    std::vector<PhysicalDeviceInfo> query_physical_devices() const;
    VkPhysicalDevice select_physical_device(const GpuSelector& selector = GpuPreference::DiscreteFirst) const;

private:
    void init_instance(const InstanceConfig& config);

    InstanceConfig m_config{};
    VkInstance m_instance{VK_NULL_HANDLE};
    VolkInstanceTable m_vki{};
    VkDebugUtilsMessengerEXT m_debug_messenger{VK_NULL_HANDLE};
};

} // namespace codotaku
