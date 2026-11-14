#include <imgui.h>
#include <TextEditor.h>

#include <exception>
#include <filesystem>
#include <map>
#include <string>
#include <utility>

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

std::vector<SourceErrorMarker> source_error_markers(
    const smc::Diagnostics& diagnostics, std::string_view source_name,
    std::size_t line_count) {
    std::map<std::size_t, std::string> messages;
    for (const auto& diagnostic : diagnostics) {
        const auto& location = diagnostic.location;
        if (diagnostic.severity != smc::DiagnosticSeverity::error ||
            location.file != source_name || location.line == 0 || location.line > line_count)
            continue;
        auto& tooltip = messages[location.line - 1];
        if (!tooltip.empty())
            tooltip += '\n';
        tooltip += "Column " + std::to_string(location.column) + ": " + diagnostic.message;
    }

    std::vector<SourceErrorMarker> markers;
    markers.reserve(messages.size());
    for (auto& [line, tooltip] : messages)
        markers.push_back({line, std::move(tooltip)});
    return markers;
}

void set_source_error_markers(
    TextEditor& editor, const smc::Diagnostics& diagnostics, std::string_view source_name) {
    editor.ClearMarkers();
    for (const auto& marker : source_error_markers(
             diagnostics, source_name, editor.GetLineCount())) {
        editor.AddMarker(marker.line, IM_COL32(200, 45, 45, 160),
                         IM_COL32(200, 45, 45, 55), marker.tooltip, marker.tooltip);
    }
}

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
		if (ImGui::BeginMenu("options")) {
			if (ImGui::BeginMenu("Tab Size")) {
				if (ImGui::MenuItem("1")) { app.editor->SetTabSize(1); }
				if (ImGui::MenuItem("2")) { app.editor->SetTabSize(2); }
				if (ImGui::MenuItem("4")) { app.editor->SetTabSize(4); }
				if (ImGui::MenuItem("8")) { app.editor->SetTabSize(8); }
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Line Spacing")) {
				if (ImGui::MenuItem("1.0")) { app.editor->SetLineSpacing(1.0f); }
				if (ImGui::MenuItem("1.25")) { app.editor->SetLineSpacing(1.25f); }
				if (ImGui::MenuItem("1.5")) { app.editor->SetLineSpacing(1.5f); }
				if (ImGui::MenuItem("1.75")) { app.editor->SetLineSpacing(1.75f); }
				if (ImGui::MenuItem("2.0")) { app.editor->SetLineSpacing(2.0f); }
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("MiniMap Columns")) {
				int width = static_cast<int>(app.editor->GetMiniMapColumns());
				if (ImGui::SliderInt("##miniMapColumns", &width, 0, 200)) { app.editor->SetMiniMapColumns(static_cast<size_t>(width)); }
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Color Palette")) {
				if (ImGui::MenuItem("Dark")) {
                    ImGui::StyleColorsDark();
                    app.editor->SetPalette(app.editor->GetDarkPalette());
                }
				if (ImGui::MenuItem("Light")) {
                    ImGui::StyleColorsLight();
                    app.editor->SetPalette(app.editor->GetLightPalette());
                }
				ImGui::EndMenu();
			}

			ImGui::Separator();

			bool flag;
			flag = app.editor->IsReadOnlyEnabled(); if (ImGui::MenuItem("Read Only", nullptr, &flag)) { app.editor->SetReadOnlyEnabled(flag); };
			flag = app.editor->IsCaretsVisible(); if (ImGui::MenuItem("Carets Visible", nullptr, &flag)) { app.editor->SetCaretsVisible(flag); };
			flag = app.editor->IsOverwriteEnabled(); if (ImGui::MenuItem("Overwrite", nullptr, &flag)) { app.editor->SetOverwriteEnabled(flag); };
			flag = app.editor->IsWordWrapEnabled(); if (ImGui::MenuItem("Word Wrap", nullptr, &flag)) { app.editor->SetWordWrapEnabled(flag); };
			flag = app.editor->IsLineFoldingEnabled(); if (ImGui::MenuItem("Line Folding", nullptr, &flag)) { app.editor->SetLineFoldingEnabled(flag); };
			flag = app.editor->IsAutoIndentEnabled(); if (ImGui::MenuItem("Auto Indent", nullptr, &flag)) { app.editor->SetAutoIndentEnabled(flag); };
			flag = app.editor->IsInsertSpacesOnTabs(); if (ImGui::MenuItem("Insert Spaces on Tabs", nullptr, &flag)) { app.editor->SetInsertSpacesOnTabs(flag); };
			flag = app.editor->IsShowWhitespacesEnabled(); if (ImGui::MenuItem("Show Whitespaces", nullptr, &flag)) { app.editor->SetShowWhitespacesEnabled(flag); };
			flag = app.editor->IsShowSpacesEnabled(); if (ImGui::MenuItem("Show Spaces", nullptr, &flag)) { app.editor->SetShowSpacesEnabled(flag); };
			flag = app.editor->IsShowTabsEnabled(); if (ImGui::MenuItem("Show Tabs", nullptr, &flag)) { app.editor->SetShowTabsEnabled(flag); };
			flag = app.editor->IsShowLineNumbersEnabled(); if (ImGui::MenuItem("Show Line Numbers", nullptr, &flag)) { app.editor->SetShowLineNumbersEnabled(flag); };
			flag = app.editor->IsShowCurrentLineHighlightEnabled(); if (ImGui::MenuItem("Show Current Line Highlight", nullptr, &flag)) { app.editor->SetShowCurrentLineHighlightEnabled(flag); };
			flag = app.editor->IsShowingMatchingBrackets(); if (ImGui::MenuItem("Show Matching Brackets", nullptr, &flag)) { app.editor->SetShowMatchingBrackets(flag); };
			flag = app.editor->IsCompletingPairedGlyphs(); if (ImGui::MenuItem("Complete Matching Glyphs", nullptr, &flag)) { app.editor->SetCompletePairedGlyphs(flag); };
			flag = app.editor->IsShowMiniMapEnabled(); if (ImGui::MenuItem("Show Mini Map", nullptr, &flag)) { app.editor->SetShowMiniMapEnabled(flag); };
			flag = app.editor->IsShowScrollbarMiniMapEnabled(); if (ImGui::MenuItem("Show Scrollbar Mini Map", nullptr, &flag)) { app.editor->SetShowScrollbarMiniMapEnabled(flag); };
			flag = app.editor->IsShowPanScrollIndicatorEnabled(); if (ImGui::MenuItem("Show Pan/Scroll Indicator", nullptr, &flag)) { app.editor->SetShowPanScrollIndicatorEnabled(flag); };
			flag = app.editor->IsMiddleMousePanMode(); if (ImGui::MenuItem("Middle Mouse Pan Mode", nullptr, &flag)) { if (flag) app.editor->SetMiddleMousePanMode(); else app.editor->SetMiddleMouseScrollMode(); };
			flag = app.editor->IsFindReplaceEnabled(); if (ImGui::MenuItem("Find/Replace Enabled", nullptr, &flag)) { app.editor->SetFindReplaceEnabled(flag); };

			ImGui::Separator();

			if (ImGui::BeginMenu("Line Number Left Margin")) {
				int margin = static_cast<int>(app.editor->GetLineNumberLeftMargin());
				if (ImGui::SliderInt("##margin", &margin, 0, 4)) { app.editor->SetLineNumberLeftMargin(static_cast<size_t>(margin)); }
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Decoration Left Margin")) {
				int margin = static_cast<int>(app.editor->GetDecorationLeftMargin());
				if (ImGui::SliderInt("##margin", &margin, 0, 4)) { app.editor->SetDecorationLeftMargin(static_cast<size_t>(margin)); }
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Text Left Margin")) {
				int margin = static_cast<int>(app.editor->GetTextLeftMargin());
				if (ImGui::SliderInt("##margin", &margin, 0, 4)) { app.editor->SetTextLeftMargin(static_cast<size_t>(margin)); }
				ImGui::EndMenu();
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
        app.coordinator->text_changed(AnalysisCoordinator::Clock::now(), app.editor.get());
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
