#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>
#include <TextEditor.h>

#include <cpp_generation.hpp>
#include <dot_layout.hpp>
#include <log.hpp>
#include <user_settings.hpp>

class TrieAutoComplete;
class AnalysisWorker;
class AnalysisCoordinator;

enum class GraphObjectKind { state_map, state, transition, guard };

struct SourceSegment {
    std::size_t begin{0};
    std::size_t end{0};
    GraphObjectKind kind{GraphObjectKind::state_map};
    std::vector<std::size_t> graph_indices;
};

struct GraphFocus {
    GraphObjectKind kind{GraphObjectKind::state_map};
    std::vector<std::size_t> graph_indices;
};

struct CanvasFocus {
    GraphFocus focus;
    TextEditor::DocPos cursor;
    TextEditor::DocSelection selection;
    std::size_t cursor_count;
};

struct DiagnosticViewState {
    struct Line {
        std::string text;
        LogLevel level;
    };

    std::uint64_t version = 0;
    bool initialized = false;
    std::vector<LogRecord> records;
    std::vector<Line> lines;
    bool levels[4] = {true, true, true, true};
    char source_filter[256] = {};
    bool measured_levels[4] = {true, true, true, true};
    std::string measured_source_filter;
    ImFont* measured_font = nullptr;
    float measured_font_size = 0.0f;
    float max_width = 0.0f;
};

struct CanvasState {
    dot_layout::DirectedGraph graph{
        {{"a", "A"}, {"b", "B"}, {"c", "C"}},
        {{"a", "b", "a->b", 1, "inner", "right"}, {"b", "c", "b->c"}},
        {{"inner", "Inner", {"a"}, "outer"},
         {"right", "Right", {"b"}, std::nullopt},
         {"outer", "Outer", {"c"}, std::nullopt}}};
    dot_layout::LayoutOptions options = [] {
        dot_layout::LayoutOptions value;
        value.compound = true;
        value.rank_separation = 4.0;
        value.node_separation = 1.0;
        return value;
    }();
    std::optional<dot_layout::LayoutResult> layout;
    bool dirty = true;
    bool asynchronous = true;
    bool recently_replaced = false;
    std::uint64_t settings_revision = 0;
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    bool origin_initialized = false;
    std::optional<GraphFocus> previous_focus;
    bool click_started = false;
};

class AppContext {
public:
    AppContext();
    ~AppContext();

    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    void initialize(std::string input_file);

    UserSettings settings;
    std::unique_ptr<TextEditor> editor;
    std::unique_ptr<TrieAutoComplete> autocomplete;
    DiagnosticLog log;
    std::string input_file;
    std::string editor_text;
    std::vector<std::size_t> line_offsets;
    CanvasState canvas;
    DiagnosticViewState diagnostic_view;
    std::unique_ptr<AnalysisWorker> worker;
    std::unique_ptr<AnalysisCoordinator> coordinator;
    std::optional<GraphFocus> focus;
    std::optional<CanvasFocus> canvas_focus;
    std::optional<CppGenerationFiles> pending_cpp_generation;
};
