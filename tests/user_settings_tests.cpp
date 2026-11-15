#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <imgui.h>
#include <TextEditor.h>

#include <user_settings.hpp>
#include <log.hpp>

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

    ImGuiIO& io() const {
        ImGui::SetCurrentContext(context_);
        return ImGui::GetIO();
    }

private:
    ImGuiContext* context_;
};

void testSettingsRoundTrip() {
    const auto path = std::filesystem::temp_directory_path() /
        ("dot_static_user_settings_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".ini");
    const auto directory = path.parent_path() / path.stem();
    require(std::filesystem::create_directory(directory),
            "Could not create settings fixture directory");

    dot_layout::DirectedGraph graph{
        {{"node", "Node", dot_layout::NodeShape::Circle}},
        {{"node", "node", "loop", 17}}};

    {
        const auto path_string = path.string();
        UserSettings settings;
        ImGuiContextGuard context;
        context.io().IniFilename = path_string.c_str();
        settings.initialize();
        settings.window_width = 1600;
        settings.window_height = 900;
        settings.canvas_scale = 1.4f;
        settings.rank_direction = dot_layout::RankDirection::TopToBottom;
        settings.rank_separation = 3.5f;
        settings.node_separation = 0.8f;
        settings.arrow_size = 12.0f;
        settings.compound = false;
        settings.ratio_selection = UserSettings::RatioSelection::Numeric;
        settings.ratio_value = 1.25f;
        settings.size_width = 10.5f;
        settings.size_height = 7.25f;
        auto& editor = settings.editor_options;
        editor.tab_size = 2;
        editor.line_spacing = 1.5f;
        editor.minimap_columns = 80;
        editor.light_palette = true;
        editor.read_only = true;
        editor.carets_visible = false;
        editor.overwrite = true;
        editor.word_wrap = true;
        editor.line_folding = true;
        editor.auto_indent = false;
        editor.insert_spaces_on_tabs = true;
        editor.show_whitespaces = false;
        editor.show_spaces = true;
        editor.show_tabs = false;
        editor.show_line_numbers = false;
        editor.show_current_line_highlight = false;
        editor.show_matching_brackets = true;
        editor.complete_matching_glyphs = false;
        editor.show_minimap = true;
        editor.show_scrollbar_minimap = false;
        editor.show_pan_scroll_indicator = false;
        editor.middle_mouse_pan = false;
        editor.find_replace = false;
        editor.line_number_left_margin = 3;
        editor.decoration_left_margin = 2;
        editor.text_left_margin = 4;
        for (int i = 0; i < 11; ++i) {
            std::ofstream source(directory / ("recent" + std::to_string(i) + ".sm"));
            source << "%map Main\n";
            require(source.good(), "Could not create recent-file fixture");
            require(settings.record_recent_file(directory /
                        ("recent" + std::to_string(i) + ".sm")),
                    "Could not add recent-file fixture");
        }
        require(settings.recent_files.size() == 10,
                "Recent-file history was not capped at ten entries");
        require(settings.record_recent_file(directory / "recent0.sm") &&
                    settings.recent_files.front() ==
                        (directory / "recent0.sm").lexically_normal().string(),
                "Reopening a recent file did not move it to the front");
        settings.capture_graph(graph);
        auto runtime_graph = graph;
        runtime_graph.nodes[0].shape = dot_layout::NodeShape::Box;
        runtime_graph.edges[0].weight = 1;
        settings.apply_graph_preferences(runtime_graph);
        require(runtime_graph.nodes[0].shape == dot_layout::NodeShape::Circle &&
                    runtime_graph.edges[0].weight == 17,
                "Graph preferences were not applied during the current run");
        ImGui::SaveIniSettingsToDisk(path_string.c_str());
    }

    {
        const auto path_string = path.string();
        UserSettings settings;
        ImGuiContextGuard context;
        context.io().IniFilename = path_string.c_str();
        settings.initialize();

        require(settings.window_width == 1600 && settings.window_height == 900,
                "Window dimensions were not restored");
        require(settings.canvas_scale == 1.4f &&
                    settings.rank_direction == dot_layout::RankDirection::TopToBottom &&
                    settings.rank_separation == 3.5f &&
                    settings.node_separation == 0.8f &&
                    settings.arrow_size == 12.0f && !settings.compound,
                "Canvas settings were not restored");
        require(settings.ratio_selection == UserSettings::RatioSelection::Numeric &&
                    settings.ratio_value == 1.25f &&
                    settings.size_width == 10.5f && settings.size_height == 7.25f,
                "Ratio settings were not restored");

        UserSettings::EditorOptions expected_editor;
        expected_editor.tab_size = 2;
        expected_editor.line_spacing = 1.5f;
        expected_editor.minimap_columns = 80;
        expected_editor.light_palette = true;
        expected_editor.read_only = true;
        expected_editor.carets_visible = false;
        expected_editor.overwrite = true;
        expected_editor.word_wrap = true;
        expected_editor.line_folding = true;
        expected_editor.auto_indent = false;
        expected_editor.insert_spaces_on_tabs = true;
        expected_editor.show_whitespaces = false;
        expected_editor.show_spaces = true;
        expected_editor.show_tabs = false;
        expected_editor.show_line_numbers = false;
        expected_editor.show_current_line_highlight = false;
        expected_editor.show_matching_brackets = true;
        expected_editor.complete_matching_glyphs = false;
        expected_editor.show_minimap = true;
        expected_editor.show_scrollbar_minimap = false;
        expected_editor.show_pan_scroll_indicator = false;
        expected_editor.middle_mouse_pan = false;
        expected_editor.find_replace = false;
        expected_editor.line_number_left_margin = 3;
        expected_editor.decoration_left_margin = 2;
        expected_editor.text_left_margin = 4;
        require(settings.editor_options == expected_editor,
                "Editor options were not restored");
        require(settings.recent_files.size() == 10 &&
                    settings.recent_files.front() ==
                        (directory / "recent0.sm").lexically_normal().string() &&
                    settings.recent_files.back() ==
                        (directory / "recent2.sm").lexically_normal().string(),
                "Recent files were not restored in most-recent-first order");

        TextEditor editor;
        settings.apply_editor_options(editor);
        require(editor.GetTabSize() == 2 && editor.GetLineSpacing() == 1.5f &&
                    editor.GetMiniMapColumns() == 80 && editor.IsReadOnlyEnabled() &&
                    !editor.IsCaretsVisible() && editor.IsOverwriteEnabled() &&
                    editor.IsWordWrapEnabled() && editor.IsLineFoldingEnabled() &&
                    !editor.IsAutoIndentEnabled() && editor.IsInsertSpacesOnTabs() &&
                    !editor.IsShowWhitespacesEnabled() && editor.IsShowSpacesEnabled() &&
                    !editor.IsShowTabsEnabled() && !editor.IsShowLineNumbersEnabled() &&
                    !editor.IsShowCurrentLineHighlightEnabled() &&
                    editor.IsShowingMatchingBrackets() &&
                    !editor.IsCompletingPairedGlyphs() && editor.IsShowMiniMapEnabled() &&
                    !editor.IsShowScrollbarMiniMapEnabled() &&
                    !editor.IsShowPanScrollIndicatorEnabled() &&
                    !editor.IsMiddleMousePanMode() && !editor.IsFindReplaceEnabled() &&
                    editor.GetLineNumberLeftMargin() == 3 &&
                    editor.GetDecorationLeftMargin() == 2 &&
                    editor.GetTextLeftMargin() == 4 &&
                    editor.GetPalette() == TextEditor::GetLightPalette(),
                "Editor options were not applied to TextEditor");
        UserSettings captured;
        captured.editor_options.light_palette = true;
        captured.capture_editor_options(editor);
        require(captured.editor_options == expected_editor,
                "TextEditor options were not captured for persistence");

        dot_layout::DirectedGraph restored{
            {{"node", "Node"}},
            {{"node", "node", "loop"}}};
        settings.apply_graph_preferences(restored);
        require(restored.nodes[0].shape == dot_layout::NodeShape::Box &&
                    restored.edges[0].weight == 1,
                "Graph preferences were unexpectedly restored from disk");

        dot_layout::DirectedGraph unrelated{
            {{"node", "Changed graph"}},
            {{"node", "node", "loop"}}};
        settings.apply_graph_preferences(unrelated);
        require(unrelated.nodes[0].shape == dot_layout::NodeShape::Box &&
                    unrelated.edges[0].weight == 1,
                "Preferences from a different graph were applied");
    }

    std::filesystem::remove(path);
    std::filesystem::remove_all(directory);
}

void testLegacyGraphPreferencesAreIgnored() {
    const auto path = std::filesystem::temp_directory_path() /
        ("dot_static_legacy_graph_settings_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".ini");
    {
        std::ofstream file(path);
        file << "[UserSettings][Data]\n"
             << "graph_fingerprint=0000000000000001\n"
             << "node_shapes=1\n"
             << "edge_weights=17\n";
    }
    {
        const auto path_string = path.string();
        UserSettings settings;
        ImGuiContextGuard context;
        context.io().IniFilename = path_string.c_str();
        settings.initialize();
        dot_layout::DirectedGraph graph{
            {{"node", "Node"}},
            {{"node", "node", "loop"}}};
        settings.apply_graph_preferences(graph);
        require(graph.nodes[0].shape == dot_layout::NodeShape::Box &&
                    graph.edges[0].weight == 1,
                "Legacy graph preferences were restored");
    }
    std::filesystem::remove(path);
}

void testMissingRecentFilesArePruned() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("dot_static_recent_pruning_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(directory),
            "Could not create recent-file pruning fixture directory");
    const auto existing = directory / "existing.sm";
    {
        std::ofstream source(existing);
        source << "%map Main\n";
        require(source.good(), "Could not create existing recent-file fixture");
    }
    const auto missing = directory / "missing.sm";
    const auto path = directory / "settings.ini";
    {
        std::ofstream file(path);
        file << "[UserSettings][Data]\n"
             << "recent_file_0=" << existing.string() << '\n'
             << "recent_file_1=" << missing.string() << '\n';
    }
    {
        const auto path_string = path.string();
        UserSettings settings;
        ImGuiContextGuard context;
        context.io().IniFilename = path_string.c_str();
        settings.initialize();
        require(settings.recent_files.size() == 1 &&
                    settings.recent_files.front() ==
                        std::filesystem::absolute(existing).lexically_normal().string(),
                "Missing recent files were not pruned");
    }
    std::filesystem::remove_all(directory);
}

void testInvalidSettingsAreLogged() {
    const auto path = std::filesystem::temp_directory_path() /
        ("dot_static_invalid_settings_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
    {
        std::ofstream file(path);
        file << "[UserSettings][Data]\nwindow_width=invalid\n";
    }
    {
        const auto path_string = path.string();
        DiagnosticLog log(10, false);
        UserSettings settings;
        ImGuiContextGuard context;
        context.io().IniFilename = path_string.c_str();
        settings.initialize(&log);
        const auto records = log.snapshot().records;
        require(records.size() == 1 && records[0].level == LogLevel::warning &&
                    records[0].source == "Settings" &&
                    records[0].detail.find("window_width") != std::string::npos,
                "Invalid settings were not logged");
    }
    std::filesystem::remove(path);
}

} // namespace

int main() {
    try {
        testSettingsRoundTrip();
        testLegacyGraphPreferencesAreIgnored();
        testMissingRecentFilesArePruned();
        testInvalidSettingsAreLogged();
    } catch (const std::exception& error) {
        std::cerr << "user_settings_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
