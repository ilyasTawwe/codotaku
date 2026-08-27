#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <gbm.h>

#include <codotaku/vulkan/instance.hpp>

namespace codotaku {

struct DeviceAlignmentLimits {
    VkDeviceSize min_storage_buffer_offset_alignment{16};
    VkDeviceSize min_uniform_buffer_offset_alignment{16};
    VkDeviceSize optimal_buffer_copy_offset_alignment{16};
    VkDeviceSize optimal_buffer_copy_row_pitch_alignment{16};
    VkDeviceSize min_memory_map_alignment{64};
};

struct QueueInfo {
    VkQueue handle{VK_NULL_HANDLE};
    uint32_t family_index{0};
    uint32_t queue_index{0};
    bool is_dedicated{false};
    bool valid() const { return handle != VK_NULL_HANDLE; }
};

enum class QueueType {
    Graphics,
    Transfer,
    Compute,
    VideoDecode,
    VideoEncode
};

class VulkanDevice {
public:
    VulkanDevice(VulkanInstance& instance, VkPhysicalDevice preferred_gpu = VK_NULL_HANDLE);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    VulkanDevice(VulkanDevice&& other) noexcept;
    VulkanDevice& operator=(VulkanDevice&& other) noexcept;

    void cleanup();

    // Getters
    VkPhysicalDevice get_physical_device() const { return m_physical_device; }
    VkDevice get_device() const { return m_device; }
    const VolkDeviceTable& get_table() const { return m_table; }
    VmaAllocator get_allocator() const { return m_allocator; }

    // Queue accessors
    const QueueInfo& get_graphics_queue() const { return m_graphics_queue; }
    const QueueInfo& get_transfer_queue() const { return m_transfer_queue; }
    const QueueInfo& get_compute_queue() const { return m_compute_queue; }
    const QueueInfo& get_video_decode_queue() const { return m_video_decode_queue; }
    const QueueInfo& get_video_encode_queue() const { return m_video_encode_queue; }
    const QueueInfo& get_queue(QueueType type) const;

    // Properties & Limits
    const VkPhysicalDeviceMemoryProperties& get_memory_properties() const { return m_memory_properties; }
    const DeviceAlignmentLimits& get_alignment_limits() const { return m_alignment_limits; }
    const VkPhysicalDeviceDescriptorHeapPropertiesEXT& get_descriptor_heap_properties() const { return m_descriptor_heap_properties; }

    // GPU DRM & GBM handles
    int get_drm_fd() const { return m_drm_fd; }
    gbm_device* get_gbm_device() const { return m_gbm_device; }
    const std::string& get_drm_node_path() const { return m_drm_node_path; }

    // Helpers
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
    VkDeviceAddress get_buffer_device_address(VkBuffer buffer) const;
    void execute_single_time_commands(
        const std::function<void(VkCommandBuffer cmd)>& record_fn,
        QueueType queue_type = QueueType::Graphics) const;

    // Debug Utils object naming & labeling
    void set_debug_name(VkObjectType type, uint64_t handle, const char* name) const;
    void begin_debug_label(VkCommandBuffer cmd, const char* label_name, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}) const;
    void end_debug_label(VkCommandBuffer cmd) const;
    void insert_debug_label(VkCommandBuffer cmd, const char* label_name, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}) const;

    // Typed naming helpers
    void set_name(VkBuffer buffer, const char* name) const { set_debug_name(VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer, name); }
    void set_name(VkImage image, const char* name) const { set_debug_name(VK_OBJECT_TYPE_IMAGE, (uint64_t)image, name); }
    void set_name(VkImageView view, const char* name) const { set_debug_name(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)view, name); }
    void set_name(VkSampler sampler, const char* name) const { set_debug_name(VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler, name); }
    void set_name(VkPipeline pipeline, const char* name) const { set_debug_name(VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, name); }
    void set_name(VkCommandBuffer cmd, const char* name) const { set_debug_name(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)cmd, name); }
    void set_name(VkQueue queue, const char* name) const { set_debug_name(VK_OBJECT_TYPE_QUEUE, (uint64_t)queue, name); }
    void set_name(VkFence fence, const char* name) const { set_debug_name(VK_OBJECT_TYPE_FENCE, (uint64_t)fence, name); }
    void set_name(VkSemaphore sem, const char* name) const { set_debug_name(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)sem, name); }
    void set_name(VkCommandPool pool, const char* name) const { set_debug_name(VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)pool, name); }
    void set_name(VkShaderModule mod, const char* name) const { set_debug_name(VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)mod, name); }

private:
    void select_physical_device(VulkanInstance& instance, VkPhysicalDevice preferred_gpu);
    void setup_drm_and_gbm();
    void create_logical_device(VulkanInstance& instance);
    void init_vma(VulkanInstance& instance);

    VkPhysicalDevice m_physical_device{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VolkDeviceTable m_table{};
    PFN_vkSetDebugUtilsObjectNameEXT m_pfnSetDebugUtilsObjectNameEXT{nullptr};
    PFN_vkCmdBeginDebugUtilsLabelEXT m_pfnCmdBeginDebugUtilsLabelEXT{nullptr};
    PFN_vkCmdEndDebugUtilsLabelEXT m_pfnCmdEndDebugUtilsLabelEXT{nullptr};
    PFN_vkCmdInsertDebugUtilsLabelEXT m_pfnCmdInsertDebugUtilsLabelEXT{nullptr};
    VmaAllocator m_allocator{VK_NULL_HANDLE};

    QueueInfo m_graphics_queue{};
    QueueInfo m_transfer_queue{};
    QueueInfo m_compute_queue{};
    QueueInfo m_video_decode_queue{};
    QueueInfo m_video_encode_queue{};

    VkPhysicalDeviceMemoryProperties m_memory_properties{};
    DeviceAlignmentLimits m_alignment_limits{};
    VkPhysicalDeviceDescriptorHeapPropertiesEXT m_descriptor_heap_properties{};
    VkPhysicalDeviceDrmPropertiesEXT m_drm_properties{};

    int m_drm_fd{-1};
    gbm_device* m_gbm_device{nullptr};
    std::string m_drm_node_path;
};

// Aliases for compatibility
using VulkanContext = VulkanDevice;

} // namespace codotaku
