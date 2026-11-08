#include <cmath>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <optional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <imgui_freetype.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <dejavu.h>
#include <notosans.h>

#include <dot_layout.hpp>
#include <smc_text_editor_language.hpp>
#include <smc/generator.hpp>
#include <smc/parser.hpp>
#include <smc/syntax_checker.hpp>
#include <TextEditor.h>

class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;
};

/// Push/PopFont
class Font : private NonCopyable {
public:
    explicit Font(ImFont* font) : m_Active(font != nullptr) {
        if (m_Active) ImGui::PushFont(font, 0.0f);
    }

    ~Font() {
        if (m_Active) ImGui::PopFont();
    }

private:
    bool m_Active = false;
};

class FontSize : private NonCopyable {
public:
    explicit FontSize(float size) : m_Active(size > 0) {
        if (m_Active) ImGui::PushFont(NULL, size);
    }

    ~FontSize() {
        if (m_Active) ImGui::PopFont();
    }

private:
    bool m_Active = false;
};

using DiagnosticLog = std::vector<std::string>;
thread_local bool defer_log_output = false;

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

std::optional<dot_layout::DirectedGraph> make_graph(
    const std::string& input_file = "mapmanager.sm", DiagnosticLog* log = nullptr);
std::optional<dot_layout::DirectedGraph> make_graph_from_contents(
    const std::string& contents, const std::string& source_name, DiagnosticLog* log = nullptr,
    std::vector<SourceSegment>* source_segments = nullptr);
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
std::optional<dot_layout::Point> focused_graph_point(
    const GraphFocus& focus, const dot_layout::DirectedGraph& graph,
    const dot_layout::LayoutResult& layout);
void append_log(DiagnosticLog* log, std::string message);
void draw_dockspace();
void render_graph(std::optional<dot_layout::DirectedGraph> graph = std::nullopt,
                  DiagnosticLog* log = nullptr,
                  std::optional<GraphFocus> focus = std::nullopt);
void render_diagnostics_window(DiagnosticLog& log);

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
    bool asynchronous = false;
    bool recently_replaced = false;
    std::uint64_t settings_revision = 0;
};

CanvasState& canvas_state() {
    static CanvasState state;
    return state;
}

struct AnalysisJob {
    std::uint64_t revision;
    std::optional<std::string> source;
    std::string source_name;
    dot_layout::DirectedGraph graph;
    dot_layout::LayoutOptions options;
};

struct AnalysisResult {
    std::uint64_t revision;
    bool source_job;
    std::optional<dot_layout::DirectedGraph> graph;
    std::optional<dot_layout::LayoutResult> layout;
    std::vector<SourceSegment> source_segments;
    DiagnosticLog diagnostics;
};

class AnalysisWorker {
public:
    AnalysisWorker() : thread_([this] { run(); }) {}
    ~AnalysisWorker() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            pending_.reset();
        }
        condition_.notify_one();
        thread_.join();
    }

    void submit(AnalysisJob job) {
        {
            std::lock_guard lock(mutex_);
            pending_ = std::move(job);
        }
        condition_.notify_one();
    }

    std::optional<AnalysisResult> take_result() {
        std::lock_guard lock(mutex_);
        auto result = std::move(result_);
        result_.reset();
        return result;
    }

private:
    void run() {
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
                result.diagnostics.push_back(std::string("SMC analysis or layout failed: ") + error.what());
            }
            {
                std::lock_guard lock(mutex_);
                result_ = std::move(result);
            }
        }
    }

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

    explicit AnalysisCoordinator(bool has_source, Clock::time_point now)
        : source_needs_analysis_(has_source), deadline_(now) {}

    void text_changed(Clock::time_point now) {
        source_is_indexed_ = false;
        source_needs_analysis_ = true;
        deadline_ = now + std::chrono::milliseconds(500);
        invalidate();
    }

    void settings_changed() { invalidate(); }

    bool source_is_indexed() const { return source_is_indexed_; }
    const std::vector<SourceSegment>& source_segments() const { return source_segments_; }

    void accept(AnalysisResult result, CanvasState& canvas, DiagnosticLog& log) {
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

    std::optional<AnalysisJob> next_job(Clock::time_point now, const std::string& source,
                                        const std::string& source_name, CanvasState& canvas) {
        if (active_revision_)
            return std::nullopt;
        std::optional<AnalysisJob> job;
        if (source_needs_analysis_ && now >= deadline_)
            job = AnalysisJob{revision_, source, source_name, {}, canvas.options};
        else if (canvas.dirty && !source_needs_analysis_)
            job = AnalysisJob{revision_, std::nullopt, source_name, canvas.graph, canvas.options};
        if (job) {
            active_revision_ = revision_;
            canvas.dirty = false;
        }
        return job;
    }

private:
    void invalidate() {
        ++revision_;
        active_revision_.reset();
    }

    std::uint64_t revision_ = 0;
    std::optional<std::uint64_t> active_revision_;
    bool source_needs_analysis_;
    bool source_is_indexed_ = false;
    Clock::time_point deadline_;
    std::vector<SourceSegment> source_segments_;
};

int main(int argc, char **argv) {
  const char *glsl_version = "#version 330";
  if (glfwInit() == GL_FALSE) {
    return GL_FALSE;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  GLFWwindow *main_window = glfwCreateWindow(1280, 800, "smc", NULL, NULL);
  if (main_window == NULL) {
    glfwTerminate();
    return GL_FALSE;
  }

  glfwMakeContextCurrent(main_window);
  glfwSwapInterval(1);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking

  float xscale, yscale;
  glfwGetWindowContentScale((GLFWwindow *)main_window, &xscale, &yscale);

  ImGuiStyle &style = ImGui::GetStyle();
  style.FontScaleDpi = std::max(xscale, yscale);

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(main_window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  io.Fonts->Clear();

  ImFontConfig dejaVuConfig;
  std::copy_n("DejaVu", 7, dejaVuConfig.Name);
  dejaVuConfig.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
  dejaVuConfig.FontDataOwnedByAtlas = false;
  io.Fonts->AddFontFromMemoryCompressedTTF(static_cast<const void *>(&dejavu),
                                           dejavuSize, 15.0f, &dejaVuConfig);

  ImFontConfig notoSansConfig;
  std::copy_n("NotoSansSC", 11, notoSansConfig.Name);
  notoSansConfig.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
  notoSansConfig.FontDataOwnedByAtlas = false;
  notoSansConfig.MergeMode = true;
  io.Fonts->AddFontFromMemoryCompressedTTF(static_cast<const void *>(&notosans),
                                           notosansSize, 15.0f,
                                           &notoSansConfig);

  TextEditor editor;
  editor.SetLanguage(smc_editor::language());
  DiagnosticLog log;
  const std::string input_file = argc > 1 ? argv[1] : "mapmanager.sm";
  const auto contents = read_input_file(input_file, &log);
  if (contents)
    editor.SetText(*contents);
  std::string editor_text = editor.GetText();
  auto line_offsets = source_line_offsets(editor_text);
  auto& canvas = canvas_state();
  canvas.asynchronous = true;
  AnalysisWorker worker;
  AnalysisCoordinator coordinator(contents.has_value(), AnalysisCoordinator::Clock::now());
  if (!contents)
    append_log(&log, "Displaying the built-in example graph until valid SMC is available.");

  // Main loop
  while (glfwWindowShouldClose(main_window) == GL_FALSE) {
    // Poll for and process events
    glfwPollEvents();

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    draw_dockspace();

    bool editor_changed = false;
    if (ImGui::Begin("SMC Editor")) {
      editor_changed = editor.Render("Source", ImVec2(0.0f, 0.0f));
    }
    ImGui::End();

    if (editor_changed) {
      editor_text = editor.GetText();
      line_offsets = source_line_offsets(editor_text);
      coordinator.text_changed(AnalysisCoordinator::Clock::now());
    }

    if (auto result = worker.take_result())
      coordinator.accept(std::move(*result), canvas, log);

    std::optional<GraphFocus> focus;
    if (coordinator.source_is_indexed()) {
      const auto cursor_offset = editor_offset_at_position(
          editor_text, line_offsets, editor.GetMainCursorPosition());
      focus = focus_at_offset(coordinator.source_segments(), cursor_offset);
    }
    const auto previous_settings_revision = canvas.settings_revision;
    render_graph(std::nullopt, &log, focus);
    if (canvas.settings_revision != previous_settings_revision)
      coordinator.settings_changed();

    if (auto job = coordinator.next_job(AnalysisCoordinator::Clock::now(),
                                        editor_text, input_file, canvas))
      worker.submit(std::move(*job));
    render_diagnostics_window(log);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(main_window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(main_window);
  glfwTerminate();
  return 0;
}

void draw_dockspace() {
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |ImGuiWindowFlags_NoBackground;

    bool p_open = true;
    ImGui::Begin("MyDockSpace", &p_open, window_flags);

    ImGui::PopStyleVar(3);

    // DockSpace
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_AutoHideTabBar);
    }
    ImGui::End();
}

void draw_spline_with_arrow(ImDrawList *draw_list, const std::vector<ImVec2> &spl,
                            const std::optional<ImVec2>& tip, ImColor color,
                            float thickness = 1.0f, float arrow_sz = 10.0f) {
    if (spl.empty())
        return;
    draw_list->PathLineTo(spl[0]);

    for (size_t i = 0; i < (spl.size() - 1) / 3; i++) {
        draw_list->PathBezierCubicCurveTo(
            spl[i * 3 + 1],
            spl[i * 3 + 2],
            spl[i * 3 + 3], 0);
    }

    if ((spl.size() - 1) % 3 == 1) {
        draw_list->PathLineTo(spl[spl.size() - 1]);
    }
    else if ((spl.size() - 1) % 3 == 2) {
        draw_list->PathLineTo(spl[spl.size() - 2]);
        draw_list->PathLineTo(spl[spl.size() - 1]);
    }
    else { } //==0

    if (tip)
        draw_list->PathLineTo(*tip);
    draw_list->PathStroke(color, thickness);

    if (tip && arrow_sz > 0.0f) {
        ImVec2 delta;
        float length_squared = 0.0f;
        for (auto point = spl.rbegin(); point != spl.rend(); ++point) {
            delta = *tip - *point;
            length_squared = delta.x * delta.x + delta.y * delta.y;
            if (length_squared > 0.0f)
                break;
        }
        if (length_squared == 0.0f)
            throw std::runtime_error("Cannot determine arrow direction");
        const ImVec2 direction = delta * (1.0f / std::sqrt(length_squared));
        const ImVec2 base = *tip - direction * arrow_sz;
        const ImVec2 side(-direction.y * arrow_sz * 0.4f,
                          direction.x * arrow_sz * 0.4f);
        draw_list->AddTriangleFilled(*tip, base + side, base - side, color);
    }
}

void drag_positive_value(const char* label, float& value, float speed, DiagnosticLog* log) {
    float edited = value;
    if (ImGui::DragFloat(label, &edited, speed, 0.01f, 100.0f,
                         "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        if (std::isfinite(edited) && edited > 0.0f) {
            value = edited;
        } else {
            append_log(log, std::string(label) + " must be positive and finite");
        }
    }
}

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

bool same_focus(const std::optional<GraphFocus>& left,
                const std::optional<GraphFocus>& right) {
    if (left.has_value() != right.has_value())
        return false;
    return !left || (left->kind == right->kind &&
                     left->graph_indices == right->graph_indices);
}

bool same_layout_options(const dot_layout::LayoutOptions& left,
                         const dot_layout::LayoutOptions& right) {
    const bool same_size = left.size.has_value() == right.size.has_value() &&
        (!left.size || (left.size->width == right.size->width &&
                        left.size->height == right.size->height));
    return left.rank_direction == right.rank_direction &&
           left.rank_separation == right.rank_separation &&
           left.node_separation == right.node_separation &&
           left.compound == right.compound && left.ratio == right.ratio &&
           same_size;
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

void render_diagnostics_window(DiagnosticLog& log) {
    if (!ImGui::Begin("Status / Diagnostics")) {
        ImGui::End();
        return;
    }

    static const DiagnosticLog* measured_log = nullptr;
    static std::size_t measured_count = 0;
    struct LogLine { std::size_t message, begin, end; };
    static std::vector<LogLine> lines;
    static ImFont* measured_font = nullptr;
    static float measured_font_size = 0.0f;
    static float max_width = 0.0f;
    if (ImGui::Button("Clear"))
        log.clear();

    ImGui::Separator();
    if (measured_log != &log || measured_count > log.size() ||
        measured_font != ImGui::GetFont() || measured_font_size != ImGui::GetFontSize()) {
        measured_log = &log;
        measured_count = 0;
        measured_font = ImGui::GetFont();
        measured_font_size = ImGui::GetFontSize();
        max_width = 0.0f;
        lines.clear();
    }
    for (; measured_count < log.size(); ++measured_count) {
        const auto& message = log[measured_count];
        std::size_t begin = 0;
        for (;;) {
            const auto newline = message.find('\n', begin);
            const auto end = newline == std::string::npos ? message.size() : newline;
            lines.push_back({measured_count, begin, end});
            max_width = std::max(max_width, ImGui::CalcTextSize(
                message.data() + begin, message.data() + end).x);
            if (newline == std::string::npos)
                break;
            begin = newline + 1;
        }
    }
    ImGui::SetNextWindowContentSize(ImVec2(max_width + ImGui::GetStyle().WindowPadding.x * 2.0f, 0.0f));
    if (ImGui::BeginChild("DiagnosticLog", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        const bool was_at_bottom =
            ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(lines.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& line = lines[static_cast<std::size_t>(i)];
                const auto& message = log[line.message];
                ImGui::TextUnformatted(message.data() + line.begin, message.data() + line.end);
            }
        if (was_at_bottom)
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

std::optional<dot_layout::Point> focused_graph_point(
    const GraphFocus& focus, const dot_layout::DirectedGraph& graph,
    const dot_layout::LayoutResult& layout) {
    if (focus.graph_indices.empty())
        return std::nullopt;
    if (focus.kind == GraphObjectKind::state_map) {
        const auto index = focus.graph_indices.front();
        if (index >= graph.clusters.size())
            return std::nullopt;
        const auto cluster = std::find_if(layout.clusters.begin(), layout.clusters.end(),
            [&](const auto& candidate) {
                return candidate.id == graph.clusters[index].id;
            });
        if (cluster == layout.clusters.end())
            return std::nullopt;
        return dot_layout::Point{
            (cluster->bounds.min_x + cluster->bounds.max_x) * 0.5,
            (cluster->bounds.min_y + cluster->bounds.max_y) * 0.5};
    }
    if (focus.kind == GraphObjectKind::state) {
        const auto index = focus.graph_indices.front();
        if (index >= layout.nodes.size())
            return std::nullopt;
        return layout.nodes[index].center;
    }

    double min_x = std::numeric_limits<double>::infinity();
    double min_y = std::numeric_limits<double>::infinity();
    double max_x = -std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();
    bool found_point = false;
    const auto include_point = [&](dot_layout::Point point) {
        min_x = std::min(min_x, point.x);
        min_y = std::min(min_y, point.y);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
        found_point = true;
    };
    for (const auto edge_index : focus.graph_indices) {
        if (edge_index >= graph.edges.size() || edge_index >= layout.edges.size())
            continue;
        const auto& edge = layout.edges[edge_index];
        if (focus.kind == GraphObjectKind::guard && edge.label)
            return edge.label->center;
        if (edge.label)
            include_point(edge.label->center);
        for (const auto& spline : edge.spline_points)
            for (const auto& point : spline)
                include_point(point);
        for (const auto& arrow : edge.arrow_tips)
            if (arrow)
                include_point(*arrow);
    }
    if (!found_point)
        return std::nullopt;
    return dot_layout::Point{(min_x + max_x) * 0.5, (min_y + max_y) * 0.5};
}

void render_graph(std::optional<dot_layout::DirectedGraph> graph_in, DiagnosticLog* log,
                  std::optional<GraphFocus> focus) {
    auto& canvas = canvas_state();
    auto& graph = canvas.graph;
    const bool graph_replaced = graph_in.has_value() || canvas.recently_replaced;
    canvas.recently_replaced = false;
    if (graph_in)
       graph = std::move(*graph_in);
    
    ImGui::Begin("canvas", NULL, ImGuiWindowFlags_MenuBar);
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->ChannelsSplit(3);
    ImGuiStyle &style = ImGui::GetStyle();
    static auto origin = ImGui::GetCursorScreenPos();
    static float& scale = style.FontScaleMain;
    static std::optional<GraphFocus> previous_focus;
    auto& options = canvas.options;
    static float rank_separation = 4.0f;
    static float node_separation = 1.0f;
    static float arrow_size = 10.0f;
    enum class RatioSelection { Default, Fill, Compress, Expand, Numeric };
    static RatioSelection ratio_selection = RatioSelection::Default;
    static float ratio_value = 1.0f;
    static float size_width = 8.0f;
    static float size_height = 6.0f;
    bool graph_changed = graph_in.has_value();
    const auto previous_options = options;
    const ImColor text_col = ImGui::GetStyle().Colors[ImGuiCol_Text];
    const ImColor line_col = ImGui::GetStyle().Colors[ImGuiCol_PlotLines];
    ImColor node_col = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];node_col.Value.w = 1.0f;

    if (ImGui::IsWindowHovered() &&
        ImGui::IsMouseDragging(0)) {
        const ImVec2 drag_delta = ImGui::GetIO().MouseDelta;
        origin.x += drag_delta.x;
        origin.y += drag_delta.y;
    }

    if (ImGui::BeginMenuBar()) {
        if(ImGui::BeginMenu("settings")) {
            ImGui::DragFloat("Scale", &scale, 0.01f, 0.2f, 3.0f);
            ImGui::RadioButton("LeftToRight", options.rank_direction == dot_layout::RankDirection::LeftToRight);
            if ( ImGui::IsItemClicked() ) {
                options.rank_direction = dot_layout::RankDirection::LeftToRight;
            }

            ImGui::RadioButton("TopToBottom", options.rank_direction == dot_layout::RankDirection::TopToBottom);
            if ( ImGui::IsItemClicked() )
                options.rank_direction = dot_layout::RankDirection::TopToBottom;

            ImGui::DragFloat("Rank Separation", &rank_separation, 0.01f, 0.01f, 100.0f);
            ImGui::DragFloat("Node Separation", &node_separation, 0.01f, 0.01f, 100.0f);
            ImGui::DragFloat("Arrow Size", &arrow_size, 0.01f, 0.0f, 100.0f);
            ImGui::Checkbox("Compound", &options.compound);
            if (ImGui::BeginMenu("Ratio")) {
                if (ImGui::RadioButton("Default", ratio_selection == RatioSelection::Default))
                    ratio_selection = RatioSelection::Default;
                if (ImGui::RadioButton("Fill", ratio_selection == RatioSelection::Fill))
                    ratio_selection = RatioSelection::Fill;
                if (ImGui::RadioButton("Compress", ratio_selection == RatioSelection::Compress))
                    ratio_selection = RatioSelection::Compress;
                if (ImGui::RadioButton("Expand", ratio_selection == RatioSelection::Expand))
                    ratio_selection = RatioSelection::Expand;
                if (ImGui::RadioButton("Numeric", ratio_selection == RatioSelection::Numeric))
                    ratio_selection = RatioSelection::Numeric;
                if (ratio_selection == RatioSelection::Numeric)
                    drag_positive_value("Height / width", ratio_value, 0.01f, log);
                if (ratio_selection == RatioSelection::Fill ||
                    ratio_selection == RatioSelection::Compress ||
                    ratio_selection == RatioSelection::Expand) {
                    drag_positive_value("Width (in)", size_width, 0.05f, log);
                    drag_positive_value("Height (in)", size_height, 0.05f, log);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Node shapes")) {
                constexpr dot_layout::NodeShape shapes[] = {
                    dot_layout::NodeShape::Box, dot_layout::NodeShape::Circle,
                    dot_layout::NodeShape::Ellipse, dot_layout::NodeShape::Point};
                constexpr const char* shape_names[] = {"box", "circle", "ellipse", "point"};
                for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
                    auto& node = graph.nodes[i];
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::BeginCombo(node.label.c_str(), shape_names[static_cast<int>(node.shape)])) {
                        for (std::size_t j = 0; j < 4; ++j) {
                            const bool selected = node.shape == shapes[j];
                            if (ImGui::Selectable(shape_names[j], selected) && !selected) {
                                node.shape = shapes[j];
                                graph_changed = true;
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edge weights")) {
                std::unordered_map<std::string_view, std::string_view> labels;
                labels.reserve(graph.nodes.size());
                for (const auto& node : graph.nodes)
                    labels.emplace(node.id, node.label);
                for (std::size_t i = 0; i < graph.edges.size(); ++i) {
                    auto& edge = graph.edges[i];
                    ImGui::PushID(static_cast<int>(i));
                    const auto label_for = [&](const std::string& id) -> std::string_view {
                        const auto node = labels.find(id);
                        if (node == labels.end())
                            throw std::runtime_error("Edge endpoint is missing from graph");
                        return node->second;
                    };
                    const std::string name = std::string(label_for(edge.from)) + " -> " +
                        std::string(label_for(edge.to)) +
                        " (" + std::to_string(i + 1) + ")";
                    ImGui::SetNextItemWidth(100.0f);
                    graph_changed |= ImGui::DragInt(name.c_str(), &edge.weight, 1.0f, 1, 1000,
                                                   "%d", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    options.rank_separation = rank_separation;
    options.node_separation = node_separation;
    options.size.reset();
    switch (ratio_selection) {
    case RatioSelection::Default: options.ratio.reset(); break;
    case RatioSelection::Numeric: options.ratio = static_cast<double>(ratio_value); break;
    case RatioSelection::Fill:
    case RatioSelection::Compress:
    case RatioSelection::Expand:
        options.size = dot_layout::Size{size_width, size_height};
        if (ratio_selection == RatioSelection::Fill)
            options.ratio = dot_layout::RatioMode::Fill;
        else if (ratio_selection == RatioSelection::Compress)
            options.ratio = dot_layout::RatioMode::Compress;
        else
            options.ratio = dot_layout::RatioMode::Expand;
        break;
    }

    if (graph_changed || !same_layout_options(previous_options, options)) {
        canvas.dirty = true;
        ++canvas.settings_revision;
    }
    if (!canvas.asynchronous && (canvas.dirty || !canvas.layout)) {
        auto layout_graph = graph;
        if (!options.compound) {
            for (auto& edge : layout_graph.edges) {
                edge.tail_cluster.reset();
                edge.head_cluster.reset();
            }
        }
        canvas.layout = dot_layout::DotLayout{}.layout(layout_graph, options);
        canvas.dirty = false;
    }
    if (!canvas.layout) {
        draw_list->ChannelsMerge();
        ImGui::End();
        return;
    }
    const auto& layout = *canvas.layout;

    if (graph_replaced || !same_focus(previous_focus, focus)) {
        if (focus) {
            if (const auto target = focused_graph_point(*focus, graph, layout)) {
                const ImVec2 center =
                    ImGui::GetCurrentWindow()->ContentRegionRect.GetCenter();
                origin.x = center.x - static_cast<float>(
                    (target->x - layout.bounds.min_x) * scale);
                origin.y = center.y - static_cast<float>(
                    (layout.bounds.max_y - target->y) * scale);
            }
        }
        previous_focus = focus;
    }
    const auto focus_contains = [&](GraphObjectKind kind, std::size_t graph_index) {
        return focus && focus->kind == kind &&
            std::find(focus->graph_indices.begin(), focus->graph_indices.end(),
                      graph_index) != focus->graph_indices.end();
    };
    const ImColor focus_col(45, 220, 95, 255);

    auto to_screen = [&](dot_layout::Point point) {
    return ImVec2( origin.x + (point.x - layout.bounds.min_x) * scale, 
        origin.y + (layout.bounds.max_y - point.y) * scale);
    };
    const ImRect visible(draw_list->GetClipRectMin(), draw_list->GetClipRectMax());
    const auto visible_rect = [&](ImVec2 min, ImVec2 max, float padding = 0.0f) {
        return min.x - padding <= visible.Max.x && max.x + padding >= visible.Min.x &&
               min.y - padding <= visible.Max.y && max.y + padding >= visible.Min.y;
    };
    const auto visible_spline = [&](const dot_layout::EdgeLayout& edge, std::size_t index) {
        ImVec2 min(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
        ImVec2 max(-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity());
        const auto include = [&](ImVec2 point) {
            min.x = std::min(min.x, point.x);
            min.y = std::min(min.y, point.y);
            max.x = std::max(max.x, point.x);
            max.y = std::max(max.y, point.y);
        };
        for (const auto& point : edge.spline_points[index])
            include(to_screen(point));
        if (edge.arrow_tips[index])
            include(to_screen(*edge.arrow_tips[index]));
        return visible_rect(min, max, arrow_size + 4.0f);
    };
    auto spline_to_screen = [&](const std::vector<dot_layout::Point>& spline) {
        std::vector<ImVec2> result;
        result.reserve(spline.size());
        for (const auto& point : spline) {
            result.push_back(to_screen(point));
        }
        return result;
    };

    {
        FontSize font_size(14.0f);

        draw_list->ChannelsSetCurrent(0);
        for (std::size_t cluster_index = 0;
             cluster_index < layout.clusters.size(); ++cluster_index) {
            const auto& cluster = layout.clusters[cluster_index];
            const ImVec2 top_left = to_screen({cluster.bounds.min_x, cluster.bounds.max_y});
            const ImVec2 bottom_right = to_screen({cluster.bounds.max_x, cluster.bounds.min_y});
            const bool highlighted =
                focus_contains(GraphObjectKind::state_map, cluster_index);
            if (visible_rect(top_left, bottom_right, 4.0f))
                draw_list->AddRect(top_left, bottom_right,
                                   highlighted ? focus_col : line_col,
                                   0.0f, highlighted ? 3.0f : 1.0f);
            if (cluster.label) {
                const ImVec2 center = to_screen(cluster.label->center);
                const ImVec2 text_size = ImGui::CalcTextSize(cluster.label->text.c_str());
                const ImVec2 pos(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
                if (visible_rect(pos, ImVec2(pos.x + text_size.x, pos.y + text_size.y), 4.0f))
                    draw_list->AddText(pos, highlighted ? focus_col : text_col,
                                       cluster.label->text.c_str());
            }
        }

        // Draw nodes
        draw_list->ChannelsSetCurrent(2);
        for (std::size_t i = 0; i < layout.nodes.size(); ++i) {
            const auto& node = layout.nodes[i];
            const auto& label = graph.nodes[i].label;
            const float x = origin.x + (node.center.x - layout.bounds.min_x) * scale;
            const float y = origin.y + (layout.bounds.max_y - node.center.y) * scale;
            const float width = node.width * scale;
            const float height = node.height * scale;
            const ImVec2 label_size = ImGui::CalcTextSize(label.c_str());
            const ImVec2 label_pos(x - label_size.x * 0.5f, y - label_size.y * 0.5f);
            const ImVec2 tl(x - width * 0.5f, y - height * 0.5f);
            const ImVec2 br(x + width * 0.5f, y + height * 0.5f);
            const bool highlighted = focus_contains(GraphObjectKind::state, i);
            
            if (visible_rect(tl, br, 4.0f) && node.shape == dot_layout::NodeShape::Box) {
                draw_list->AddRectFilled(tl, br, node_col);
                if (highlighted)
                    draw_list->AddRect(tl, br, focus_col, 0.0f, 3.0f);
            } else if (visible_rect(tl, br, 4.0f)) {
                draw_list->AddEllipseFilled(ImVec2(x, y), ImVec2(width * 0.5f, height * 0.5f), node_col);
                if (highlighted)
                    draw_list->AddEllipse(ImVec2(x, y),
                        ImVec2(width * 0.5f, height * 0.5f), focus_col, 0, 3.0f);
            }
            if (node.shape != dot_layout::NodeShape::Point &&
                visible_rect(label_pos, ImVec2(label_pos.x + label_size.x, label_pos.y + label_size.y), 4.0f))
                draw_list->AddText(label_pos, highlighted ? focus_col : text_col,
                                   label.c_str());
        }
        
        // Draw edges
        draw_list->ChannelsSetCurrent(1);
        for (std::size_t edge_index = 0; edge_index < layout.edges.size(); ++edge_index) {
            const auto& edge = layout.edges[edge_index];
            const bool highlighted =
                focus_contains(GraphObjectKind::transition, edge_index) ||
                focus_contains(GraphObjectKind::guard, edge_index);
            const auto edge_color = highlighted ? focus_col : line_col;
            for (std::size_t i = 0; i < edge.spline_points.size(); ++i) {
                if (!visible_spline(edge, i))
                    continue;
                auto spl = spline_to_screen(edge.spline_points[i]);
                std::optional<ImVec2> tip;
                if (edge.arrow_tips[i])
                    tip = to_screen(*edge.arrow_tips[i]);
                draw_spline_with_arrow(draw_list, spl, tip, edge_color,
                                       highlighted ? 3.0f : 1.0f, arrow_size);
            }
            if (edge.label) {
                const ImVec2 center = to_screen(edge.label->center);
                const ImVec2 half_size(edge.label->width * scale * 0.5f,
                                    edge.label->height * scale * 0.5f);
                const ImVec2 pos(center.x - half_size.x, center.y - half_size.y);
                const ImVec2 size = ImGui::CalcTextSize(edge.label->text.c_str());
                if (visible_rect(pos, ImVec2(pos.x + size.x, pos.y + size.y), 4.0f))
                    draw_list->AddText(pos, highlighted ? focus_col : text_col,
                                       edge.label->text.c_str());
            }
        }

        draw_list->ChannelsMerge();
    }

    ImGui::End();
}