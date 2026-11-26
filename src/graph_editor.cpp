#include <graph_editor.hpp>

GraphEditor::GraphEditor(AppContext& app_context)
    : fsm_(*this), app_(app_context) {}

GraphEditor::~GraphEditor() {}

void GraphEditor::log(const char* message) {
    app_.log.write(LogLevel::debug, "Graph Editor", message);
}

bool GraphEditor::hit_nothing() {
    return !app_.focus;
}

void GraphEditor::render() {
    render_graph_window();
}

bool GraphEditor::hit_node() {
    return app_.focus && app_.focus->kind == GraphObjectKind::state;
}

bool GraphEditor::hit_edge() {
    return app_.focus && app_.focus->kind == GraphObjectKind::guard;
}

bool GraphEditor::hit_cluster() {
    return app_.focus && app_.focus->kind == GraphObjectKind::state_map;
}

bool GraphEditor::is_node_selected() {
    return app_.focus && app_.focus->kind == GraphObjectKind::state;
}

void GraphEditor::select_current_item() {
    app_.focus = *current_item_;
}
void GraphEditor::unselect_current_item() {
    app_.focus = GraphFocus{};
}
void GraphEditor::set_source_node() {
    // Implementation for setting the source node
}
void GraphEditor::set_target_node() {
    // Implementation for setting the target node
}
void GraphEditor::add_edge() {
    // Implementation for adding an edge[between source and target nodes]
}
void GraphEditor::toggle_select_current_item() {
    if (app_.focus && current_item_ && 
        current_item_->kind == app_.focus->kind && 
        current_item_->graph_indices == app_.focus->graph_indices) {
        unselect_current_item();
    } else {
        select_current_item();
    }
}

void GraphEditor::set_as_current_item() {
    if (app_.focus) {
        current_item_ = app_.focus;
    }
}
void GraphEditor::begin_edge_candidate() {
    // Implementation for beginning an edge candidate
}
void GraphEditor::end_edge_candidate() {
    // Implementation for ending an edge candidate
}

void GraphEditor::open_popup(const char* window) {
    // Implementation for showing a popup window
    // on ok: call button_ok_pressed then close the popup
    // on cancel: call button_cancel_pressed then close the popup
}
