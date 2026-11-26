#include <algorithm>
#include <cstring>
#include <string>

#include <imgui.h>

#include <analysis.hpp>
#include <graph_edit_service.hpp>
#include <graph_editor.hpp>

namespace {

bool same_focus(const GraphFocus& left, const GraphFocus& right) {
    return left.kind == right.kind && left.graph_indices == right.graph_indices;
}

void set_input_text(std::array<char, 128>& buffer, const std::string& value) {
    buffer.fill('\0');
    const auto count = std::min(value.size(), buffer.size() - 1);
    std::copy_n(value.begin(), count, buffer.begin());
}

} // namespace

GraphEditor::GraphEditor(AppContext& app_context)
    : fsm_(*this), app_(app_context) {}

GraphEditor::~GraphEditor() = default;

void GraphEditor::log(const char* message) {
    app_.log.write(LogLevel::debug, "Graph Editor", message);
}

bool GraphEditor::hit_nothing() {
    return !current_item_;
}

bool GraphEditor::hit_node() {
    return current_item_ && current_item_->kind == GraphObjectKind::state;
}

bool GraphEditor::hit_edge() {
    return current_item_ &&
        (current_item_->kind == GraphObjectKind::guard ||
         current_item_->kind == GraphObjectKind::transition);
}

bool GraphEditor::hit_cluster() {
    return current_item_ && current_item_->kind == GraphObjectKind::state_map;
}

bool GraphEditor::is_node_selected() {
    const auto& selected = current_item_ ? current_item_ : app_.focus;
    return selected && selected->kind == GraphObjectKind::state;
}

bool GraphEditor::has_current_item() {
    return current_item_.has_value() || app_.focus.has_value();
}

void GraphEditor::set_current_item(std::optional<GraphFocus> item) {
    current_item_ = std::move(item);
}

void GraphEditor::select_current_item() {
    if (current_item_)
        app_.focus = *current_item_;
    else if (app_.focus)
        current_item_ = app_.focus;
}

void GraphEditor::unselect_current_item() {
    app_.focus.reset();
    app_.canvas_focus.reset();
    app_.editor->SetCursor({});
}

void GraphEditor::set_source_node() {
    if (current_item_ && current_item_->kind == GraphObjectKind::state)
        source_node_ = current_item_;
    else if (app_.focus && app_.focus->kind == GraphObjectKind::state)
        source_node_ = app_.focus;
    else
        source_node_.reset();
}

void GraphEditor::set_target_node() {
    if (current_item_ && current_item_->kind == GraphObjectKind::state)
        target_node_ = current_item_;
    else
        target_node_.reset();
}

bool GraphEditor::apply_edit(const GraphEditCommand& command) {
    if (!app_.editor) {
        popup_error_ = "No SMC source document is open.";
        app_.log.write(LogLevel::error, "Graph Editor", popup_error_);
        return false;
    }

    const auto source = app_.editor->GetText();
    auto result = apply_graph_edit(source, app_.input_file, command);
    if (!result.success) {
        popup_error_ = result.error;
        app_.log.write(LogLevel::error, "Graph Editor", popup_error_);
        return false;
    }
    if (result.source == source)
        return true;

    const auto offsets = source_line_offsets(source);
    const auto end = editor_position_at_offset(source, offsets, source.size());
    app_.editor->ReplaceSectionText(TextEditor::DocPos(0, 0), end, result.source);
    app_.editor_text = app_.editor->GetText();
    app_.line_offsets = source_line_offsets(app_.editor_text);
    app_.canvas_focus.reset();
    app_.focus.reset();
    app_.canvas.previous_focus.reset();
    current_item_.reset();
    source_node_.reset();
    target_node_.reset();
    if (app_.coordinator)
        app_.coordinator->text_changed(AnalysisCoordinator::Clock::now(), app_.editor.get());
    popup_error_.clear();
    return true;
}

void GraphEditor::add_edge() {
    if (!source_node_ || !target_node_ ||
        source_node_->graph_indices.empty() || target_node_->graph_indices.empty()) {
        app_.log.write(LogLevel::warning, "Graph Editor",
                       "Select both a source state and a target state to create a transition.");
        return;
    }
    GraphEditCommand command;
    command.operation = GraphEditOperation::create_transition;
    command.state_index = source_node_->graph_indices.front();
    command.target_state_index = target_node_->graph_indices.front();
    apply_edit(command);
}

void GraphEditor::delete_current_item() {
    const auto selected = current_item_ ? current_item_ : app_.focus;
    if (!selected || selected->graph_indices.empty()) {
        app_.log.write(LogLevel::warning, "Graph Editor",
                       "There is no graph element to delete.");
        return;
    }

    GraphEditCommand command;
    if (selected->kind == GraphObjectKind::state) {
        command.operation = GraphEditOperation::delete_state;
        command.state_index = selected->graph_indices.front();
    } else if (selected->kind == GraphObjectKind::guard ||
               selected->kind == GraphObjectKind::transition) {
        command.operation = GraphEditOperation::delete_transition;
        command.edge_index = selected->graph_indices.front();
    } else {
        app_.log.write(LogLevel::warning, "Graph Editor",
                       "Deleting state maps is not supported yet.");
        return;
    }
    if (!apply_edit(command))
        app_.focus = selected;
}

void GraphEditor::toggle_select_current_item() {
    if (!current_item_)
        return;
    if (!app_.focus || !same_focus(*current_item_, *app_.focus))
        select_current_item();
}

void GraphEditor::begin_edge_candidate() {
    target_node_.reset();
    if (!source_node_)
        set_source_node();
}

void GraphEditor::end_edge_candidate() {
    source_node_.reset();
    target_node_.reset();
}

void GraphEditor::open_popup(const char* window) {
    popup_error_.clear();
    if (std::string(window) == "create node window") {
        popup_mode_ = PopupMode::create_state;
        set_input_text(state_name_input_, "NewState");
        popup_map_index_ = 0;
        if (current_item_ && current_item_->kind == GraphObjectKind::state_map &&
            !current_item_->graph_indices.empty())
            popup_map_index_ = current_item_->graph_indices.front();
    } else if (std::string(window) == "property window") {
        popup_mode_ = PopupMode::edit_item;
        const auto selected = current_item_ ? current_item_ : app_.focus;
        if (selected && selected->kind == GraphObjectKind::state &&
            !selected->graph_indices.empty() &&
            selected->graph_indices.front() < app_.canvas.graph.nodes.size()) {
            set_input_text(state_name_input_,
                app_.canvas.graph.nodes[selected->graph_indices.front()].label);
        } else {
            state_name_input_.fill('\0');
        }
        if (selected && selected->kind == GraphObjectKind::guard &&
            !selected->graph_indices.empty() &&
            selected->graph_indices.front() < app_.canvas.graph.edges.size()) {
            const auto& edge =
                app_.canvas.graph.edges[selected->graph_indices.front()];
            const auto node = std::find_if(
                app_.canvas.graph.nodes.begin(), app_.canvas.graph.nodes.end(),
                [&](const auto& candidate) { return candidate.id == edge.to; });
            if (node != app_.canvas.graph.nodes.end()) {
                popup_target_state_index_ = static_cast<std::size_t>(
                    std::distance(app_.canvas.graph.nodes.begin(), node));
            }
        }
    } else {
        app_.log.write(LogLevel::error, "Graph Editor",
                       "Unknown graph editor popup requested.");
        return;
    }
    ImGui::OpenPopup(window);
}

bool GraphEditor::commit_popup() {
    GraphEditCommand command;
    if (popup_mode_ == PopupMode::create_state) {
        command.operation = GraphEditOperation::create_state;
        command.map_index = popup_map_index_;
        command.name = state_name_input_.data();
        return apply_edit(command);
    }
    if (popup_mode_ == PopupMode::edit_item) {
        const auto selected = current_item_ ? current_item_ : app_.focus;
        if (!selected || selected->graph_indices.empty()) {
            popup_error_.clear();
            return true;
        }
        if (selected->kind == GraphObjectKind::guard) {
            command.operation = GraphEditOperation::update_transition_target;
            command.edge_index = selected->graph_indices.front();
            command.target_state_index = popup_target_state_index_;
            return apply_edit(command);
        }
        if (selected->kind != GraphObjectKind::state)
            return true;
        const auto index = selected->graph_indices.front();
        if (index >= app_.canvas.graph.nodes.size()) {
            popup_error_ = "The selected state no longer exists.";
            return false;
        }
        command.operation = GraphEditOperation::rename_state;
        command.state_index = index;
        command.name = state_name_input_.data();
        if (command.name == app_.canvas.graph.nodes[index].label)
            return true;
        return apply_edit(command);
    }
    return true;
}

void GraphEditor::button_cancel_pressed() {
    popup_mode_ = PopupMode::none;
    popup_error_.clear();
    ImGui::CloseCurrentPopup();
    fsm_.button_cancel_pressed();
}

void GraphEditor::button_ok_pressed() {
    if (!commit_popup())
        return;
    popup_mode_ = PopupMode::none;
    ImGui::CloseCurrentPopup();
    fsm_.button_ok_pressed();
}

void GraphEditor::render_popups() {
    if (popup_mode_ == PopupMode::create_state &&
        ImGui::BeginPopupModal("create node window", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("State name", state_name_input_.data(), state_name_input_.size());
        if (!app_.canvas.graph.clusters.empty()) {
            const auto selected_map = std::min(
                popup_map_index_, app_.canvas.graph.clusters.size() - 1);
            if (ImGui::BeginCombo("State map",
                    app_.canvas.graph.clusters[selected_map].label.c_str())) {
                for (std::size_t i = 0; i < app_.canvas.graph.clusters.size(); ++i) {
                    const bool selected = i == popup_map_index_;
                    if (ImGui::Selectable(
                            app_.canvas.graph.clusters[i].label.c_str(), selected))
                        popup_map_index_ = i;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        if (!popup_error_.empty())
            ImGui::TextWrapped("%s", popup_error_.c_str());
        if (ImGui::Button("Create"))
            button_ok_pressed();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            button_cancel_pressed();
        ImGui::EndPopup();
    }

    if (popup_mode_ == PopupMode::edit_item &&
        ImGui::BeginPopupModal("property window", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto selected = current_item_ ? current_item_ : app_.focus;
        const bool state_selected = selected &&
            selected->kind == GraphObjectKind::state &&
            !selected->graph_indices.empty();
        const bool edge_selected = selected &&
            selected->kind == GraphObjectKind::guard &&
            !selected->graph_indices.empty();
        if (state_selected) {
            ImGui::InputText("State name", state_name_input_.data(),
                             state_name_input_.size());
        } else if (edge_selected) {
            const auto preview = popup_target_state_index_ <
                    app_.canvas.graph.nodes.size()
                ? app_.canvas.graph.nodes[popup_target_state_index_].label.c_str()
                : "Select a target";
            if (ImGui::BeginCombo("Target state", preview)) {
                for (std::size_t i = 0; i < app_.canvas.graph.nodes.size(); ++i) {
                    const auto& node = app_.canvas.graph.nodes[i];
                    if (node.label == "Default")
                        continue;
                    const bool selected_target = i == popup_target_state_index_;
                    if (ImGui::Selectable(node.label.c_str(), selected_target))
                        popup_target_state_index_ = i;
                    if (selected_target)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        } else if (selected &&
                   (selected->kind == GraphObjectKind::transition ||
                    selected->kind == GraphObjectKind::guard)) {
            ImGui::TextUnformatted("Select a specific edge to edit its target.");
        } else {
            ImGui::TextUnformatted("State-map properties are not editable yet.");
        }
        if (!popup_error_.empty())
            ImGui::TextWrapped("%s", popup_error_.c_str());
        if ((state_selected || edge_selected) && ImGui::Button("Apply"))
            button_ok_pressed();
        if (selected && (state_selected || edge_selected ||
                         selected->kind == GraphObjectKind::guard ||
                         selected->kind == GraphObjectKind::transition)) {
            if (state_selected || edge_selected)
                ImGui::SameLine();
            if (ImGui::Button(state_selected ? "Delete state" : "Delete transition")) {
                ImGui::CloseCurrentPopup();
                popup_mode_ = PopupMode::none;
                delete_pressed();
            }
        }
        if (state_selected || edge_selected)
            ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            button_cancel_pressed();
        ImGui::EndPopup();
    }
}
