#include <cstring>
#include <print>
#include <stdexcept>

#include <codotaku/shader/slang_compiler.hpp>

namespace codotaku {

SlangCompiler::SlangCompiler() {
    if (SLANG_FAILED(slang::createGlobalSession(m_global_session.writeRef()))) {
        throw std::runtime_error("Failed to create Slang global session");
    }
}

CompiledShaders SlangCompiler::compile_source(const char* source_code, const char* module_name) {
    slang::SessionDesc session_desc = {};
    slang::TargetDesc target_desc = {};
    target_desc.format = SLANG_SPIRV;
    target_desc.profile = m_global_session->findProfile("spirv_1_5");
    session_desc.targets = &target_desc;
    session_desc.targetCount = 1;

    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(m_global_session->createSession(session_desc, session.writeRef()))) {
        throw std::runtime_error("Failed to create Slang compilation session");
    }

    Slang::ComPtr<slang::IBlob> diagnostic_blob;
    Slang::ComPtr<slang::IModule> module(
        session->loadModuleFromSourceString(module_name, module_name, source_code, diagnostic_blob.writeRef()));

    if (!module) {
        std::string err = diagnostic_blob ? static_cast<const char*>(diagnostic_blob->getBufferPointer()) : "Unknown Slang error";
        throw std::runtime_error("Slang compilation failed: " + err);
    }

    Slang::ComPtr<slang::IEntryPoint> vs_entry;
    module->findEntryPointByName("vsMain", vs_entry.writeRef());

    Slang::ComPtr<slang::IEntryPoint> fs_entry;
    module->findEntryPointByName("fsMain", fs_entry.writeRef());

    slang::IComponentType* components[] = { module.get(), vs_entry.get(), fs_entry.get() };
    Slang::ComPtr<slang::IComponentType> program;
    session->createCompositeComponentType(components, 3, program.writeRef(), diagnostic_blob.writeRef());

    Slang::ComPtr<slang::IComponentType> linked_program;
    program->link(linked_program.writeRef(), diagnostic_blob.writeRef());

    Slang::ComPtr<slang::IBlob> vs_blob;
    linked_program->getEntryPointCode(0, 0, vs_blob.writeRef(), diagnostic_blob.writeRef());

    Slang::ComPtr<slang::IBlob> fs_blob;
    linked_program->getEntryPointCode(1, 0, fs_blob.writeRef(), diagnostic_blob.writeRef());

    CompiledShaders result{};
    auto copy_blob = [](slang::IBlob* blob, std::vector<uint32_t>& out) {
        size_t size_bytes = blob->getBufferSize();
        out.resize(size_bytes / sizeof(uint32_t));
        std::memcpy(out.data(), blob->getBufferPointer(), size_bytes);
    };

    copy_blob(vs_blob.get(), result.vs_spirv);
    copy_blob(fs_blob.get(), result.fs_spirv);

    auto layout = linked_program->getLayout();

    // Reflect Push Constants
    for (unsigned i = 0; i < layout->getParameterCount(); ++i) {
        auto param = layout->getParameterByIndex(i);
        if (param->getCategory() == slang::ParameterCategory::PushConstantBuffer) {
            uint32_t size = sizeof(uint64_t); // 8 bytes for BDA pointer

            result.reflection.push_constants.push_back({
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset = 0,
                .size = size,
            });
        }
    }

    return result;
}

VkShaderModule create_shader_module(VkDevice device, const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size() * sizeof(uint32_t),
        .pCode = spirv.data(),
    };
    VkShaderModule module{VK_NULL_HANDLE};
    if (vkCreateShaderModule(device, &create_info, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shader module from SPIR-V");
    }
    return module;
}

} // namespace codotaku
