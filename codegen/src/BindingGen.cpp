#include "Shared.hpp"
#include <iostream>
#include <set>

namespace { namespace format_strings {
    // requires: base_classes, class_name
    constexpr char const* binding_include = R"GEN(#include "{base_directory}/{file_name}"
)GEN";

    constexpr char const* class_includes = R"GEN(#pragma once
#include <stdexcept>
#include <Geode/platform/platform.hpp>
#include <Geode/c++stl/gdstdlib.hpp>
#include <cocos2d.h>
#include <cocos-ext.h>
#include <Geode/GeneratedPredeclare.hpp>
#include <Geode/Enums.hpp>
#include <Geode/utils/SeedValue.hpp>

)GEN";

    constexpr char const* class_no_includes = R"GEN(#pragma once
#include <Geode/platform/platform.hpp>
#include <stdexcept>

)GEN";
    
    constexpr char const* class_include_prereq = R"GEN(#include "{file_name}"
)GEN";

    constexpr char const* class_start = R"GEN(
class {class_name}{base_classes} {{
public:
    static constexpr auto CLASS_NAME = "{class_name}";
)GEN";

	constexpr char const* custom_constructor = R"GEN(    GEODE_CUSTOM_CONSTRUCTOR_GD({class_name}, {first_base})
)GEN";

	constexpr char const* custom_constructor_cutoff = R"GEN(    GEODE_CUSTOM_CONSTRUCTOR_CUTOFF({class_name}, {first_base})
)GEN";

    constexpr char const* function_definition = R"GEN(
    /**
{docs}{docs_addresses}     */
    {static}{virtual}{return_type} {function_name}({parameters}){const};
)GEN";

    constexpr char const* error_definition = R"GEN(    
private:
    [[deprecated("{class_name}::{function_name} not implemented")]]
    /**
{docs}{docs_addresses}     */
    {static}{virtual}{return_type} {function_name}({parameters}){const};
public:
)GEN";

    constexpr char const* structor_definition = R"GEN(
    /**
{docs}{docs_addresses}     */
    {function_name}({parameters});
)GEN";
    
    // requires: type, member_name, array
    constexpr char const* member_definition = R"GEN({private}    {type} {member_name};{public}
)GEN";

    constexpr char const* pad_definition = R"GEN(    GEODE_PAD({hardcode});
)GEN";

    constexpr char const* class_end = R"GEN(};
)GEN";
}}

inline std::string nameForPlatform(Platform platform) {
    switch (platform) {
        case Platform::MacArm: return "MacOS (ARM)";
        case Platform::MacIntel: return "MacOS (Intel)";
        case Platform::Mac: return "MacOS";
        case Platform::Windows: return "Windows";
        case Platform::iOS: return "iOS";
        case Platform::Android: return "Android";
        default: // unreachable
            return "Windows";
    }
}

// Only Function and FunctionBindField
template <class T>
std::string generateAddressDocs(T const& f, PlatformNumber pn) {
    std::string ret;

    for (auto platform : {Platform::MacArm, Platform::MacIntel, Platform::Windows, Platform::iOS, Platform::Android}) {
        auto status = codegen::getStatusWithPlatform(platform, f);

        if (status == BindStatus::NeedsBinding) {
            ret += fmt::format("     * @note[short] {}: 0x{:x}\n", 
                nameForPlatform(platform),
                codegen::platformNumberWithPlatform(platform, pn)
            );
        }
        else if (status == BindStatus::Binded) {
            ret += fmt::format("     * @note[short] {}\n", 
                nameForPlatform(platform)
            );
        }
        else if (status == BindStatus::Inlined) {
            ret += fmt::format("     * @note[short] {}: Out of line\n", 
                nameForPlatform(platform)
            );
        }
    }

    return ret;
}

std::string generateDocs(std::string const& docs) {
    if (docs.size() < 7) return "";
    auto ret = docs.substr(1, docs.size() - 6); // i hate this but idk how to generalize

    for (auto next = ret.find("        "); next != std::string::npos; next = ret.find("        ")) {
        ret.replace(next, 8, "     * ");
    }
        
    return ret;
}

std::string generateBindingHeader(Root const& root, std::filesystem::path const& singleFolder, std::unordered_set<std::string>* generatedFiles) {
    std::string output;
    std::string base_directory = singleFolder.filename().string();

    {
        std::string filename = "Standalones.hpp";
        output += fmt::format(format_strings::binding_include,
            fmt::arg("base_directory", base_directory),
            fmt::arg("file_name", filename)
        );

        if (generatedFiles != nullptr) {
            generatedFiles->insert(filename);
        }

        std::string single_output;
        single_output += format_strings::class_includes;

        for (auto& f : root.functions) {
            if (codegen::getStatus(f) == BindStatus::Missing) continue;

            FunctionProto const* fb = &f.prototype;

            std::string addressDocs = generateAddressDocs(f, f.binds);
            std::string docs = generateDocs(fb->attributes.docs);

            single_output += fmt::format(format_strings::function_definition,
                fmt::arg("virtual", ""),
                fmt::arg("static", ""),
                fmt::arg("class_name", ""),
                fmt::arg("const", ""),
                fmt::arg("function_name", fb->name),
                fmt::arg("parameters", codegen::getParameters(*fb)),
                fmt::arg("return_type", fb->ret.name),
                fmt::arg("docs_addresses", addressDocs),
                fmt::arg("docs", docs)
            );

        }

        writeFile(singleFolder / filename, single_output);
    }

        for (auto& cls : root.classes) {
        if (is_cocos_or_fmod_class(cls.name))
            continue;

        std::string filename = (codegen::getUnqualifiedClassName(cls.name) + ".hpp");
        output += fmt::format(format_strings::binding_include,
            fmt::arg("base_directory", base_directory),
            fmt::arg("file_name", filename)
        );

        if (generatedFiles != nullptr) {
            generatedFiles->insert(filename);
        }

        std::string single_output;
        if (cls.name != "GDString") {
            single_output += format_strings::class_includes;
        } else {
            single_output += format_strings::class_no_includes;
        }

        // Hack: Include fmod.hpp if potentially needed.
        if (can_find(cls.name, "FMOD")) {
            single_output += "#include <fmod.hpp>\n";
        }

        for (auto dep : cls.attributes.depends) {
            if (is_cocos_or_fmod_class(dep)) continue;

            std::string depfilename = (codegen::getUnqualifiedClassName(dep) + ".hpp");

            single_output += fmt::format(format_strings::class_include_prereq, fmt::arg("file_name", depfilename));
        }

        std::string supers = str_if(
            fmt::format(" : public {}", fmt::join(cls.superclasses, ", public ")),
            !cls.superclasses.empty()
        );

        single_output += fmt::format(::format_strings::class_start,
            fmt::arg("class_name", cls.name),
            fmt::arg("base_classes", supers)//,
            // fmt::arg("hidden", str_if("GEODE_HIDDEN ", (codegen::platform & (Platform::Mac | Platform::iOS)) != Platform::None))
        );

        // what.
        if (!cls.superclasses.empty()) {
            single_output += fmt::format(
                fmt::runtime(is_cocos_class(cls.superclasses[0]) 
                    ? format_strings::custom_constructor_cutoff
                    : format_strings::custom_constructor),
                fmt::arg("class_name", cls.name),
                fmt::arg("first_base", cls.superclasses[0])
            );
        }

        bool unimplementedField = false;
        for (auto field : cls.fields) {
            MemberFunctionProto* fb;
            char const* used_format = format_strings::function_definition;

            std::string addressDocs;

            if (auto i = field.get_as<InlineField>()) {
                single_output += "\t" + i->inner + "\n";
                continue;
            } else if (auto m = field.get_as<MemberField>()) {

                if (m->platform == Platform::None || (m->platform & codegen::platform) != Platform::None) {
                    auto* opt = std::getenv("CODEGEN_FORCE_PUBLIC_MEMBER");
                    if (opt != nullptr && std::string_view(opt) == "1") {
                        unimplementedField = false;
                    }
                    single_output += fmt::format(format_strings::member_definition,
                        fmt::arg("private", unimplementedField ? "private:\n" : ""),
                        fmt::arg("public", unimplementedField ? "\npublic:" : ""),
                        fmt::arg("type", m->type.name),
                        fmt::arg("member_name", m->name + str_if(fmt::format("[{}]", m->count), m->count))
                    );
                }

                continue;
            } else if (auto p = field.get_as<PadField>()) {
                auto hardcode = codegen::platformNumber(p->amount);

                if (hardcode > 0)
                    single_output += fmt::format(format_strings::pad_definition, fmt::arg("hardcode", hardcode));
                else if (hardcode == 0)
                    single_output += "    // no padding\n";
                else
                    unimplementedField = true;

                continue;
            } else if (auto fn = field.get_as<FunctionBindField>()) {
                if (codegen::getStatus(*fn) == BindStatus::Missing)
                    continue;

                fb = &fn->prototype;

                if (codegen::platformNumber(fn->binds) == -1 && codegen::getStatus(*fn) != BindStatus::Binded && fb->type != FunctionType::Normal) {
                    continue;
                }

                addressDocs = generateAddressDocs(*fn, fn->binds);
            }

            std::string docs = generateDocs(fb->attributes.docs);

            single_output += fmt::format(fmt::runtime(used_format),
                fmt::arg("virtual", str_if("virtual ", fb->is_virtual)),
                fmt::arg("static", str_if("static ", fb->is_static)),
                fmt::arg("class_name", cls.name),
                fmt::arg("const", str_if(" const ", fb->is_const)),
                fmt::arg("function_name", fb->name),
                fmt::arg("parameters", codegen::getParameters(*fb)),
                fmt::arg("return_type", fb->ret.name),
                fmt::arg("docs_addresses", addressDocs),
                fmt::arg("docs", docs)
            );
        }

        // if (hasClass)
        single_output += ::format_strings::class_end;

        writeFile(singleFolder / filename, single_output);
    }

    return output;
}
