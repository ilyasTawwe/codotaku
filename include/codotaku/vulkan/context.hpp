#pragma once

#include <cstdint>
#include <vector>
#include <volk.h>
#include <vk_mem_alloc.h>

namespace codotaku {

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance get_instance() const { return m_instance; }
    VkPhysicalDevice get_physical_device() const { return m_physical_device; }
    VkDevice get_device() const { return m_device; }
    uint32_t get_queue_family_index() const { return m_queue_family_index; }
    VkQueue get_queue() const { return m_queue; }
    const VkPhysicalDeviceMemoryProperties& get_memory_properties() const { return m_memory_properties; }
    VmaAllocator get_allocator() const { return m_allocator; }
    int get_drm_fd() const { return m_drm_fd; }

    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
    VkDeviceAddress get_buffer_device_address(VkBuffer buffer) const;

private:
    void init_instance();
    void select_physical_device();
    void create_logical_device();
    void init_vma();
    void open_drm_node();
    void cleanup();

    VkInstance m_instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debug_messenger{VK_NULL_HANDLE};
    VkPhysicalDevice m_physical_device{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    uint32_t m_queue_family_index{0};
    VkQueue m_queue{VK_NULL_HANDLE};
    VkPhysicalDeviceMemoryProperties m_memory_properties{};

    VmaAllocator m_allocator{VK_NULL_HANDLE};
    int m_drm_fd{-1};
};

} // namespace codotaku
