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

    Slang::ComPtr<slang::IEntryPoint> cs_entry;
    module->findEntryPointByName("csMain", cs_entry.writeRef());

    std::vector<slang::IComponentType*> components;
    components.push_back(module.get());

    bool has_graphics = (vs_entry != nullptr && fs_entry != nullptr);
    bool has_compute = (cs_entry != nullptr);

    if (has_graphics) {
        components.push_back(vs_entry.get());
        components.push_back(fs_entry.get());
    } else if (has_compute) {
        components.push_back(cs_entry.get());
    } else {
        throw std::runtime_error("No valid entry points found in Slang source (expected vsMain+fsMain or csMain)");
    }

    Slang::ComPtr<slang::IComponentType> program;
    session->createCompositeComponentType(
        components.data(),
        static_cast<SlangInt>(components.size()),
        program.writeRef(),
        diagnostic_blob.writeRef());

    Slang::ComPtr<slang::IComponentType> linked_program;
    program->link(linked_program.writeRef(), diagnostic_blob.writeRef());

    CompiledShaders result{};
    auto copy_blob = [](slang::IBlob* blob, std::vector<uint32_t>& out) {
        size_t size_bytes = blob->getBufferSize();
        out.resize(size_bytes / sizeof(uint32_t));
        std::memcpy(out.data(), blob->getBufferPointer(), size_bytes);
    };

    if (has_graphics) {
        Slang::ComPtr<slang::IBlob> vs_blob;
        linked_program->getEntryPointCode(0, 0, vs_blob.writeRef(), diagnostic_blob.writeRef());

        Slang::ComPtr<slang::IBlob> fs_blob;
        linked_program->getEntryPointCode(1, 0, fs_blob.writeRef(), diagnostic_blob.writeRef());

        copy_blob(vs_blob.get(), result.vs_spirv);
        copy_blob(fs_blob.get(), result.fs_spirv);
    } else if (has_compute) {
        Slang::ComPtr<slang::IBlob> cs_blob;
        linked_program->getEntryPointCode(0, 0, cs_blob.writeRef(), diagnostic_blob.writeRef());
        copy_blob(cs_blob.get(), result.cs_spirv);
    }

    auto layout = linked_program->getLayout();

    // Reflect Push Constants and Descriptor Set Bindings
    for (unsigned i = 0; i < layout->getParameterCount(); ++i) {
        auto param = layout->getParameterByIndex(i);
        auto category = param->getCategory();

        if (category == slang::ParameterCategory::PushConstantBuffer) {
            auto type_layout = param->getTypeLayout();
            auto elem_layout = type_layout->getElementTypeLayout();
            uint32_t size = elem_layout ? static_cast<uint32_t>(elem_layout->getSize(slang::ParameterCategory::Uniform))
                                        : static_cast<uint32_t>(type_layout->getSize(slang::ParameterCategory::Uniform));
            if (size == 0) {
                size = 8; // fallback for 64-bit BDA pointer struct
            }

            VkShaderStageFlags stages = has_compute
                                            ? VK_SHADER_STAGE_COMPUTE_BIT
                                            : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

            result.reflection.push_constants.push_back({
                .stageFlags = stages,
                .offset = 0,
                .size = size,
            });
            std::println("  [Slang Reflection] Push Constant Range: name='{}', size={} bytes", param->getName(), size);
        } else {
            uint32_t set = static_cast<uint32_t>(param->getBindingSpace());
            uint32_t binding = static_cast<uint32_t>(param->getBindingIndex());

            auto type = param->getTypeLayout()->getType();
            auto access = type->getResourceAccess();

            VkDescriptorType desc_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            if (access == SLANG_RESOURCE_ACCESS_READ_WRITE || access == SLANG_RESOURCE_ACCESS_WRITE) {
                desc_type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            }

            VkShaderStageFlags stage_flags = has_compute ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ALL_GRAPHICS;

            result.reflection.descriptor_sets[set].push_back({
                .set = set,
                .binding = binding,
                .descriptor_type = desc_type,
                .stage_flags = stage_flags,
            });

            std::println("  [Slang Reflection] Descriptor Binding: Set {}, Binding {}, Type {}",
                set, binding, (desc_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ? "StorageImage" : "CombinedImageSampler"));
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
