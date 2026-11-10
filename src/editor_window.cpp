#include <imgui.h>
#include <TextEditor.h>

#include <analysis.hpp>
#include <app_context.hpp>
#include <ui_windows.hpp>

void render_editor_window(AppContext& app) {
    if (!ImGui::Begin("SMC Editor")) {
        ImGui::End();
        return;
    }

    const bool changed = app.editor->Render("Source", ImVec2(0.0f, 0.0f));
    ImGui::End();
    if (!changed)
        return;

    app.editor_text = app.editor->GetText();
    app.line_offsets = source_line_offsets(app.editor_text);
    if (app.coordinator)
        app.coordinator->text_changed(AnalysisCoordinator::Clock::now());
}
