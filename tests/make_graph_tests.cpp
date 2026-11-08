#define main dot_layout_demo_main
#include "../src/main.cpp"
#undef main

namespace {

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void testConversion() {
    const auto input_file = std::string(TEST_SOURCE_DIR) + "/tests/fsm_graph.sm";
    const auto graph = make_graph(input_file);
    require(graph.has_value(), "Valid FSM failed to convert");
    std::ifstream input(input_file, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    std::vector<SourceSegment> source_segments;
    const auto graph_from_editor = make_graph_from_contents(
        contents, input_file, nullptr, &source_segments);
    require(graph_from_editor.has_value() &&
                graph_from_editor->nodes.size() == graph->nodes.size() &&
                graph_from_editor->edges.size() == graph->edges.size(),
            "Editor contents did not produce the same graph as file input");
    const auto other_map_focus = focus_at_offset(
        source_segments, contents.find("%map Other"));
    require(other_map_focus && other_map_focus->kind == GraphObjectKind::state_map &&
                other_map_focus->graph_indices == std::vector<std::size_t>{1},
            "Map declaration did not map to its cluster");
    const auto state_focus = focus_at_offset(
        source_segments, contents.find("OtherState"));
    require(state_focus && state_focus->kind == GraphObjectKind::state &&
                state_focus->graph_indices == std::vector<std::size_t>{1},
            "State declaration did not map to its node");
    const auto transition_offset = contents.find("go(value");
    const auto transition_focus = focus_at_offset(source_segments, transition_offset);
    require(transition_focus &&
                transition_focus->kind == GraphObjectKind::transition &&
                transition_focus->graph_indices == std::vector<std::size_t>({0, 1}),
            "Transition declaration did not map to its guard edges");
    const auto guard_focus = focus_at_offset(
        source_segments, contents.find("[value > 0]"));
    require(guard_focus && guard_focus->kind == GraphObjectKind::guard &&
                guard_focus->graph_indices == std::vector<std::size_t>{0},
            "Guard declaration did not map to its edge");
    require(editor_offset_at_position("a\n猫z", TextEditor::DocPos(1, 1)) == 5,
            "Editor cursor position did not convert through UTF-8 text");

    DiagnosticLog diagnostics;
    require(!make_graph_from_contents("%unknown\n", "editor.sm", &diagnostics) &&
                !diagnostics.empty(),
            "Editor parse diagnostics were not reported");
    require(graph->clusters.size() == 2 && graph->nodes.size() == 6 &&
                graph->edges.size() == 9, "Unexpected graph size");
    require(graph->clusters[0].label == "Main" && graph->clusters[1].label == "Other",
            "Map labels not preserved");
    require(graph->clusters[0].nodes.size() == 3 &&
                graph->clusters[1].nodes.size() == 3, "Incorrect map membership");
    require(graph->nodes[0].label == "Ready" && graph->nodes[3].label == "Ready" &&
                graph->nodes[0].id != graph->nodes[3].id, "State identity not unique");
    require(graph->nodes[0].shape == dot_layout::NodeShape::Ellipse &&
                graph->nodes[1].shape == dot_layout::NodeShape::Box &&
                graph->nodes[2].shape == dot_layout::NodeShape::Box &&
                graph->nodes[3].shape == dot_layout::NodeShape::Box &&
                graph->nodes[4].shape == dot_layout::NodeShape::Box &&
                graph->nodes[5].shape == dot_layout::NodeShape::Box,
            "Incorrect start state shape");
    const auto& edges = graph->edges;
    require(edges[0].from == graph->nodes[0].id && edges[0].to == graph->nodes[3].id,
            "Qualified destination not resolved");
    require(edges[0].label.find("go(value: int) [value > 0]") != std::string::npos &&
                edges[0].label.find("record(value);") != std::string::npos,
            "Transition label omits parameters, guard or actions");
    require(edges[1].to == graph->nodes[0].id &&
                edges[1].label.find("[value <= 0]") != std::string::npos,
            "Guard alternatives not preserved");
    require(edges[2].to == graph->nodes[0].id, "nil should be a self-loop");
    require(edges[3].to == graph->nodes[3].id &&
                edges[3].label.find("Ready/push(Other::Ready)") != std::string::npos,
            "Push destination or return state lost");
    require(edges[4].to == graph->nodes[0].id &&
                edges[4].label.find("nil/push(Ready)") != std::string::npos,
            "Direct push not represented");
    require(edges[5].to == graph->nodes[0].id &&
                edges[5].label.find("pop(done(1)) [dynamic destination]") != std::string::npos,
            "Pop semantics not represented");
    require(edges[6].to == graph->nodes[4].id &&
                edges[6].label.find("jump(Other::Done)") != std::string::npos,
            "Jump destination not represented");
    require(edges[7].from == graph->nodes[3].id && edges[7].to == graph->nodes[4].id &&
                edges[8].to == graph->nodes[0].id, "Other map targets not resolved");
    const auto layout = dot_layout::DotLayout{}.layout(*graph);
    require(layout.nodes.size() == graph->nodes.size() &&
                layout.edges.size() == graph->edges.size() &&
                layout.clusters.size() == graph->clusters.size(),
            "Converted graph cannot be laid out");
    const auto focused_state_point =
        focused_graph_point(*state_focus, *graph, layout);
    require(focused_state_point &&
                focused_state_point->x == layout.nodes[1].center.x &&
                focused_state_point->y == layout.nodes[1].center.y,
            "Focused state did not resolve to its graph node position");
    const auto focused_map_point =
        focused_graph_point(*other_map_focus, *graph, layout);
    const auto& other_map_bounds = layout.clusters[1].bounds;
    require(focused_map_point &&
                focused_map_point->x == (other_map_bounds.min_x + other_map_bounds.max_x) * 0.5 &&
                focused_map_point->y == (other_map_bounds.min_y + other_map_bounds.max_y) * 0.5,
            "Focused map did not resolve to its cluster center");
    const auto focused_guard_point =
        focused_graph_point(*guard_focus, *graph, layout);
    require(focused_guard_point && layout.edges[0].label &&
                focused_guard_point->x == layout.edges[0].label->center.x &&
                focused_guard_point->y == layout.edges[0].label->center.y,
            "Focused guard did not resolve to its graph edge label");
}

} // namespace

int main() {
    try {
        testConversion();
        require(!make_graph(std::string(TEST_SOURCE_DIR) + "/tests/no-such-file.sm"),
                "Missing file accepted");
        require(!make_graph(std::string(TEST_SOURCE_DIR) + "/deps/smc.cpp/tests/invalid.sm"),
                "Invalid FSM accepted");
    } catch (const std::exception& error) {
        std::cerr << "make_graph_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
