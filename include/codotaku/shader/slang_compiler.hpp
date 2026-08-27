#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <volk.h>
#include <slang.h>
#include <slang-com-ptr.h>

namespace codotaku {

struct ReflectedPipelineData {
    std::vector<VkPushConstantRange> push_constants;
};

struct CompiledShaders {
    std::vector<uint32_t> vs_spirv;
    std::vector<uint32_t> fs_spirv;
    ReflectedPipelineData reflection;
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
