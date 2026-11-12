#include <analysis.hpp>
#include <ui_windows.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

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
    std::vector<SourceSegment> source_ranges;
    DiagnosticLog debug_log(DiagnosticLog::default_capacity, false);
    const auto graph_from_editor = make_graph_from_contents(
        contents, input_file, &debug_log, &source_segments, &source_ranges);
    require(graph_from_editor.has_value() &&
                graph_from_editor->nodes.size() == graph->nodes.size() &&
                graph_from_editor->edges.size() == graph->edges.size(),
            "Editor contents did not produce the same graph as file input");
    const auto debug_records = debug_log.snapshot().records;
    require(debug_records.size() == source_segments.size(),
            "Source segment debug log count did not match generated segments");
    for (std::size_t index = 0; index < debug_records.size(); ++index) {
        const auto& record = debug_records[index];
        require(record.level == LogLevel::debug && record.source == "Analysis" &&
                    record.detail.find("source_segments[" + std::to_string(index) + "]") !=
                        std::string::npos &&
                    record.detail.find("begin=") != std::string::npos &&
                    record.detail.find("end=") != std::string::npos &&
                    record.detail.find("kind=") != std::string::npos &&
                    record.detail.find("graph_indices=[") != std::string::npos,
                "Source segment debug log omitted expected metadata");
    }
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
    const auto transition_range = source_range_for_focus(
        source_ranges, GraphFocus{GraphObjectKind::guard, {1}});
    require(transition_range && contents.substr(transition_range->begin,
                transition_range->end - transition_range->begin) ==
                "go(value: int) [value > 0] Other::Ready { record(value); }\n"
                "                   [value <= 0] Ready { reject(); }",
            "Edge did not map to the complete owning transition");
    const auto state_range = source_range_for_focus(
        source_ranges, GraphFocus{GraphObjectKind::state, {0}});
    require(state_range && contents.substr(state_range->begin,
                state_range->end - state_range->begin).starts_with("Ready\n{\n    go") &&
                contents[state_range->end - 1] == '}',
            "State range did not include nested transitions");
    const auto map_range = source_range_for_focus(
        source_ranges, GraphFocus{GraphObjectKind::state_map, {0}});
    require(map_range && contents.substr(map_range->begin, 9) == "%map Main" &&
                contents.substr(map_range->end - 2, 2) == "%%",
            "Map range did not include its states");
    require(!source_range_for_focus(source_ranges, GraphFocus{GraphObjectKind::state, {2}}),
            "Synthetic default state unexpectedly had a source range");
    const std::string utf8 = "a\r\n猫z\n";
    const auto lines = source_line_offsets(utf8);
    require(editor_position_at_offset(utf8, lines, 6) == TextEditor::DocPos(1, 1) &&
                editor_position_at_offset(utf8, lines, utf8.size()) == TextEditor::DocPos(2, 0) &&
                editor_position_at_offset(utf8, lines, 2) == TextEditor::DocPos(0, 1),
            "Source offset did not convert through UTF-8 and CRLF");

    DiagnosticLog diagnostics;
    require(!make_graph_from_contents("%unknown\n", "editor.sm", &diagnostics) &&
                !diagnostics.snapshot().records.empty(),
            "Editor parse diagnostics were not reported");
    const auto records = diagnostics.snapshot().records;
    require(records[0].level == LogLevel::error && records[0].source == "SMC" &&
                records[0].detail.find("editor.sm:") != std::string::npos,
            "Parser diagnostic metadata or location was lost");
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

void test_source_segment_parent_fallback() {
    const std::string contents =
        "%start Main::state_x\n"
        "%class Context\n"
        "%header context.h\n"
        "%map Main\n"
        "%%\n"
        "state_x\n"
        "{\n"
        "    transition_xy state_y{}\n"
        "}\n"
        "\n"
        "state_y\n"
        "{\n"
        "}\n"
        "%%\n";
    std::vector<SourceSegment> source_segments;
    require(make_graph_from_contents(contents, "segments.sm", nullptr, &source_segments)
                .has_value(),
            "Nested source segment fixture did not produce a graph");

    const auto transition_offset = contents.find("transition_xy");
    const auto transition_focus = focus_at_offset(source_segments, transition_offset);
    require(transition_focus &&
                transition_focus->kind == GraphObjectKind::transition,
            "Transition token did not map to the transition");

    const auto state_close = contents.find("}\n\nstate_y");
    require(state_close != std::string::npos, "State closing brace not found");
    const auto closing_brace_focus = focus_at_offset(source_segments, state_close);
    require(closing_brace_focus &&
                closing_brace_focus->kind == GraphObjectKind::state &&
                closing_brace_focus->graph_indices == std::vector<std::size_t>{0},
            "State closing brace did not map to its state");

    const auto blank_line_offset = state_close + 2;
    const auto blank_line_focus = focus_at_offset(source_segments, blank_line_offset);
    require(blank_line_focus &&
                blank_line_focus->kind == GraphObjectKind::state_map &&
                blank_line_focus->graph_indices == std::vector<std::size_t>{0},
            "Blank line between states did not fall back to the state map");
}

} // namespace

int main() {
    try {
        testConversion();
        test_source_segment_parent_fallback();
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
