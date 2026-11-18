#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>
#include <TextEditor.h>

#include <user_settings.hpp>
#include <log.hpp>

namespace {

bool parse_integer(std::string_view value, int& result) {
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

bool parse_float(std::string_view value, float& result) {
    const std::string text(value);
    char* end = nullptr;
    result = std::strtof(text.c_str(), &end);
    return end != text.c_str() && end == text.c_str() + text.size() &&
           std::isfinite(result);
}

bool parse_boolean(std::string_view value, bool& result) {
    if (value != "0" && value != "1")
        return false;
    result = value == "1";
    return true;
}

std::uint64_t fingerprint(const dot_layout::DirectedGraph& graph) {
    std::uint64_t value = 14695981039346656037ull;
    const auto add_bytes = [&](const char* data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            value ^= static_cast<unsigned char>(data[i]);
            value *= 1099511628211ull;
        }
    };
    const auto add_string = [&](const std::string& text) {
        const auto add_integer = [&](std::uint64_t integer) {
            for (unsigned int shift = 0; shift < 64; shift += 8) {
                const char byte = static_cast<char>((integer >> shift) & 0xffu);
                add_bytes(&byte, 1);
            }
        };
        add_integer(text.size());
        add_bytes(text.data(), text.size());
    };
    const auto add_count = [&](std::size_t count) {
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            const char byte = static_cast<char>(
                (static_cast<std::uint64_t>(count) >> shift) & 0xffu);
            add_bytes(&byte, 1);
        }
    };

    add_count(graph.nodes.size());
    for (const auto& node : graph.nodes) {
        add_string(node.id);
        add_string(node.label);
    }
    add_count(graph.edges.size());
    for (const auto& edge : graph.edges) {
        add_string(edge.from);
        add_string(edge.to);
        add_string(edge.label);
    }
    add_count(graph.clusters.size());
    for (const auto& cluster : graph.clusters) {
        add_string(cluster.id);
        add_string(cluster.label);
        add_string(cluster.parent.value_or(""));
        add_count(cluster.nodes.size());
        for (const auto& node : cluster.nodes)
            add_string(node);
    }
    return value;
}

} // namespace

void* UserSettings::read_open_func(
    ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) {
    return std::string_view(name) == "Data" ? handler->UserData : nullptr;
}

void UserSettings::read_line_func(
    ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
    auto& settings = *static_cast<UserSettings*>(entry);
    const std::string_view text(line);
    const auto separator = text.find('=');
    if (separator == std::string_view::npos)
        return;

    const auto key = text.substr(0, separator);
    const auto value = text.substr(separator + 1);
    int integer = 0;
    float floating = 0.0f;
    bool valid = false;

    if (key == "window_width" || key == "window_height") {
        valid = parse_integer(value, integer) && integer >= 320 && integer <= 10000;
        if (valid && key == "window_width")
            settings.window_width = integer;
        else if (valid)
            settings.window_height = integer;
    } else if (key == "canvas_scale" || key == "rank_separation" ||
               key == "node_separation" || key == "arrow_size" ||
               key == "ratio_value" || key == "size_width" || key == "size_height" ||
               key == "focus_red" || key == "focus_green" || key == "focus_blue" ||
               key == "focus_thickness" || key == "secondary_focus_thickness") {
        valid = parse_float(value, floating);
        if (key == "canvas_scale")
            valid = valid && floating >= 0.2f && floating <= 3.0f;
        else if (key == "arrow_size")
            valid = valid && floating >= 0.0f && floating <= 100.0f;
        else if (key == "focus_red" || key == "focus_green" || key == "focus_blue")
            valid = valid && floating >= 0.0f && floating <= 1.0f;
        else
            valid = valid && floating > 0.0f && floating <= 100.0f;
        if (valid && key == "canvas_scale")
            settings.canvas_scale = floating;
        else if (valid && key == "rank_separation")
            settings.rank_separation = floating;
        else if (valid && key == "node_separation")
            settings.node_separation = floating;
        else if (valid && key == "arrow_size")
            settings.arrow_size = floating;
        else if (valid && key == "ratio_value")
            settings.ratio_value = floating;
        else if (valid && key == "size_width")
            settings.size_width = floating;
        else if (valid && key == "size_height")
            settings.size_height = floating;
        else if (valid && key == "focus_red")
            settings.focus_color[0] = floating;
        else if (valid && key == "focus_green")
            settings.focus_color[1] = floating;
        else if (valid && key == "focus_blue")
            settings.focus_color[2] = floating;
        else if (valid && key == "focus_thickness")
            settings.focus_thickness = floating;
        else if (valid && key == "secondary_focus_thickness")
            settings.secondary_focus_thickness = floating;
    } else if (key == "rank_direction") {
        valid = parse_integer(value, integer) &&
                integer >= static_cast<int>(dot_layout::RankDirection::TopToBottom) &&
                integer <= static_cast<int>(dot_layout::RankDirection::RightToLeft);
        if (valid)
            settings.rank_direction = static_cast<dot_layout::RankDirection>(integer);
    } else if (key == "compound") {
        valid = value == "0" || value == "1";
        if (valid)
            settings.compound = value == "1";
    } else if (key == "ratio_selection") {
        valid = parse_integer(value, integer) &&
                integer >= static_cast<int>(RatioSelection::Default) &&
                integer <= static_cast<int>(RatioSelection::Numeric);
        if (valid)
            settings.ratio_selection = static_cast<RatioSelection>(integer);
    } else if (key == "editor_tab_size" || key == "editor_minimap_columns" ||
               key == "editor_line_number_left_margin" ||
               key == "editor_decoration_left_margin" ||
               key == "editor_text_left_margin") {
        valid = parse_integer(value, integer);
        if (key == "editor_tab_size")
            valid = valid && (integer == 1 || integer == 2 || integer == 4 || integer == 8);
        else if (key == "editor_minimap_columns")
            valid = valid && integer >= 0 && integer <= 200;
        else
            valid = valid && integer >= 0 && integer <= 4;
        if (valid && key == "editor_tab_size")
            settings.editor_options.tab_size = integer;
        else if (valid && key == "editor_minimap_columns")
            settings.editor_options.minimap_columns = integer;
        else if (valid && key == "editor_line_number_left_margin")
            settings.editor_options.line_number_left_margin = integer;
        else if (valid && key == "editor_decoration_left_margin")
            settings.editor_options.decoration_left_margin = integer;
        else if (valid)
            settings.editor_options.text_left_margin = integer;
    } else if (key == "editor_line_spacing") {
        valid = parse_float(value, floating) && floating >= 1.0f && floating <= 2.0f;
        if (valid)
            settings.editor_options.line_spacing = floating;
    } else if (key.starts_with("editor_")) {
        bool* option = nullptr;
        if (key == "editor_palette_light") option = &settings.editor_options.light_palette;
        else if (key == "editor_read_only") option = &settings.editor_options.read_only;
        else if (key == "editor_carets_visible") option = &settings.editor_options.carets_visible;
        else if (key == "editor_overwrite") option = &settings.editor_options.overwrite;
        else if (key == "editor_word_wrap") option = &settings.editor_options.word_wrap;
        else if (key == "editor_line_folding") option = &settings.editor_options.line_folding;
        else if (key == "editor_auto_indent") option = &settings.editor_options.auto_indent;
        else if (key == "editor_insert_spaces_on_tabs") option = &settings.editor_options.insert_spaces_on_tabs;
        else if (key == "editor_show_whitespaces") option = &settings.editor_options.show_whitespaces;
        else if (key == "editor_show_spaces") option = &settings.editor_options.show_spaces;
        else if (key == "editor_show_tabs") option = &settings.editor_options.show_tabs;
        else if (key == "editor_show_line_numbers") option = &settings.editor_options.show_line_numbers;
        else if (key == "editor_show_current_line_highlight") option = &settings.editor_options.show_current_line_highlight;
        else if (key == "editor_show_matching_brackets") option = &settings.editor_options.show_matching_brackets;
        else if (key == "editor_complete_matching_glyphs") option = &settings.editor_options.complete_matching_glyphs;
        else if (key == "editor_show_minimap") option = &settings.editor_options.show_minimap;
        else if (key == "editor_show_scrollbar_minimap") option = &settings.editor_options.show_scrollbar_minimap;
        else if (key == "editor_show_pan_scroll_indicator") option = &settings.editor_options.show_pan_scroll_indicator;
        else if (key == "editor_middle_mouse_pan") option = &settings.editor_options.middle_mouse_pan;
        else if (key == "editor_find_replace") option = &settings.editor_options.find_replace;
        if (option)
            valid = parse_boolean(value, *option);
        else
            return;
    } else if (key.starts_with("recent_file_")) {
        int index = 0;
        valid = parse_integer(key.substr(std::string_view("recent_file_").size()), index) &&
                index >= 0 && index < 10 && !value.empty() &&
                std::filesystem::path(value).extension() == ".sm";
        if (valid) {
            const auto position = static_cast<std::size_t>(index);
            if (settings.recent_files.size() <= position)
                settings.recent_files.resize(position + 1);
            settings.recent_files[position] = std::string(value);
        }
    } else {
        return;
    }

    if (!valid)
        write_log(settings.log_, LogLevel::warning, "Settings",
                  "Ignoring invalid UserSettings entry '" + std::string(key) + "'");
}

void UserSettings::write_all_func(
    ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer) {
    const auto& settings = *static_cast<const UserSettings*>(handler->UserData);
    buffer->appendf("[%s][Data]\n", handler->TypeName);
    buffer->appendf("window_width=%d\n", settings.window_width);
    buffer->appendf("window_height=%d\n", settings.window_height);
    buffer->appendf("canvas_scale=%.6g\n", settings.canvas_scale);
    buffer->appendf("rank_separation=%.6g\n", settings.rank_separation);
    buffer->appendf("node_separation=%.6g\n", settings.node_separation);
    buffer->appendf("arrow_size=%.6g\n", settings.arrow_size);
    buffer->appendf("focus_red=%.9g\n", settings.focus_color[0]);
    buffer->appendf("focus_green=%.9g\n", settings.focus_color[1]);
    buffer->appendf("focus_blue=%.9g\n", settings.focus_color[2]);
    buffer->appendf("focus_thickness=%.9g\n", settings.focus_thickness);
    buffer->appendf("secondary_focus_thickness=%.9g\n", settings.secondary_focus_thickness);
    buffer->appendf("rank_direction=%d\n", static_cast<int>(settings.rank_direction));
    buffer->appendf("compound=%d\n", settings.compound ? 1 : 0);
    buffer->appendf("ratio_selection=%d\n", static_cast<int>(settings.ratio_selection));
    buffer->appendf("ratio_value=%.6g\n", settings.ratio_value);
    buffer->appendf("size_width=%.6g\n", settings.size_width);
    buffer->appendf("size_height=%.6g\n", settings.size_height);
    const auto& editor = settings.editor_options;
    buffer->appendf("editor_tab_size=%d\n", editor.tab_size);
    buffer->appendf("editor_line_spacing=%.6g\n", editor.line_spacing);
    buffer->appendf("editor_minimap_columns=%d\n", editor.minimap_columns);
    buffer->appendf("editor_palette_light=%d\n", editor.light_palette ? 1 : 0);
    buffer->appendf("editor_read_only=%d\n", editor.read_only ? 1 : 0);
    buffer->appendf("editor_carets_visible=%d\n", editor.carets_visible ? 1 : 0);
    buffer->appendf("editor_overwrite=%d\n", editor.overwrite ? 1 : 0);
    buffer->appendf("editor_word_wrap=%d\n", editor.word_wrap ? 1 : 0);
    buffer->appendf("editor_line_folding=%d\n", editor.line_folding ? 1 : 0);
    buffer->appendf("editor_auto_indent=%d\n", editor.auto_indent ? 1 : 0);
    buffer->appendf("editor_insert_spaces_on_tabs=%d\n", editor.insert_spaces_on_tabs ? 1 : 0);
    buffer->appendf("editor_show_whitespaces=%d\n", editor.show_whitespaces ? 1 : 0);
    buffer->appendf("editor_show_spaces=%d\n", editor.show_spaces ? 1 : 0);
    buffer->appendf("editor_show_tabs=%d\n", editor.show_tabs ? 1 : 0);
    buffer->appendf("editor_show_line_numbers=%d\n", editor.show_line_numbers ? 1 : 0);
    buffer->appendf("editor_show_current_line_highlight=%d\n", editor.show_current_line_highlight ? 1 : 0);
    buffer->appendf("editor_show_matching_brackets=%d\n", editor.show_matching_brackets ? 1 : 0);
    buffer->appendf("editor_complete_matching_glyphs=%d\n", editor.complete_matching_glyphs ? 1 : 0);
    buffer->appendf("editor_show_minimap=%d\n", editor.show_minimap ? 1 : 0);
    buffer->appendf("editor_show_scrollbar_minimap=%d\n", editor.show_scrollbar_minimap ? 1 : 0);
    buffer->appendf("editor_show_pan_scroll_indicator=%d\n", editor.show_pan_scroll_indicator ? 1 : 0);
    buffer->appendf("editor_middle_mouse_pan=%d\n", editor.middle_mouse_pan ? 1 : 0);
    buffer->appendf("editor_find_replace=%d\n", editor.find_replace ? 1 : 0);
    buffer->appendf("editor_line_number_left_margin=%d\n", editor.line_number_left_margin);
    buffer->appendf("editor_decoration_left_margin=%d\n", editor.decoration_left_margin);
    buffer->appendf("editor_text_left_margin=%d\n", editor.text_left_margin);
    for (std::size_t i = 0; i < settings.recent_files.size() && i < 10; ++i)
        buffer->appendf("recent_file_%d=%s\n", static_cast<int>(i),
                        settings.recent_files[i].c_str());
    buffer->append("\n\n");
}

void UserSettings::initialize(DiagnosticLog* log) {
    log_ = log;
    ImGuiSettingsHandler handler{};
    handler.TypeName = "UserSettings";
    handler.TypeHash = ImHashStr(handler.TypeName);
    handler.ReadOpenFn = read_open_func;
    handler.ReadLineFn = read_line_func;
    handler.WriteAllFn = write_all_func;
    handler.UserData = this;
    ImGui::AddSettingsHandler(&handler);
    ImGui::LoadIniSettingsFromDisk(ImGui::GetIO().IniFilename);
    if (prune_recent_files())
        sync();
}

void UserSettings::sync() const {
    if (ImGui::GetCurrentContext())
        ImGui::MarkIniSettingsDirty();
}

void UserSettings::apply_editor_options(TextEditor& editor) const {
    const auto& options = editor_options;
    editor.SetTabSize(static_cast<std::size_t>(options.tab_size));
    editor.SetLineSpacing(options.line_spacing);
    editor.SetMiniMapColumns(static_cast<std::size_t>(options.minimap_columns));
    editor.SetReadOnlyEnabled(options.read_only);
    editor.SetCaretsVisible(options.carets_visible);
    editor.SetOverwriteEnabled(options.overwrite);
    editor.SetWordWrapEnabled(options.word_wrap);
    editor.SetShowMatchingBrackets(options.show_matching_brackets);
    editor.SetLineFoldingEnabled(options.line_folding);
    editor.SetAutoIndentEnabled(options.auto_indent);
    editor.SetInsertSpacesOnTabs(options.insert_spaces_on_tabs);
    editor.SetShowWhitespacesEnabled(options.show_whitespaces);
    editor.SetShowSpacesEnabled(options.show_spaces);
    editor.SetShowTabsEnabled(options.show_tabs);
    editor.SetShowLineNumbersEnabled(options.show_line_numbers);
    editor.SetShowCurrentLineHighlightEnabled(options.show_current_line_highlight);
    editor.SetCompletePairedGlyphs(options.complete_matching_glyphs);
    editor.SetShowMiniMapEnabled(options.show_minimap);
    editor.SetShowScrollbarMiniMapEnabled(options.show_scrollbar_minimap);
    editor.SetShowPanScrollIndicatorEnabled(options.show_pan_scroll_indicator);
    if (options.middle_mouse_pan)
        editor.SetMiddleMousePanMode();
    else
        editor.SetMiddleMouseScrollMode();
    editor.SetFindReplaceEnabled(options.find_replace);
    editor.SetLineNumberLeftMargin(static_cast<std::size_t>(options.line_number_left_margin));
    editor.SetDecorationLeftMargin(static_cast<std::size_t>(options.decoration_left_margin));
    editor.SetTextLeftMargin(static_cast<std::size_t>(options.text_left_margin));
    if (options.light_palette) {
        ImGui::StyleColorsLight();
        editor.SetPalette(TextEditor::GetLightPalette());
    } else {
        ImGui::StyleColorsDark();
        editor.SetPalette(TextEditor::GetDarkPalette());
    }
}

void UserSettings::capture_editor_options(const TextEditor& editor) {
    auto& options = editor_options;
    options.tab_size = static_cast<int>(editor.GetTabSize());
    options.line_spacing = editor.GetLineSpacing();
    options.minimap_columns = static_cast<int>(editor.GetMiniMapColumns());
    options.read_only = editor.IsReadOnlyEnabled();
    options.carets_visible = editor.IsCaretsVisible();
    options.overwrite = editor.IsOverwriteEnabled();
    options.word_wrap = editor.IsWordWrapEnabled();
    options.line_folding = editor.IsLineFoldingEnabled();
    options.auto_indent = editor.IsAutoIndentEnabled();
    options.insert_spaces_on_tabs = editor.IsInsertSpacesOnTabs();
    options.show_whitespaces = editor.IsShowWhitespacesEnabled();
    options.show_spaces = editor.IsShowSpacesEnabled();
    options.show_tabs = editor.IsShowTabsEnabled();
    options.show_line_numbers = editor.IsShowLineNumbersEnabled();
    options.show_current_line_highlight = editor.IsShowCurrentLineHighlightEnabled();
    options.show_matching_brackets = editor.IsShowingMatchingBrackets();
    options.complete_matching_glyphs = editor.IsCompletingPairedGlyphs();
    options.show_minimap = editor.IsShowMiniMapEnabled();
    options.show_scrollbar_minimap = editor.IsShowScrollbarMiniMapEnabled();
    options.show_pan_scroll_indicator = editor.IsShowPanScrollIndicatorEnabled();
    options.middle_mouse_pan = editor.IsMiddleMousePanMode();
    options.find_replace = editor.IsFindReplaceEnabled();
    options.line_number_left_margin = static_cast<int>(editor.GetLineNumberLeftMargin());
    options.decoration_left_margin = static_cast<int>(editor.GetDecorationLeftMargin());
    options.text_left_margin = static_cast<int>(editor.GetTextLeftMargin());
}

bool UserSettings::record_recent_file(const std::filesystem::path& path) {
    if (path.extension() != ".sm") {
        write_log(log_, LogLevel::warning, "Settings",
                  "Not adding non-.sm file to recent files: '" + path.string() + "'");
        return false;
    }
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        write_log(log_, LogLevel::error, "Settings",
                  "Cannot normalize recent file '" + path.string() + "': " + error.message());
        return false;
    }
    const auto normalized = absolute.lexically_normal().string();
    std::erase(recent_files, normalized);
    recent_files.insert(recent_files.begin(), normalized);
    if (recent_files.size() > 10)
        recent_files.resize(10);
    return true;
}

bool UserSettings::prune_recent_files() {
    const auto previous_size = recent_files.size();
    std::vector<std::string> valid;
    valid.reserve(std::min<std::size_t>(recent_files.size(), 10));
    for (const auto& file : recent_files) {
        if (file.empty() || std::filesystem::path(file).extension() != ".sm")
            continue;
        std::error_code error;
        const bool exists = std::filesystem::is_regular_file(file, error);
        if (error) {
            if (error != std::errc::no_such_file_or_directory &&
                error != std::errc::not_a_directory) {
                write_log(log_, LogLevel::warning, "Settings",
                          "Cannot inspect recent file '" + file + "': " + error.message());
                valid.push_back(file);
            }
        } else if (exists &&
                   std::find(valid.begin(), valid.end(), file) == valid.end() &&
                   valid.size() < 10) {
            valid.push_back(file);
        }
    }
    recent_files = std::move(valid);
    return recent_files.size() != previous_size;
}

void UserSettings::capture_graph(const dot_layout::DirectedGraph& graph) {
    graph_preferences.fingerprint = fingerprint(graph);
    graph_preferences.node_shapes.clear();
    graph_preferences.edge_weights.clear();
    graph_preferences.node_shapes.reserve(graph.nodes.size());
    graph_preferences.edge_weights.reserve(graph.edges.size());
    for (const auto& node : graph.nodes)
        graph_preferences.node_shapes.push_back(static_cast<int>(node.shape));
    for (const auto& edge : graph.edges)
        graph_preferences.edge_weights.push_back(edge.weight);
}

void UserSettings::apply_graph_preferences(dot_layout::DirectedGraph& graph) const {
    ::apply_graph_preferences(graph, graph_preferences);
}

void apply_graph_preferences(
    dot_layout::DirectedGraph& graph, const GraphPreferences& preferences) {
    if (preferences.fingerprint == 0 || fingerprint(graph) != preferences.fingerprint ||
        preferences.node_shapes.size() != graph.nodes.size() ||
        preferences.edge_weights.size() != graph.edges.size())
        return;

    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        const int shape = preferences.node_shapes[i];
        if (shape >= 0 && shape <= static_cast<int>(dot_layout::NodeShape::Point))
            graph.nodes[i].shape = static_cast<dot_layout::NodeShape>(shape);
    }
    for (std::size_t i = 0; i < graph.edges.size(); ++i) {
        const int weight = preferences.edge_weights[i];
        if (weight >= 1 && weight <= 1000)
            graph.edges[i].weight = weight;
    }
}
