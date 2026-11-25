#include "graph_editor.h"

GraphEditor::GraphEditor() : fsm_(*this) {
    fsm_.enterStartState();
}

GraphEditor::~GraphEditor() {}

bool GraphEditor::none_selected() {}

bool GraphEditor::node_hovered() {}

bool GraphEditor::edge_hovered() {}

bool GraphEditor::cluster_hovered() {}

bool GraphEditor::node_selected() {}

bool GraphEditor::edge_selected() {}

bool GraphEditor::cluster_selected() {}

void GraphEditor::select_current_item() {}

void GraphEditor::unselect_current_item() {}

void GraphEditor::set_source() {}

void GraphEditor::toggle_select_current_item() {}

void GraphEditor::apply() {}

void GraphEditor::set_as_current_item() {}

void GraphEditor::begin_edge_candidate() {}

void GraphEditor::end_edge_candidate() {}

void GraphEditor::set_target() {}

void GraphEditor::add_edge() {}

void GraphEditor::show_popup(const char* window) {}

void GraphEditor::close_popup(const char* window) {}

