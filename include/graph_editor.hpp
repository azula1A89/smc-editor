#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include <GraphEditorContext.hpp>
#include <app_context.hpp>

struct GraphEditCommand;

class GraphEditor {
    enum class PopupMode { none, create_state, edit_item };

    GraphEditorContext fsm_;
    AppContext& app_;
    std::optional<GraphFocus> current_item_;
    std::optional<GraphFocus> source_node_;
    std::optional<GraphFocus> target_node_;
    PopupMode popup_mode_ = PopupMode::none;
    std::array<char, 128> state_name_input_{};
    std::size_t popup_map_index_ = 0;
    std::size_t popup_target_state_index_ = 0;
    std::string popup_error_;

public:
    GraphEditor(AppContext& app_context);
    ~GraphEditor();

    void render();
    void log(const char* message);

    void left_click() { fsm_.left_click(); }
    void double_click() { fsm_.double_click(); }
    void left_shift_down() { fsm_.left_shift_down(); }
    void esc_down() { fsm_.esc_down(); }
    void delete_pressed() { fsm_.delete_pressed(); }
    void button_cancel_pressed();
    void button_ok_pressed();
    void set_current_item(std::optional<GraphFocus> item);

    bool hit_nothing();
    bool hit_node();
    bool hit_edge();
    bool hit_cluster();
    bool is_node_selected();
    bool has_current_item();

    void select_current_item();
    void unselect_current_item();
    void set_source_node();
    void set_target_node();
    void add_edge();
    void delete_current_item();
    void toggle_select_current_item();
    void begin_edge_candidate();
    void end_edge_candidate();
    void open_popup(const char* window);

private:
    void render_graph_window();
    void render_popups();
    bool commit_popup();
    bool apply_edit(const GraphEditCommand& command);
};
