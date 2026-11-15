#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>

#include <dot_layout.hpp>

struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;
class DiagnosticLog;
class TextEditor;

struct GraphPreferences {
    std::uint64_t fingerprint = 0;
    std::vector<int> node_shapes;
    std::vector<int> edge_weights;
};

class UserSettings {
public:
    enum class RatioSelection { Default, Fill, Compress, Expand, Numeric };

    struct EditorOptions {
        int tab_size = 4;
        float line_spacing = 1.0f;
        int minimap_columns = 0;
        bool light_palette = false;
        bool read_only = false;
        bool carets_visible = true;
        bool overwrite = false;
        bool word_wrap = false;
        bool line_folding = false;
        bool auto_indent = true;
        bool insert_spaces_on_tabs = false;
        bool show_whitespaces = true;
        bool show_spaces = true;
        bool show_tabs = true;
        bool show_line_numbers = true;
        bool show_current_line_highlight = true;
        bool show_matching_brackets = true;
        bool complete_matching_glyphs = true;
        bool show_minimap = false;
        bool show_scrollbar_minimap = true;
        bool show_pan_scroll_indicator = true;
        bool middle_mouse_pan = true;
        bool find_replace = true;
        int line_number_left_margin = 1;
        int decoration_left_margin = 1;
        int text_left_margin = 2;

        bool operator==(const EditorOptions&) const = default;
    };

    int window_width = 1280;
    int window_height = 800;
    float canvas_scale = 1.0f;
    float rank_separation = 4.0f;
    float node_separation = 1.0f;
    float arrow_size = 10.0f;
    dot_layout::RankDirection rank_direction = dot_layout::RankDirection::LeftToRight;
    bool compound = true;
    RatioSelection ratio_selection = RatioSelection::Default;
    float ratio_value = 1.0f;
    float size_width = 8.0f;
    float size_height = 6.0f;
    EditorOptions editor_options;
    std::vector<std::string> recent_files;
    GraphPreferences graph_preferences;

    void initialize(DiagnosticLog* log = nullptr);
    void sync() const;
    void apply_editor_options(TextEditor& editor) const;
    void capture_editor_options(const TextEditor& editor);
    bool record_recent_file(const std::filesystem::path& path);
    bool prune_recent_files();
    void capture_graph(const dot_layout::DirectedGraph& graph);
    void apply_graph_preferences(dot_layout::DirectedGraph& graph) const;

private:
    DiagnosticLog* log_ = nullptr;
    static void* read_open_func(
        ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
    static void read_line_func(
        ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void write_all_func(
        ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer);
};

void apply_graph_preferences(
    dot_layout::DirectedGraph& graph, const GraphPreferences& preferences);
