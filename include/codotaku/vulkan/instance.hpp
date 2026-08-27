#pragma once

#include <memory>
#include <string>
#include <vector>
#include <volk.h>

namespace codotaku {

class VulkanInstance {
public:
    explicit VulkanInstance(const std::string& app_name = "Codotaku App", bool enable_validation = true);
    ~VulkanInstance();

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    VulkanInstance(VulkanInstance&& other) noexcept;
    VulkanInstance& operator=(VulkanInstance&& other) noexcept;

    void cleanup();

    VkInstance get_instance() const { return m_instance; }
    const VolkInstanceTable& get_table() const { return m_table; }

    std::vector<VkPhysicalDevice> enumerate_physical_devices() const;

private:
    void init_instance(const std::string& app_name, bool enable_validation);
    void setup_debug_messenger();

    VkInstance m_instance{VK_NULL_HANDLE};
    VolkInstanceTable m_table{};
    VkDebugUtilsMessengerEXT m_debug_messenger{VK_NULL_HANDLE};
};

} // namespace codotaku
