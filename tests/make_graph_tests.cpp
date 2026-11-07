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
    const auto graph_from_editor = make_graph_from_contents(contents, input_file);
    require(graph_from_editor.has_value() &&
                graph_from_editor->nodes.size() == graph->nodes.size() &&
                graph_from_editor->edges.size() == graph->edges.size(),
            "Editor contents did not produce the same graph as file input");

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
