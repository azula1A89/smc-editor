#pragma once
#include <GraphEditorContext.hpp>

class GraphEditor
{
    GraphEditorContext fsm_;
public:
    GraphEditor();
    ~GraphEditor();

    // guard functions
    bool none_selected();
    bool node_hovered();
    bool edge_hovered();
    bool cluster_hovered();
    bool node_selected();
    bool edge_selected();
    bool cluster_selected();

    // action functions
    void select_current_item();
    void unselect_current_item();
    void set_source();
    void toggle_select_current_item();
    void apply();
    void set_as_current_item();
    void begin_edge_candidate();
    void end_edge_candidate();
    void set_target();
    void add_edge();
    void show_popup(const char* window);
    void close_popup(const char* window);
};