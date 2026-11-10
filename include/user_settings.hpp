#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

#include <dot_layout.hpp>

struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;

struct GraphPreferences {
    std::uint64_t fingerprint = 0;
    std::vector<int> node_shapes;
    std::vector<int> edge_weights;
};

class UserSettings {
public:
    enum class RatioSelection { Default, Fill, Compress, Expand, Numeric };

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
    GraphPreferences graph_preferences;

    void initialize();
    void sync() const;
    void capture_graph(const dot_layout::DirectedGraph& graph);
    void apply_graph_preferences(dot_layout::DirectedGraph& graph) const;

private:
    static void* read_open_func(
        ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
    static void read_line_func(
        ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void write_all_func(
        ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer);
};

void apply_graph_preferences(
    dot_layout::DirectedGraph& graph, const GraphPreferences& preferences);
