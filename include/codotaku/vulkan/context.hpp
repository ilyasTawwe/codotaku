#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <volk.h>
#include <vk_mem_alloc.h>

namespace codotaku {

struct DeviceAlignmentLimits {
    VkDeviceSize min_storage_buffer_offset_alignment{16};
    VkDeviceSize min_uniform_buffer_offset_alignment{16};
    VkDeviceSize optimal_buffer_copy_offset_alignment{16};
    VkDeviceSize optimal_buffer_copy_row_pitch_alignment{16};
    VkDeviceSize min_memory_map_alignment{64};
};

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
    const DeviceAlignmentLimits& get_alignment_limits() const { return m_alignment_limits; }
    VmaAllocator get_allocator() const { return m_allocator; }
    int get_drm_fd() const { return m_drm_fd; }

    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
    VkDeviceAddress get_buffer_device_address(VkBuffer buffer) const;
    void execute_single_time_commands(const std::function<void(VkCommandBuffer cmd)>& record_fn) const;

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
    DeviceAlignmentLimits m_alignment_limits{};

    VmaAllocator m_allocator{VK_NULL_HANDLE};
    int m_drm_fd{-1};
};

} // namespace codotaku
