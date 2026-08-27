#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <volk.h>
#include <slang.h>
#include <slang-com-ptr.h>

namespace codotaku {

struct ReflectedBinding {
    uint32_t set{0};
    uint32_t binding{0};
    VkDescriptorType descriptor_type{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    VkShaderStageFlags stage_flags{VK_SHADER_STAGE_ALL_GRAPHICS};
};

struct ReflectedPipelineData {
    std::vector<VkPushConstantRange> push_constants;
    std::map<uint32_t, std::vector<ReflectedBinding>> descriptor_sets;
};

struct CompiledShaders {
    std::vector<uint32_t> vs_spirv;
    std::vector<uint32_t> fs_spirv;
    std::vector<uint32_t> cs_spirv;
    ReflectedPipelineData reflection;

    bool is_compute() const { return !cs_spirv.empty(); }
    bool is_graphics() const { return !vs_spirv.empty() && !fs_spirv.empty(); }
};

class SlangCompiler {
public:
    SlangCompiler();
    ~SlangCompiler() = default;

    CompiledShaders compile_source(const char* source_code, const char* module_name = "shader_module");

private:
    Slang::ComPtr<slang::IGlobalSession> m_global_session;
};

VkShaderModule create_shader_module(VkDevice device, const std::vector<uint32_t>& spirv);

} // namespace codotaku
