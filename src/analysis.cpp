#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <analysis.hpp>
#include <smc/parser.hpp>
#include <smc/syntax_checker.hpp>

namespace {

thread_local bool defer_log_output = false;

} // namespace

void append_log(DiagnosticLog* log, std::string message) {
    if (!defer_log_output)
        std::cerr << message << '\n';
    if (log)
        log->push_back(std::move(message));
}

void print_diagnostics(const smc::Diagnostics& diagnostics, DiagnosticLog* log) {
    for (const auto& diagnostic : diagnostics) {
        const char* severity = diagnostic.severity == smc::DiagnosticSeverity::error
                                   ? "error"
                                   : "warning";
        append_log(log, diagnostic.location.file + ':' +
            std::to_string(diagnostic.location.line) + ':' +
            std::to_string(diagnostic.location.column) + ": " + severity + ": " +
            diagnostic.message);
    }
}

std::optional<std::string> read_input_file(const std::string& input_file, DiagnosticLog* log) {
    std::ifstream input(input_file, std::ios::binary);
    if (!input) {
        append_log(log, "smc_cpp: cannot open input file '" + input_file + "'");
        return std::nullopt;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    if (input.bad()) {
        append_log(log, "smc_cpp: failed while reading input file '" + input_file + "'");
        return std::nullopt;
    }
    return contents;
}

std::vector<std::size_t> source_line_offsets(std::string_view contents) {
    std::vector<std::size_t> offsets{0};
    for (std::size_t index = 0; index < contents.size(); ++index) {
        if (contents[index] == '\r') {
            if (index + 1 < contents.size() && contents[index + 1] == '\n')
                ++index;
            offsets.push_back(index + 1);
        } else if (contents[index] == '\n') {
            offsets.push_back(index + 1);
        }
    }
    return offsets;
}

namespace {

std::size_t source_offset_at_location(
    std::string_view contents, const std::vector<std::size_t>& line_offsets,
    const smc::SourceLocation& location) {
    if (location.line == 0 || location.line > line_offsets.size())
        return contents.size();
    const std::size_t line_start = line_offsets[location.line - 1];
    const std::size_t line_end = location.line < line_offsets.size()
        ? line_offsets[location.line] : contents.size();
    return std::min(line_start + (location.column > 0 ? location.column - 1 : 0), line_end);
}

int focus_priority(GraphObjectKind kind) {
    switch (kind) {
    case GraphObjectKind::state_map: return 0;
    case GraphObjectKind::state: return 1;
    case GraphObjectKind::transition: return 2;
    case GraphObjectKind::guard: return 3;
    }
    return 0;
}

} // namespace

std::size_t editor_offset_at_position(
    std::string_view contents, TextEditor::DocPos position) {
    return editor_offset_at_position(contents, source_line_offsets(contents), position);
}

std::size_t editor_offset_at_position(
    std::string_view contents, const std::vector<std::size_t>& line_offsets,
    TextEditor::DocPos position) {
    if (position.line >= line_offsets.size())
        return contents.size();

    const std::size_t line_start = line_offsets[position.line];
    const std::size_t line_end = position.line + 1 < line_offsets.size()
        ? line_offsets[position.line + 1] : contents.size();
    std::size_t index = line_start;
    std::size_t glyph_index = 0;
    while (index < line_end && glyph_index < position.index) {
        const auto byte = static_cast<unsigned char>(contents[index]);
        if (byte == '\r' || byte == '\n')
            break;
        std::size_t width = 1;
        if ((byte & 0xE0) == 0xC0)
            width = 2;
        else if ((byte & 0xF0) == 0xE0)
            width = 3;
        else if ((byte & 0xF8) == 0xF0)
            width = 4;
        index = std::min(index + width, line_end);
        ++glyph_index;
    }
    return index;
}

std::optional<GraphFocus> focus_at_offset(
    const std::vector<SourceSegment>& source_segments, std::size_t offset) {
    const auto segment = std::upper_bound(
        source_segments.begin(), source_segments.end(), offset,
        [](std::size_t position, const SourceSegment& candidate) {
            return position < candidate.begin;
        });
    if (segment == source_segments.begin())
        return std::nullopt;

    const auto& candidate = *std::prev(segment);
    if (offset >= candidate.end &&
        !(segment == source_segments.end() && offset == candidate.end))
        return std::nullopt;
    return GraphFocus{candidate.kind, candidate.graph_indices};
}

std::optional<dot_layout::DirectedGraph> make_graph_from_contents(
    const std::string& contents, const std::string& source_name, DiagnosticLog* log,
    std::vector<SourceSegment>* source_segments) {
    smc::Diagnostics diagnostics;
    smc::Parser parser(contents, source_name, diagnostics);
    const auto fsm = parser.parse();
    smc::SyntaxChecker checker(diagnostics);
    const bool syntax_valid = fsm && checker.check(*fsm, "c++");
    print_diagnostics(diagnostics, log);
    if (!syntax_valid)
        return std::nullopt;

    dot_layout::DirectedGraph graph;
    struct LocatedFocus {
        std::size_t offset;
        GraphObjectKind kind;
        std::vector<std::size_t> graph_indices;
    };
    std::vector<LocatedFocus> located_foci;
    const auto line_offsets = source_line_offsets(contents);
    const auto add_focus = [&](const smc::SourceLocation& location, GraphObjectKind kind,
                               std::vector<std::size_t> indices) {
        located_foci.push_back({source_offset_at_location(contents, line_offsets, location),
                                kind, std::move(indices)});
    };
    std::vector<std::vector<std::string>> state_ids;
    for (const auto& map : fsm->maps) {
        const auto cluster_index = graph.clusters.size();
        dot_layout::Cluster cluster{map.name, map.name, {}, std::nullopt};
        add_focus(map.location, GraphObjectKind::state_map, {cluster_index});
        std::vector<std::string> ids;
        for (const auto& state : map.states) {
            const std::string id = "state_" + std::to_string(graph.nodes.size());
            const auto node_index = graph.nodes.size();
            graph.nodes.push_back({id, state.name,
                map.name + "::" + state.name == fsm->start_state
                    ? dot_layout::NodeShape::Ellipse : dot_layout::NodeShape::Box});
            if (state.location.line != map.location.line ||
                state.location.column != map.location.column) {
                add_focus(state.location, GraphObjectKind::state, {node_index});
            }
            cluster.nodes.push_back(id);
            ids.push_back(id);
        }
        if (cluster.nodes.empty()) {
            append_log(log, source_name + ": state map '" + map.name +
                "' has no states; cannot lay out an empty cluster");
            return std::nullopt;
        }
        graph.clusters.push_back(std::move(cluster));
        state_ids.push_back(std::move(ids));
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> state_lookup;
    for (std::size_t i = 0; i < fsm->maps.size(); ++i) {
        auto& states = state_lookup[fsm->maps[i].name];
        for (std::size_t j = 0; j < fsm->maps[i].states.size(); ++j)
            states.emplace(fsm->maps[i].states[j].name, state_ids[i][j]);
    }
    const auto resolve_state = [&](const std::string& name, const std::string& current_map)
        -> std::optional<std::string> {
        const auto separator = name.find("::");
        const std::string map_name = separator == std::string::npos
            ? current_map : name.substr(0, separator);
        const std::string state_name = separator == std::string::npos
            ? name : name.substr(separator + 2);
        const auto map = state_lookup.find(map_name);
        if (map == state_lookup.end())
            return std::nullopt;
        const auto state = map->second.find(state_name);
        return state == map->second.end() ? std::nullopt : std::optional(state->second);
    };
    for (std::size_t map_index = 0; map_index < fsm->maps.size(); ++map_index) {
        const auto& map = fsm->maps[map_index];
        for (std::size_t state_index = 0; state_index < map.states.size(); ++state_index) {
            const auto& state = map.states[state_index];
            const auto& source = state_ids[map_index][state_index];
            for (const auto& transition : state.transitions) {
                const auto transition_edge_start = graph.edges.size();
                for (const auto& guard : transition.guards) {
                    const auto edge_index = graph.edges.size();
                    std::string target = source;
                    if (guard.transition_type != "pop") {
                        const auto& destination = guard.transition_type == "push"
                            ? guard.push_state : guard.end_state;
                        if (destination != "nil") {
                            const auto resolved = resolve_state(destination, map.name);
                            if (!resolved) {
                                append_log(log, source_name +
                                    ": cannot resolve destination '" + destination +
                                    "' in map '" + map.name + "'");
                                return std::nullopt;
                            }
                            target = *resolved;
                        }
                    }

                    std::string label = transition.name + "(" + transition.parameters + ")";
                    if (!guard.condition.empty())
                        label += " [" + guard.condition + "]";
                    if (guard.transition_type == "push")
                        label += " / " + guard.end_state + "/push(" + guard.push_state + ")";
                    else if (guard.transition_type == "pop")
                        label += " / pop(" + guard.pop_arguments + ") [dynamic destination]";
                    else if (guard.transition_type == "jump")
                        label += " / jump(" + guard.end_state + ")";
                    if (!guard.actions.empty())
                        label += " {" + guard.actions + "}";
                    graph.edges.push_back({source, target, std::move(label)});
                    add_focus(guard.location, GraphObjectKind::guard, {edge_index});
                }
                std::vector<std::size_t> transition_edges;
                for (std::size_t edge_index = transition_edge_start;
                     edge_index < graph.edges.size(); ++edge_index) {
                    transition_edges.push_back(edge_index);
                }
                if (!transition_edges.empty())
                    add_focus(transition.location, GraphObjectKind::transition,
                              std::move(transition_edges));
            }
        }
    }
    std::sort(located_foci.begin(), located_foci.end(),
        [](const LocatedFocus& left, const LocatedFocus& right) {
            if (left.offset != right.offset)
                return left.offset < right.offset;
            return focus_priority(left.kind) > focus_priority(right.kind);
        });
    std::vector<SourceSegment> resolved_segments;
    for (std::size_t index = 0; index < located_foci.size();) {
        const auto& focus = located_foci[index];
        std::size_t next = index + 1;
        while (next < located_foci.size() &&
               located_foci[next].offset == focus.offset) {
            ++next;
        }
        const std::size_t end = next < located_foci.size()
            ? located_foci[next].offset : contents.size();
        if (focus.offset < end || (focus.offset == contents.size() && resolved_segments.empty())) {
            resolved_segments.push_back(
                {focus.offset, end, focus.kind, focus.graph_indices});
        }
        index = next;
    }
    if (source_segments)
        *source_segments = std::move(resolved_segments);
    return graph;
}

std::optional<dot_layout::DirectedGraph> make_graph(
    const std::string& input_file, DiagnosticLog* log) {
    const auto contents = read_input_file(input_file, log);
    if (!contents)
        return std::nullopt;
    return make_graph_from_contents(*contents, input_file, log);
}

AnalysisWorker::AnalysisWorker() : thread_([this] { run(); }) {}

AnalysisWorker::~AnalysisWorker() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        pending_.reset();
    }
    condition_.notify_one();
    thread_.join();
}

void AnalysisWorker::submit(AnalysisJob job) {
    {
        std::lock_guard lock(mutex_);
        pending_ = std::move(job);
    }
    condition_.notify_one();
}

std::optional<AnalysisResult> AnalysisWorker::take_result() {
    std::lock_guard lock(mutex_);
    auto result = std::move(result_);
    result_.reset();
    return result;
}

void AnalysisWorker::run() {
    defer_log_output = true;
    for (;;) {
        std::optional<AnalysisJob> job;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
            if (stopping_)
                return;
            job = std::move(pending_);
            pending_.reset();
        }

        AnalysisResult result{job->revision, job->source.has_value()};
        try {
            result.graph = job->source
                ? make_graph_from_contents(*job->source, job->source_name,
                                           &result.diagnostics, &result.source_segments)
                : std::optional<dot_layout::DirectedGraph>(std::move(job->graph));
            if (result.graph) {
                apply_graph_preferences(*result.graph, job->graph_preferences);
                auto layout_graph = *result.graph;
                if (!job->options.compound) {
                    for (auto& edge : layout_graph.edges) {
                        edge.tail_cluster.reset();
                        edge.head_cluster.reset();
                    }
                }
                result.layout = dot_layout::DotLayout{}.layout(layout_graph, job->options);
            }
        } catch (const std::exception& error) {
            result.graph.reset();
            result.layout.reset();
            result.diagnostics.push_back(
                std::string("SMC analysis or layout failed: ") + error.what());
        }
        {
            std::lock_guard lock(mutex_);
            result_ = std::move(result);
        }
    }
}

AnalysisCoordinator::AnalysisCoordinator(bool has_source, Clock::time_point now)
    : source_needs_analysis_(has_source), deadline_(now) {}

void AnalysisCoordinator::text_changed(Clock::time_point now) {
    source_is_indexed_ = false;
    source_needs_analysis_ = true;
    deadline_ = now + std::chrono::milliseconds(500);
    invalidate();
}

void AnalysisCoordinator::settings_changed() {
    invalidate();
}

bool AnalysisCoordinator::source_is_indexed() const {
    return source_is_indexed_;
}

const std::vector<SourceSegment>& AnalysisCoordinator::source_segments() const {
    return source_segments_;
}

void AnalysisCoordinator::accept(
    AnalysisResult result, CanvasState& canvas, DiagnosticLog& log) {
    if (active_revision_ && *active_revision_ == result.revision)
        active_revision_.reset();
    if (result.revision != revision_)
        return;
    for (auto& message : result.diagnostics)
        append_log(&log, std::move(message));
    if (result.graph && result.layout) {
        canvas.graph = std::move(*result.graph);
        canvas.layout = std::move(*result.layout);
        canvas.recently_replaced = result.source_job;
        if (result.source_job) {
            source_segments_ = std::move(result.source_segments);
            source_is_indexed_ = true;
            source_needs_analysis_ = false;
            append_log(&log, "SMC analysis succeeded; graph and DOT layout updated.");
        }
    } else {
        if (result.source_job) {
            source_needs_analysis_ = false;
            append_log(&log, "SMC analysis failed; retaining the last valid graph.");
        }
        canvas.dirty = result.source_job;
    }
}

std::optional<AnalysisJob> AnalysisCoordinator::next_job(
    Clock::time_point now, const std::string& source, const std::string& source_name,
    CanvasState& canvas, const UserSettings& settings) {
    if (active_revision_)
        return std::nullopt;

    std::optional<AnalysisJob> job;
    if (source_needs_analysis_ && now >= deadline_) {
        job = AnalysisJob{revision_, source, source_name, {}, canvas.options,
                          settings.graph_preferences};
    } else if (canvas.dirty && !source_needs_analysis_) {
        job = AnalysisJob{revision_, std::nullopt, source_name, canvas.graph,
                          canvas.options, settings.graph_preferences};
    }
    if (job) {
        active_revision_ = revision_;
        canvas.dirty = false;
    }
    return job;
}

void AnalysisCoordinator::invalidate() {
    ++revision_;
    active_revision_.reset();
}
