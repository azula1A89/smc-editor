#include <iostream>
#include <stdexcept>
#include <string>

#include <analysis.hpp>
#include <graph_edit_service.hpp>

namespace {

const std::string source =
    "%start Main::Ready\n"
    "%class Context\n"
    "%header context.h\n"
    "%map Main\n"
    "%%\n"
    "// keep this comment\n"
    "Ready\n"
    "{\n"
    "    move Done { stay(); }\n"
    "}\n"
    "Done\n"
    "{\n"
    "}\n"
    "%%\n";

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

dot_layout::DirectedGraph parse_graph(const std::string& text) {
    auto graph = make_graph_from_contents(text, "graph_edit_tests.sm");
    require(graph.has_value(), "Edited source did not parse into a graph");
    return std::move(*graph);
}

void test_create_state_preserves_source() {
    GraphEditCommand command;
    command.operation = GraphEditOperation::create_state;
    command.map_index = 0;
    command.name = "Added";
    const auto result = apply_graph_edit(source, "graph_edit_tests.sm", command);
    require(result.success, "Creating a state failed");
    require(result.source.find("// keep this comment") != std::string::npos,
            "Creating a state discarded an unrelated comment");
    const auto graph = parse_graph(result.source);
    require(graph.nodes.size() == 4 && graph.nodes[2].label == "Added",
            "Created state was not represented in the derived graph");
}

void test_rename_state_updates_references() {
    GraphEditCommand command;
    command.operation = GraphEditOperation::rename_state;
    command.state_index = 0;
    command.name = "Beginning";
    const auto result = apply_graph_edit(source, "graph_edit_tests.sm", command);
    require(result.success, "Renaming a state failed");
    require(result.source.find("%start Main::Beginning") != std::string::npos &&
                result.source.find("move Done") != std::string::npos,
            "Renaming the start state did not update its declaration safely");
    const auto graph = parse_graph(result.source);
    require(graph.nodes.front().label == "Beginning",
            "Renamed state was not represented in the derived graph");
}

void test_create_and_delete_transition() {
    GraphEditCommand create;
    create.operation = GraphEditOperation::create_transition;
    create.state_index = 0;
    create.target_state_index = 1;
    const auto created = apply_graph_edit(source, "graph_edit_tests.sm", create);
    require(created.success, "Creating a transition failed");
    require(created.source.find("graph_transition_1 Done {}") != std::string::npos,
            "Created transition was not written into the source");
    const auto created_graph = parse_graph(created.source);
    require(created_graph.edges.size() == 2,
            "Created transition was not represented in the derived graph");

    GraphEditCommand remove;
    remove.operation = GraphEditOperation::delete_transition;
    remove.edge_index = 0;
    const auto deleted = apply_graph_edit(source, "graph_edit_tests.sm", remove);
    require(deleted.success, "Deleting a transition failed");
    require(deleted.source.find("move Done") == std::string::npos &&
                parse_graph(deleted.source).edges.empty(),
            "Deleted transition remained in the source or graph");
}

void test_update_transition_target() {
    GraphEditCommand command;
    command.operation = GraphEditOperation::update_transition_target;
    command.edge_index = 0;
    command.target_state_index = 0;
    const auto result = apply_graph_edit(source, "graph_edit_tests.sm", command);
    require(result.success, "Editing a transition target failed");
    require(result.source.find("move Ready") != std::string::npos,
            "Transition target edit was not written into the source");
    const auto graph = parse_graph(result.source);
    require(graph.edges.size() == 1 && graph.edges.front().to == graph.nodes.front().id,
            "Edited transition target was not represented in the derived graph");
}

void test_delete_state_removes_incoming_edges() {
    GraphEditCommand command;
    command.operation = GraphEditOperation::delete_state;
    command.state_index = 1;
    const auto result = apply_graph_edit(source, "graph_edit_tests.sm", command);
    require(result.success, "Deleting a referenced state failed");
    require(result.source.find("Done") == std::string::npos &&
                result.source.find("move Done") == std::string::npos,
            "Deleting a state left an incoming transition reference");
    const auto graph = parse_graph(result.source);
    require(graph.nodes.size() == 2 && graph.edges.empty(),
            "Deleted state or its incoming edge remained in the graph");
}

void test_reject_invalid_delete_without_candidate() {
    GraphEditCommand command;
    command.operation = GraphEditOperation::delete_state;
    command.state_index = 0;
    const auto result = apply_graph_edit(source, "graph_edit_tests.sm", command);
    require(!result.success && result.source.empty() &&
                result.error.find("start state") != std::string::npos,
            "Deleting the start state did not fail safely");
}

void test_reject_invalid_cpp_state_name() {
    GraphEditCommand command;
    command.operation = GraphEditOperation::rename_state;
    command.state_index = 1;
    command.name = "class";
    const auto result = apply_graph_edit(source, "graph_edit_tests.sm", command);
    require(!result.success && result.source.empty() &&
                result.error.find("Invalid C++ identifier") != std::string::npos,
            "A C++ keyword was accepted as an SMC state name");
}

} // namespace

int main() {
    try {
        test_create_state_preserves_source();
        test_rename_state_updates_references();
        test_create_and_delete_transition();
        test_update_transition_target();
        test_delete_state_removes_incoming_edges();
        test_reject_invalid_delete_without_candidate();
        test_reject_invalid_cpp_state_name();
    } catch (const std::exception& error) {
        std::cerr << "graph_edit_tests: " << error.what() << '\n';
        return 1;
    }
}
