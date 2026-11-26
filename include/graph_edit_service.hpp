#pragma once

#include <cstddef>
#include <string>

enum class GraphEditOperation {
    create_state,
    rename_state,
    delete_state,
    create_transition,
    update_transition_target,
    delete_transition,
};

struct GraphEditCommand {
    GraphEditOperation operation;
    std::size_t map_index = 0;
    std::size_t state_index = 0;
    std::size_t target_state_index = 0;
    std::size_t edge_index = 0;
    std::string name;
};

struct GraphEditResult {
    bool success = false;
    std::string source;
    std::string error;
};

GraphEditResult apply_graph_edit(
    const std::string& source, const std::string& source_name,
    const GraphEditCommand& command);
