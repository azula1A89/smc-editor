#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <TextEditor.h>
#include <smc/diagnostic.hpp>

#include <app_context.hpp>

struct AnalysisJob {
    std::uint64_t revision;
    std::optional<std::string> source;
    std::string source_name;
    dot_layout::DirectedGraph graph;
    dot_layout::LayoutOptions options;
    GraphPreferences graph_preferences;
};

struct AstNodeView {
    std::string label;
    SourceSegment source;
    std::vector<std::size_t> children;
};

struct AstTreeView {
    std::vector<AstNodeView> nodes;
    std::vector<std::size_t> roots;
};

struct AnalysisResult {
    std::uint64_t revision;
    bool source_job;
    std::optional<dot_layout::DirectedGraph> graph;
    std::optional<dot_layout::LayoutResult> layout;
    std::vector<SourceSegment> source_segments;
    std::vector<SourceSegment> source_ranges;
    AstTreeView ast;
    std::vector<LogRecord> diagnostics;
    std::string source_name;
    smc::Diagnostics source_diagnostics;
};

struct SourceErrorMarker {
    std::size_t line;
    std::string tooltip;
};

std::vector<SourceErrorMarker> source_error_markers(
    const smc::Diagnostics& diagnostics, std::string_view source_name,
    std::size_t line_count);
void set_source_error_markers(
    TextEditor& editor, const smc::Diagnostics& diagnostics, std::string_view source_name);

class AnalysisWorker {
public:
    AnalysisWorker();
    ~AnalysisWorker();

    AnalysisWorker(const AnalysisWorker&) = delete;
    AnalysisWorker& operator=(const AnalysisWorker&) = delete;

    void submit(AnalysisJob job);
    std::optional<AnalysisResult> take_result();

private:
    void run();

    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<AnalysisJob> pending_;
    std::optional<AnalysisResult> result_;
    bool stopping_ = false;
    std::thread thread_;
};

class AnalysisCoordinator {
public:
    using Clock = std::chrono::steady_clock;

    explicit AnalysisCoordinator(bool has_source, Clock::time_point now);
    void text_changed(Clock::time_point now, TextEditor* editor = nullptr);
    void settings_changed();
    bool source_is_indexed() const;
    const std::vector<SourceSegment>& source_segments() const;
    const std::vector<SourceSegment>& source_ranges() const;
    const AstTreeView& ast() const;
    void accept(
        AnalysisResult result, CanvasState& canvas, DiagnosticLog& log,
        TextEditor* editor = nullptr);
    std::optional<AnalysisJob> next_job(
        Clock::time_point now, const std::string& source, const std::string& source_name,
        CanvasState& canvas, const UserSettings& settings = UserSettings{});

private:
    void invalidate();

    std::uint64_t revision_ = 0;
    std::optional<std::uint64_t> active_revision_;
    bool source_needs_analysis_;
    bool source_is_indexed_ = false;
    Clock::time_point deadline_;
    std::vector<SourceSegment> source_segments_;
    std::vector<SourceSegment> source_ranges_;
    AstTreeView ast_;
};

std::optional<dot_layout::DirectedGraph> make_graph(
    const std::string& input_file = "mapmanager.sm", DiagnosticLog* log = nullptr);
std::optional<dot_layout::DirectedGraph> make_graph_from_contents(
    const std::string& contents, const std::string& source_name, DiagnosticLog* log = nullptr,
    std::vector<SourceSegment>* source_segments = nullptr,
    std::vector<SourceSegment>* source_ranges = nullptr,
    AstTreeView* ast = nullptr,
    smc::Diagnostics* source_diagnostics = nullptr);
std::optional<std::string> read_input_file(
    const std::string& input_file, DiagnosticLog* log = nullptr);
std::optional<GraphFocus> focus_at_offset(
    const std::vector<SourceSegment>& source_segments, std::size_t offset);
std::size_t editor_offset_at_position(
    std::string_view contents, TextEditor::DocPos position);
std::size_t editor_offset_at_position(
    std::string_view contents, const std::vector<std::size_t>& line_offsets,
    TextEditor::DocPos position);
std::vector<std::size_t> source_line_offsets(std::string_view contents);
TextEditor::DocPos editor_position_at_offset(
    std::string_view contents, const std::vector<std::size_t>& line_offsets,
    std::size_t offset);
std::optional<SourceSegment> source_range_for_focus(
    const std::vector<SourceSegment>& ranges, const GraphFocus& focus);
