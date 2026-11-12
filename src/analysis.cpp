#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <analysis.hpp>
#include <smc/generator.hpp>
#include <smc/parser.hpp>
#include <smc/syntax_checker.hpp>

void print_diagnostics(const smc::Diagnostics& diagnostics, DiagnosticLog* log) {
    for (const auto& diagnostic : diagnostics) {
        const char* severity = diagnostic.severity == smc::DiagnosticSeverity::error
                                   ? "error"
                                   : "warning";
        write_log(log, diagnostic.severity == smc::DiagnosticSeverity::error
                           ? LogLevel::error : LogLevel::warning, "SMC",
            diagnostic.location.file + ':' +
            std::to_string(diagnostic.location.line) + ':' +
            std::to_string(diagnostic.location.column) + ": " + severity + ": " +
            diagnostic.message);
    }
}

std::optional<std::string> read_input_file(const std::string& input_file, DiagnosticLog* log) {
    std::ifstream input(input_file, std::ios::binary);
    if (!input) {
        write_log(log, LogLevel::error, "File", "smc_cpp: cannot open input file '" + input_file + "'");
        return std::nullopt;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    if (input.bad()) {
        write_log(log, LogLevel::error, "File", "smc_cpp: failed while reading input file '" + input_file + "'");
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

const char* graph_object_kind_name(GraphObjectKind kind) {
    switch (kind) {
    case GraphObjectKind::state_map: return "state_map";
    case GraphObjectKind::state: return "state";
    case GraphObjectKind::transition: return "transition";
    case GraphObjectKind::guard: return "guard";
    }
    return "unknown";
}

class DirectedGraphGenerator final : public smc::Generator {
public:
    struct Result {
        dot_layout::DirectedGraph graph;
        std::vector<SourceSegment> source_segments;
    };

    DirectedGraphGenerator(const std::string& contents, const std::string& source_name,
                           DiagnosticLog* log)
        : contents_(contents), source_name_(source_name), log_(log),
          line_offsets_(source_line_offsets(contents)) {}

    std::optional<Result> generate(const smc::Fsm& fsm) {
        fsm_ = &fsm;
        for (const auto& map : fsm.maps) {
            auto& states = state_lookup_[map.name];
            for (const auto& state : map.states) {
                states.emplace(state.name, "state_" + std::to_string(next_state_id_++));
            }
        }

        fsm.accept(*this);
        if (failed_)
            return std::nullopt;

        std::sort(located_foci_.begin(), located_foci_.end(),
            [](const LocatedFocus& left, const LocatedFocus& right) {
                if (left.offset != right.offset)
                    return left.offset < right.offset;
                return focus_priority(left.kind) > focus_priority(right.kind);
            });
        std::vector<SourceSegment> resolved_segments;
        for (std::size_t index = 0; index < located_foci_.size();) {
            const auto& focus = located_foci_[index];
            std::size_t next = index + 1;
            while (next < located_foci_.size() &&
                   located_foci_[next].offset == focus.offset) {
                ++next;
            }
            const std::size_t end = next < located_foci_.size()
                ? located_foci_[next].offset : contents_.size();
            if (focus.offset < end ||
                (focus.offset == contents_.size() && resolved_segments.empty())) {
                resolved_segments.push_back(
                    {focus.offset, end, focus.kind, focus.graph_indices});
            }
            index = next;
        }
        if (resolved_segments.empty()) {
            write_log(log_, LogLevel::debug, "Analysis",
                      source_name_ + ": source_segments is empty");
        }
        for (std::size_t index = 0; index < resolved_segments.size(); ++index) {
            const auto& segment = resolved_segments[index];
            std::ostringstream detail;
            detail << source_name_ << ": source_segments[" << index << "] begin="
                   << segment.begin << " end=" << segment.end
                   << " kind=" << graph_object_kind_name(segment.kind)
                   << " graph_indices=[";
            for (std::size_t graph_index = 0;
                 graph_index < segment.graph_indices.size(); ++graph_index) {
                if (graph_index > 0)
                    detail << ", ";
                detail << segment.graph_indices[graph_index];
            }
            detail << ']';
            write_log(log_, LogLevel::debug, "Analysis", detail.str());
        }
        return Result{std::move(graph_), std::move(resolved_segments)};
    }

    void visit(const smc::Fsm& fsm) override {
        fsm_ = &fsm;
        for (const auto& map : fsm.maps)
            map.accept(*this);
    }

    void visit(const smc::StateMap& map) override {
        if (failed_)
            return;

        current_map_ = &map;
        const auto cluster_index = graph_.clusters.size();
        graph_.clusters.push_back({map.name, map.name, {}, std::nullopt});
        add_focus(map.location, GraphObjectKind::state_map, {cluster_index});
        for (const auto& state : map.states)
            state.accept(*this);
        if (map.states.empty()) {
            write_log(log_, LogLevel::error, "SMC", source_name_ + ": state map '" + map.name +
                "' has no states; cannot lay out an empty cluster");
            failed_ = true;
        }
    }

    void visit(const smc::State& state) override {
        if (failed_)
            return;

        const std::string id = "state_" + std::to_string(graph_.nodes.size());
        const auto node_index = graph_.nodes.size();
        graph_.nodes.push_back({id, state.name,
            current_map_->name + "::" + state.name == fsm_->start_state
                ? dot_layout::NodeShape::Ellipse : dot_layout::NodeShape::Box});
        graph_.clusters.back().nodes.push_back(id);
        if (state.location.line != current_map_->location.line ||
            state.location.column != current_map_->location.column) {
            add_focus(state.location, GraphObjectKind::state, {node_index});
        }

        current_state_id_ = id;
        for (const auto& transition : state.transitions)
            transition.accept(*this);
    }

    void visit(const smc::Transition& transition) override {
        if (failed_)
            return;

        const auto transition_edge_start = graph_.edges.size();
        current_transition_ = &transition;
        for (const auto& guard : transition.guards)
            guard.accept(*this);
        std::vector<std::size_t> transition_edges;
        for (std::size_t edge_index = transition_edge_start;
             edge_index < graph_.edges.size(); ++edge_index) {
            transition_edges.push_back(edge_index);
        }
        if (!transition_edges.empty())
            add_focus(transition.location, GraphObjectKind::transition,
                      std::move(transition_edges));
    }

    void visit(const smc::Guard& guard) override {
        if (failed_)
            return;

        const auto edge_index = graph_.edges.size();
        std::string target = current_state_id_;
        if (guard.transition_type != "pop") {
            const auto& destination = guard.transition_type == "push"
                ? guard.push_state : guard.end_state;
            if (destination != "nil") {
                const auto resolved = resolve_state(destination, current_map_->name);
                if (!resolved) {
                    write_log(log_, LogLevel::error, "SMC", source_name_ +
                        ": cannot resolve destination '" + destination +
                        "' in map '" + current_map_->name + "'");
                    failed_ = true;
                    return;
                }
                target = *resolved;
            }
        }

        std::string label = current_transition_->name;
        label += "(" + current_transition_->parameters + ")";
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
        graph_.edges.push_back({current_state_id_, target, std::move(label)});
        add_focus(guard.location, GraphObjectKind::guard, {edge_index});
    }

private:
    struct LocatedFocus {
        std::size_t offset;
        GraphObjectKind kind;
        std::vector<std::size_t> graph_indices;
    };

    void add_focus(const smc::SourceLocation& location, GraphObjectKind kind,
                   std::vector<std::size_t> indices) {
        located_foci_.push_back({
            source_offset_at_location(contents_, line_offsets_, location),
            kind, std::move(indices)});
    }

    std::optional<std::string> resolve_state(
        const std::string& name, const std::string& current_map) const {
        const auto separator = name.find("::");
        const std::string map_name = separator == std::string::npos
            ? current_map : name.substr(0, separator);
        const std::string state_name = separator == std::string::npos
            ? name : name.substr(separator + 2);
        const auto map = state_lookup_.find(map_name);
        if (map == state_lookup_.end())
            return std::nullopt;
        const auto state = map->second.find(state_name);
        return state == map->second.end() ? std::nullopt
                                           : std::optional(state->second);
    }

    const std::string& contents_;
    const std::string& source_name_;
    DiagnosticLog* log_;
    std::vector<std::size_t> line_offsets_;
    const smc::Fsm* fsm_ = nullptr;
    const smc::StateMap* current_map_ = nullptr;
    const smc::Transition* current_transition_ = nullptr;
    std::string current_state_id_;
    dot_layout::DirectedGraph graph_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
        state_lookup_;
    std::vector<LocatedFocus> located_foci_;
    std::size_t next_state_id_ = 0;
    bool failed_ = false;
};

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

    DirectedGraphGenerator generator(contents, source_name, log);
    auto result = generator.generate(*fsm);
    if (!result)
        return std::nullopt;
    if (source_segments)
        *source_segments = std::move(result->source_segments);
    return std::move(result->graph);
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
        // Stage every diagnostic until the revision is accepted or discarded.
        DiagnosticLog diagnostics(std::numeric_limits<std::size_t>::max(), false);
        try {
            result.graph = job->source
                ? make_graph_from_contents(*job->source, job->source_name,
                                           &diagnostics, &result.source_segments)
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
            diagnostics.write(LogLevel::error, "Analysis",
                              std::string("SMC analysis or layout failed: ") + error.what());
        }
        result.diagnostics = std::move(diagnostics.snapshot().records);
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
    for (auto& record : result.diagnostics)
        log.publish(std::move(record));
    if (result.graph && result.layout) {
        canvas.graph = std::move(*result.graph);
        canvas.layout = std::move(*result.layout);
        canvas.recently_replaced = result.source_job;
        if (result.source_job) {
            source_segments_ = std::move(result.source_segments);
            source_is_indexed_ = true;
            source_needs_analysis_ = false;
            log.write(LogLevel::info, "Analysis", "SMC analysis succeeded; graph and DOT layout updated.");
        }
    } else {
        if (result.source_job) {
            source_needs_analysis_ = false;
            log.write(LogLevel::error, "Analysis", "SMC analysis failed; retaining the last valid graph.");
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
