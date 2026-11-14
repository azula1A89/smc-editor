#include <filesystem>

#include <imgui.h>
#include <imgui_internal.h>
#include <ui_windows.hpp>

void initialize_default_docking_layout_if_missing(
    ImGuiID dockspace_id, ImVec2 size, const char* ini_filename) {
    if (ImGui::DockBuilderGetNode(dockspace_id) ||
        (ini_filename && std::filesystem::exists(ini_filename)))
        return;

    ImGui::DockBuilderAddNode(dockspace_id);
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    ImGuiID right_id = 0;
    ImGuiID editor_id = 0;
    ImGui::DockBuilderSplitNode(
        dockspace_id, ImGuiDir_Right, 0.5f, &right_id, &editor_id);

    ImGuiID diagnostics_id = 0;
    ImGuiID canvas_id = 0;
    ImGui::DockBuilderSplitNode(
        right_id, ImGuiDir_Down, 0.28f, &diagnostics_id, &canvas_id);

    ImGui::DockBuilderDockWindow("SMC Editor", editor_id);
    ImGui::DockBuilderDockWindow("canvas", canvas_id);
    ImGui::DockBuilderDockWindow("Status / Diagnostics", diagnostics_id);
    ImGui::DockBuilderFinish(dockspace_id);
}

void draw_dockspace() {
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |ImGuiWindowFlags_NoBackground;

    bool p_open = true;
    ImGui::Begin("MyDockSpace", &p_open, window_flags);

    ImGui::PopStyleVar(3);

    // DockSpace
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        initialize_default_docking_layout_if_missing(
            dockspace_id, viewport->WorkSize, ImGui::GetIO().IniFilename);
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_AutoHideTabBar);
    }
    ImGui::End();
}
