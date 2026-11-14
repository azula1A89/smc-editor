#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include <ui_windows.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

class ImGuiContextGuard {
public:
    ImGuiContextGuard() : context_(ImGui::CreateContext()) {}
    ~ImGuiContextGuard() {
        ImGui::DestroyContext(context_);
    }

private:
    ImGuiContext* context_;
};

std::filesystem::path temporary_ini_path(const char* prefix) {
    return std::filesystem::temp_directory_path() /
        (std::string(prefix) +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".ini");
}

void testDefaultLayoutForMissingIni() {
    ImGuiContextGuard context;
    const auto ini_path = temporary_ini_path("dot_static_missing_layout_");
    const ImGuiID dockspace_id = ImHashStr("DefaultDockspace");

    initialize_default_docking_layout_if_missing(
        dockspace_id, ImVec2(1280.0f, 800.0f), ini_path.string().c_str());

    const ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspace_id);
    require(root && root->SplitAxis == ImGuiAxis_X,
            "Missing ini did not create a left/right dock split");
    require(root->ChildNodes[0] && root->ChildNodes[1] &&
                root->ChildNodes[1]->SplitAxis == ImGuiAxis_Y,
            "Default layout did not split the right side into top/bottom");

    const ImGuiWindowSettings* editor =
        ImGui::FindWindowSettingsByID(ImHashStr("SMC Editor"));
    const ImGuiWindowSettings* canvas =
        ImGui::FindWindowSettingsByID(ImHashStr("canvas"));
    const ImGuiWindowSettings* diagnostics =
        ImGui::FindWindowSettingsByID(ImHashStr("Status / Diagnostics"));
    require(editor && canvas && diagnostics,
            "Default layout did not assign all application windows");
    require(editor->DockId == root->ChildNodes[0]->ID &&
                canvas->DockId == root->ChildNodes[1]->ChildNodes[0]->ID &&
                diagnostics->DockId == root->ChildNodes[1]->ChildNodes[1]->ID,
            "Application windows were not assigned to their default regions");
}

void testExistingIniIsNotOverridden() {
    ImGuiContextGuard context;
    const auto ini_path = temporary_ini_path("dot_static_existing_layout_");
    {
        std::ofstream ini(ini_path);
        require(static_cast<bool>(ini), "Could not create existing ini test file");
    }

    const ImGuiID dockspace_id = ImHashStr("ExistingDockspace");
    initialize_default_docking_layout_if_missing(
        dockspace_id, ImVec2(1280.0f, 800.0f), ini_path.string().c_str());
    require(ImGui::DockBuilderGetNode(dockspace_id) == nullptr,
            "Existing ini was overridden with the default layout");

    std::filesystem::remove(ini_path);
}

} // namespace

int main() {
    try {
        testDefaultLayoutForMissingIni();
        testExistingIniIsNotOverridden();
    } catch (const std::exception& error) {
        std::cerr << "dockspace_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
