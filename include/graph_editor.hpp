#pragma once
#include <GraphEditorContext.hpp>
#include <app_context.hpp>

class GraphEditor
{
    GraphEditorContext fsm_;
    AppContext& app_;
    std::optional<GraphFocus> current_item_;
    GraphFocus source_node_;
    GraphFocus target_node_;
public:
    GraphEditor(AppContext& app_context);
    ~GraphEditor();
    
    void render();
    void log(const char* message);

    // event input
    void left_click() { fsm_.left_click(); }
    void double_click() { fsm_.double_click(); }
    void left_shift_down() { fsm_.left_shift_down(); }
    void esc_down() { fsm_.esc_down(); }
    void button_cancel_pressed() { fsm_.button_cancel_pressed(); }
    void button_ok_pressed() { fsm_.button_ok_pressed(); }

    // guard functions
    bool hit_nothing();
    bool hit_node();
    bool hit_edge();
    bool hit_cluster();
    bool is_node_selected();

    // action functions
    void select_current_item();
    void unselect_current_item();
    void set_source_node();
    void set_target_node();
    void add_edge();
    void toggle_select_current_item();
    void set_as_current_item();
    void begin_edge_candidate();
    void end_edge_candidate();
    void open_popup(const char* window);

private:
    void render_graph_window();
    void render_node_properties_panel();
    void render_edge_properties_panel();
    void render_cluster_properties_panel();
};
