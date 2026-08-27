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
    const VolkInstanceTable& get_vki() const { return m_vki; }
    const VolkInstanceTable& vki() const { return m_vki; }

    std::vector<VkPhysicalDevice> enumerate_physical_devices() const;

private:
    void init_instance(const std::string& app_name, bool enable_validation);

    VkInstance m_instance{VK_NULL_HANDLE};
    VolkInstanceTable m_vki{};
    VkDebugUtilsMessengerEXT m_debug_messenger{VK_NULL_HANDLE};
};

} // namespace codotaku
