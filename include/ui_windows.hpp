#pragma once

#include <optional>

#include <dot_layout.hpp>

class AppContext;
struct GraphFocus;

void draw_dockspace();
void render_editor_window(AppContext& app);
void render_graph_window(AppContext& app);
void render_log_window(AppContext& app);
std::optional<dot_layout::Point> focused_graph_point(
    const GraphFocus& focus, const dot_layout::DirectedGraph& graph,
    const dot_layout::LayoutResult& layout);
