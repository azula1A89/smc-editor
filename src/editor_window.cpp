#include <imgui.h>
#include <TextEditor.h>
#include <FileSelector.h>

#include <exception>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <analysis.hpp>
#include <app_context.hpp>
#include <ui_windows.hpp>

namespace {

void log_cpp_generation_success(AppContext& app, const CppGenerationFiles& files) {
    app.log.write(LogLevel::info, "C++ generator",
                  "Generated '" + files.header_path.string() + "' and '" +
                      files.source_path.string() + "'");
}

bool open_source_file(AppContext& app, const std::filesystem::path& path) {
    if (path.extension() != ".sm") {
        app.log.write(LogLevel::error, "File", "Only .sm files can be opened.");
        return false;
    }

    const auto contents = read_input_file(path.string(), &app.log);
    if (!contents)
        return false;

    app.input_file = path.string();
    app.editor->SetText(*contents);
    app.editor_text = app.editor->GetText();
    app.saved_text = app.editor_text;
    app.line_offsets = source_line_offsets(app.editor_text);
    app.coordinator->text_changed(AnalysisCoordinator::Clock::now(), app.editor.get());
    app.focus.reset();
    app.canvas_focus.reset();
    if (app.settings.record_recent_file(path))
        app.settings.sync();
    return true;
}

bool save_source_file(AppContext& app) {
    if (app.input_file.empty()) {
        app.log.write(LogLevel::error, "File", "No source file is open.");
        return false;
    }

    std::ofstream output(app.input_file, std::ios::binary | std::ios::trunc);
    if (!output) {
        app.log.write(LogLevel::error, "File",
                      "Cannot save source file '" + app.input_file + "'");
        return false;
    }

    const auto contents = app.editor->GetText();
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    output.close();
    if (!output) {
        app.log.write(LogLevel::error, "File",
                      "Failed while saving source file '" + app.input_file + "'");
        return false;
    }
    app.saved_text = contents;
    app.log.write(LogLevel::info, "File", "Saved '" + app.input_file + "'");
    return true;
}

enum class SelectorMode { open, create, directory };

void request_file_selector(AppContext& app, SelectorMode mode) {
    try {
        auto& selector = FileSelector::Instance();
        if (selector.IsOpen()) {
            app.log.write(LogLevel::error, "File", "A file selector is already open.");
            return;
        }
        auto directory = std::filesystem::path(app.input_file).parent_path();
        if (directory.empty())
            directory = ".";
        if (!selector.SetCurrentPath(directory)) {
            app.log.write(LogLevel::error, "File",
                          "Cannot open folder '" + directory.string() + "'");
            return;
        }
        const bool opened = mode == SelectorMode::directory ? selector.SelectDirectory() :
                            mode == SelectorMode::create ? selector.SaveAs(directory) :
                            selector.OpenFile(".sm");
        if (!opened)
            app.log.write(LogLevel::error, "File", "Cannot open file selector.");
    } catch (const std::exception& error) {
        app.log.write(LogLevel::error, "File", error.what());
    }
}

bool valid_class_name(const std::string& name) {
    static const std::unordered_set<std::string> keywords = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
        "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
        "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
        "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit",
        "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline",
        "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
        "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
        "register", "reinterpret_cast", "requires", "return", "short", "signed",
        "sizeof", "static", "static_assert", "static_cast", "struct", "switch",
        "template", "this", "thread_local", "throw", "true", "try", "typedef",
        "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
        "volatile", "wchar_t", "while", "xor", "xor_eq"
    };
    const auto letter = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    return !name.empty() && letter(name.front()) &&
           std::all_of(name.begin() + 1, name.end(), [&](char c) {
               return letter(c) || (c >= '0' && c <= '9');
           }) && !keywords.contains(name);
}

void create_source_file(AppContext& app, const std::filesystem::path& path) {
    const auto name = path.stem().string();
    const std::string contents = "%start StartMap::Start\n%class " + name +
        "\n%header " + name + ".h\n\n%map StartMap\n%%\nStart\n{\n\n}\n%%\n";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        app.log.write(LogLevel::error, "File", "Cannot create source file '" + path.string() + "'");
        return;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    output.close();
    if (!output) {
        app.log.write(LogLevel::error, "File", "Failed while creating source file '" + path.string() + "'");
        return;
    }
    open_source_file(app, path);
}

void perform_file_action(AppContext& app, FileAction action, const std::filesystem::path& path) {
    switch (action) {
    case FileAction::new_file: request_file_selector(app, SelectorMode::create); break;
    case FileAction::open_file: request_file_selector(app, SelectorMode::open); break;
    case FileAction::open_recent: open_source_file(app, path); break;
    case FileAction::close_window: app.close_confirmed = true; break;
    case FileAction::none: break;
    }
}

void request_file_action(AppContext& app, FileAction action,
                         const std::filesystem::path& path = {}) {
    if (app.pending_file_action != FileAction::none)
        return;
    if (!app.document_modified()) {
        perform_file_action(app, action, path);
        return;
    }
    app.pending_file_action = action;
    app.pending_file_path = path;
    app.open_unsaved_popup = true;
}

void render_unsaved_confirmation(AppContext& app) {
    if (app.open_unsaved_popup) {
        ImGui::OpenPopup("Unsaved changes");
        app.open_unsaved_popup = false;
    }
    bool open = true;
    if (ImGui::BeginPopupModal("Unsaved changes", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes to the current file?");
        bool proceed = false;
        bool cancel = false;
        if (ImGui::Button("Save"))
            proceed = save_source_file(app);
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
            proceed = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            cancel = true;
        if (proceed || cancel) {
            const auto action = app.pending_file_action;
            const auto path = app.pending_file_path;
            app.pending_file_action = FileAction::none;
            app.pending_file_path.clear();
            ImGui::CloseCurrentPopup();
            if (proceed)
                perform_file_action(app, action, path);
        }
        ImGui::EndPopup();
    }
    if (!open) {
        app.pending_file_action = FileAction::none;
        app.pending_file_path.clear();
    }
}

void render_file_selector(AppContext& app) {
    auto& selector = FileSelector::Instance();
    if (!selector.Render())
        return;
    if (selector.SelectedOpenFile()) {
        if (!open_source_file(app, selector.GetSelectedPath()))
            request_file_selector(app, SelectorMode::open);
    } else if (selector.SelectedSaveAs()) {
        const auto selected = selector.GetSelectedPath();
        auto path = selected;
        if (path.extension().empty())
            path += ".sm";
        if (path.extension() != ".sm" || !valid_class_name(path.stem().string())) {
            app.log.write(LogLevel::error, "File",
                          "Use a .sm filename with a valid C++ class identifier.");
            return;
        }
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error) {
            app.log.write(LogLevel::error, "File", "Cannot inspect '" + path.string() +
                          "': " + error.message());
        } else if (exists && path != selected) {
            app.pending_new_file_overwrite = path;
            ImGui::OpenPopup("Overwrite source file?");
        } else {
            create_source_file(app, path);
        }
    } else if (selector.SelectedDirectory()) {
        try {
            auto files = generate_cpp_files(app.editor->GetText(), app.input_file);
            files.header_path = selector.GetSelectedPath() / files.header_path.filename();
            files.source_path = selector.GetSelectedPath() / files.source_path.filename();
            if (std::filesystem::exists(files.header_path) ||
                std::filesystem::exists(files.source_path)) {
                app.pending_cpp_generation = std::move(files);
                ImGui::OpenPopup("Overwrite generated C++ files?");
            } else {
                write_cpp_files(files);
                log_cpp_generation_success(app, files);
            }
        } catch (const std::exception& error) {
            app.pending_cpp_generation.reset();
            app.log.write(LogLevel::error, "C++ generator", error.what());
        }
    }
}

void render_new_file_confirmation(AppContext& app) {
    if (!app.pending_new_file_overwrite)
        return;
    bool open = true;
    if (ImGui::BeginPopupModal("Overwrite source file?", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Overwrite '%s'?", app.pending_new_file_overwrite->string().c_str());
        if (ImGui::Button("Overwrite")) {
            create_source_file(app, *app.pending_new_file_overwrite);
            app.pending_new_file_overwrite.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            app.pending_new_file_overwrite.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
        app.pending_new_file_overwrite.reset();
}

void handle_save_shortcut(AppContext& app) {
    if (!FileSelector::Instance().IsOpen() && app.pending_file_action == FileAction::none &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && !ImGui::GetIO().KeyShift &&
        !ImGui::GetIO().KeySuper && ImGui::IsKeyPressed(ImGuiKey_S, false))
        save_source_file(app);
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

void request_window_close(AppContext& app) {
    app.close_requested = true;
}

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
    if (app.close_requested) {
        app.close_requested = false;
        auto& selector = FileSelector::Instance();
        if (selector.IsOpen()) {
            if (ImGui::BeginPopupModal("###ImGuiFileSelector")) {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            selector.Close();
        }
        request_file_action(app, FileAction::close_window);
    }
    app.editor_visible = false;
    if (!ImGui::Begin("SMC Editor", nullptr, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        handle_save_shortcut(app);
        render_file_selector(app);
        render_cpp_generation_confirmation(app);
        render_new_file_confirmation(app);
        render_unsaved_confirmation(app);
        return;
    }

    app.editor_visible = true;
    const auto previous_editor_options = app.settings.editor_options;
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("file")) {
            if (ImGui::MenuItem("new"))
                request_file_action(app, FileAction::new_file);
            if (ImGui::MenuItem("open"))
                request_file_action(app, FileAction::open_file);
            if (app.settings.prune_recent_files())
                app.settings.sync();
            if (ImGui::BeginMenu("open recent")) {
                if (app.settings.recent_files.empty()) {
                    ImGui::TextDisabled("No recent .sm files");
                } else {
                    const auto recent_files = app.settings.recent_files;
                    for (const auto& path : recent_files) {
                        if (ImGui::MenuItem(path.c_str()))
                            request_file_action(app, FileAction::open_recent, path);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("save", "Ctrl+S"))
                save_source_file(app);
            if (ImGui::BeginMenu("generate")) {
                if (ImGui::MenuItem("generate .hpp and .cpp"))
                    request_file_selector(app, SelectorMode::directory);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
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
		if (ImGui::BeginMenu("options")) {
			if (ImGui::BeginMenu("Tab Size")) {
				if (ImGui::MenuItem("1", NULL, app.editor->GetTabSize() == 1)) { app.editor->SetTabSize(1); }
				if (ImGui::MenuItem("2", NULL, app.editor->GetTabSize() == 2)) { app.editor->SetTabSize(2); }
				if (ImGui::MenuItem("4", NULL, app.editor->GetTabSize() == 4)) { app.editor->SetTabSize(4); }
				if (ImGui::MenuItem("8", NULL, app.editor->GetTabSize() == 8)) { app.editor->SetTabSize(8); }
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Line Spacing")) {
				if (ImGui::MenuItem("1.0", NULL, app.editor->GetLineSpacing() == 1.0f)) { app.editor->SetLineSpacing(1.0f); }
				if (ImGui::MenuItem("1.25", NULL, app.editor->GetLineSpacing() == 1.25f)) { app.editor->SetLineSpacing(1.25f); }
				if (ImGui::MenuItem("1.5", NULL, app.editor->GetLineSpacing() == 1.25f)) { app.editor->SetLineSpacing(1.5f); }
				if (ImGui::MenuItem("1.75", NULL, app.editor->GetLineSpacing() == 1.75f)) { app.editor->SetLineSpacing(1.75f); }
				if (ImGui::MenuItem("2.0", NULL, app.editor->GetLineSpacing() == 2.0f)) { app.editor->SetLineSpacing(2.0f); }
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("MiniMap Columns")) {
				int width = static_cast<int>(app.editor->GetMiniMapColumns());
				if (ImGui::SliderInt("##miniMapColumns", &width, 0, 200)) { app.editor->SetMiniMapColumns(static_cast<size_t>(width)); }
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Color Palette")) {
				if (ImGui::MenuItem("Dark", NULL, !app.settings.editor_options.light_palette)) {
                    ImGui::StyleColorsDark();
                    app.editor->SetPalette(app.editor->GetDarkPalette());
				    app.settings.editor_options.light_palette = false;
                }
				if (ImGui::MenuItem("Light", NULL, app.settings.editor_options.light_palette)) {
                    ImGui::StyleColorsLight();
                    app.editor->SetPalette(app.editor->GetLightPalette());
				    app.settings.editor_options.light_palette = true;
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
        app.settings.capture_editor_options(*app.editor);
        if (previous_editor_options != app.settings.editor_options)
            app.settings.sync();
        ImGui::EndMenuBar();
    }
    const bool changed = app.editor->Render("Source", ImVec2(0.0f, 0.0f));
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        app.canvas_focus.reset();
    ImGui::End();
    handle_save_shortcut(app);
    render_file_selector(app);
    render_cpp_generation_confirmation(app);
    render_new_file_confirmation(app);
    render_unsaved_confirmation(app);
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
