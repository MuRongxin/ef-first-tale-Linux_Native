#pragma once
#include <filesystem>
#include <string>
namespace ef {
struct Options {
    std::filesystem::path content{"content"}, screenshot;
    int width{1000}, height{750}, frames{};
    bool fullscreen{}, maximized{}, validate{}, smoke{};
    std::string script;
    char variant{'e'};
    bool text_preview{}, audit_scripts{}, audit_resources{};
    int trace_messages{};
};
int run(const Options& options);
}
