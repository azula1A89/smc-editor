#pragma once

#include <optional>

#include <imgui.h>
#include <dot_layout.hpp>

class AppContext;
struct GraphFocus;
struct SourceSegment;

void draw_dockspace();
void initialize_default_docking_layout_if_missing(
    ImGuiID dockspace_id, ImVec2 size, const char* ini_filename);
void render_editor_window(AppContext& app);
void request_window_close(AppContext& app);
void update_graph_focus(AppContext& app);
void select_graph_source(AppContext& app, const SourceSegment& source);
void render_graph_window(AppContext& app);
void render_log_window(AppContext& app);
std::optional<dot_layout::Point> focused_graph_point(
    const GraphFocus& focus, const dot_layout::DirectedGraph& graph,
    const dot_layout::LayoutResult& layout);
