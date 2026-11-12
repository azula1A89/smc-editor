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
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        app.canvas_focus.reset();
    ImGui::End();
    if (!changed)
        return;

    app.editor_text = app.editor->GetText();
    app.line_offsets = source_line_offsets(app.editor_text);
    if (app.coordinator)
        app.coordinator->text_changed(AnalysisCoordinator::Clock::now());
    app.canvas_focus.reset();
}

void update_graph_focus(AppContext& app) {
    if (app.canvas.recently_replaced)
        app.canvas_focus.reset();
    if (app.canvas_focus && app.editor) {
        const auto selection = app.editor->GetMainCursorSelection();
        const auto& saved = *app.canvas_focus;
        if (app.editor->GetMainCursorPosition() != saved.cursor ||
            selection.start != saved.selection.start || selection.end != saved.selection.end ||
            app.editor->GetNumberOfCursors() != saved.cursor_count)
            app.canvas_focus.reset();
    }
    if (app.canvas_focus) {
        app.focus = app.canvas_focus->focus;
    } else {
        app.focus.reset();
        if (app.editor && app.coordinator && app.coordinator->source_is_indexed()) {
            const auto offset = editor_offset_at_position(
                app.editor_text, app.line_offsets, app.editor->GetMainCursorPosition());
            app.focus = focus_at_offset(app.coordinator->source_segments(), offset);
        }
    }
}
