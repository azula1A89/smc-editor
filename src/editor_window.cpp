#include <imgui.h>
#include <TextEditor.h>

#include <exception>
#include <filesystem>
#include <string>

#include <analysis.hpp>
#include <app_context.hpp>
#include <ui_windows.hpp>

namespace {

void log_cpp_generation_success(AppContext& app, const CppGenerationFiles& files) {
    app.log.write(LogLevel::info, "C++ generator",
                  "Generated '" + files.header_path.string() + "' and '" +
                      files.source_path.string() + "'");
}

void render_cpp_generation_confirmation(AppContext& app) {
    if (!app.pending_cpp_generation)
        return;
    bool open = true;
    if (ImGui::BeginPopupModal(
            "Overwrite generated C++ files?", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& files = *app.pending_cpp_generation;
        ImGui::Text("These output files already exist:");
        ImGui::TextWrapped("%s", files.header_path.string().c_str());
        ImGui::TextWrapped("%s", files.source_path.string().c_str());
        if (ImGui::Button("Overwrite")) {
            try {
                write_cpp_files(files);
                log_cpp_generation_success(app, files);
            } catch (const std::exception& error) {
                app.log.write(LogLevel::error, "C++ generator", error.what());
            }
            app.pending_cpp_generation.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            app.pending_cpp_generation.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
        app.pending_cpp_generation.reset();
}

void render_ast_node(AppContext& app, const AstTreeView& ast, std::size_t index) {
    const auto& node = ast.nodes[index];
    ImGui::PushID(static_cast<int>(index));
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DrawLinesToNodes |
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (app.canvas_focus && app.canvas_focus->focus.kind == node.source.kind &&
        app.canvas_focus->focus.graph_indices == node.source.graph_indices)
        flags |= ImGuiTreeNodeFlags_Selected;
    const bool open = ImGui::TreeNodeEx("node", flags, "%s", node.label.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        select_graph_source(app, node.source);
    if (open && !node.children.empty()) {
        for (const auto child : node.children)
            render_ast_node(app, ast, child);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace

void select_graph_source(AppContext& app, const SourceSegment& source) {
    const auto start = editor_position_at_offset(
        app.editor_text, app.line_offsets, source.begin);
    const auto end = editor_position_at_offset(
        app.editor_text, app.line_offsets, source.end);
    app.editor->SetCursor(start);
    app.editor->SelectRegion(start, end);
    app.editor->ScrollToLine(start.line);
    app.focus = GraphFocus{source.kind, source.graph_indices};
    app.canvas_focus = CanvasFocus{
        *app.focus, app.editor->GetMainCursorPosition(),
        app.editor->GetMainCursorSelection(), app.editor->GetNumberOfCursors()};
}

void render_editor_window(AppContext& app) {
    if (!ImGui::Begin("SMC Editor", nullptr, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        render_cpp_generation_confirmation(app);
        return;
    }

    bool open_cpp_overwrite_popup = false;
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("ast")) {
            if (!app.coordinator || !app.coordinator->source_is_indexed()) {
                ImGui::TextDisabled("AST unavailable until analysis succeeds");
            } else {
                const auto& ast = app.coordinator->ast();
                if (ast.roots.empty())
                    ImGui::TextDisabled("No AST nodes");
                for (const auto root : ast.roots)
                    render_ast_node(app, ast, root);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("generate")) {
            if (ImGui::MenuItem("generate .hpp and .cpp")) {
                try {
                    app.pending_cpp_generation = generate_cpp_files(
                        app.editor->GetText(), app.input_file);
                    const auto& files = *app.pending_cpp_generation;
                    if (std::filesystem::exists(files.header_path) ||
                        std::filesystem::exists(files.source_path)) {
                        open_cpp_overwrite_popup = true;
                    } else {
                        write_cpp_files(files);
                        log_cpp_generation_success(app, files);
                        app.pending_cpp_generation.reset();
                    }
                } catch (const std::exception& error) {
                    app.pending_cpp_generation.reset();
                    app.log.write(LogLevel::error, "C++ generator", error.what());
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    if (open_cpp_overwrite_popup)
        ImGui::OpenPopup("Overwrite generated C++ files?");
    render_cpp_generation_confirmation(app);
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
