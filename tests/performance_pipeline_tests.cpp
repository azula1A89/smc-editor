#include <algorithm>
#include <ctime>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <imgui.h>
#include <imgui_internal.h>

#include <dejavu.h>

#include <analysis.hpp>
#include <app_context.hpp>
#include <smc_text_editor_language.hpp>
#include <ui_windows.hpp>

namespace {

AppContext& test_app() {
    static AppContext app;
    app.canvas.asynchronous = false;
    return app;
}

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

AnalysisResult wait_for(AnalysisWorker& worker, std::uint64_t revision) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = worker.take_result()) {
            if (result->revision == revision)
                return std::move(*result);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Worker did not finish");
}

void test_worker() {
    const auto file = std::string(TEST_SOURCE_DIR) + "/tests/fsm_graph.sm";
    const auto contents = read_input_file(file);
    require(contents.has_value(), "Could not read test fixture");
    AnalysisWorker worker;
    dot_layout::LayoutOptions options;
    worker.submit({1, *contents, file, {}, options});
    auto valid = wait_for(worker, 1);
    require(valid.graph && valid.layout && !valid.source_segments.empty() &&
                !valid.source_ranges.empty() && !valid.ast.roots.empty(),
            "Worker did not return a graph, layout and source mapping");
    require(valid.graph->nodes.size() == valid.layout->nodes.size() &&
                valid.graph->edges.size() == valid.layout->edges.size(),
            "Worker returned inconsistent graph and layout");
    UserSettings settings;
    settings.capture_graph(*valid.graph);
    settings.graph_preferences.node_shapes[0] =
        static_cast<int>(dot_layout::NodeShape::Circle);
    settings.graph_preferences.edge_weights[0] = 23;
    worker.submit({7, *contents, file, {}, options, settings.graph_preferences});
    auto customized = wait_for(worker, 7);
    require(customized.graph &&
                customized.graph->nodes[0].shape == dot_layout::NodeShape::Circle &&
                customized.graph->edges[0].weight == 23,
            "Worker did not restore saved node and edge preferences");
    worker.submit({2, std::string("%unknown\n"), file, {}, options});
    auto invalid = wait_for(worker, 2);
    require(!invalid.graph && !invalid.layout && !invalid.diagnostics.empty() &&
                invalid.source_name == file && !invalid.source_diagnostics.empty() &&
                invalid.source_diagnostics[0].location.line == 1,
            "Worker did not report invalid input");
    worker.submit({3, std::nullopt, file, *valid.graph, options});
    auto relayout = wait_for(worker, 3);
    require(relayout.graph && relayout.layout && !relayout.source_job,
            "Worker could not relayout an existing graph");
    options.rank_separation = 0;
    worker.submit({4, std::nullopt, file, *valid.graph, options});
    auto failure = wait_for(worker, 4);
    require(!failure.graph && !failure.layout && !failure.diagnostics.empty(),
            "Worker did not report a layout failure");
    options.rank_separation = 0.5;
    worker.submit({5, std::nullopt, file, *valid.graph, options});
    worker.submit({6, *contents, file, {}, options});
    auto latest = wait_for(worker, 6);
    require(latest.graph && latest.layout && latest.source_job,
            "Worker did not complete the latest queued request");
}

void test_worker_notification() {
    std::mutex mutex;
    std::condition_variable condition;
    int notifications = 0;
    AnalysisWorker worker([&] {
        {
            std::lock_guard lock(mutex);
            ++notifications;
        }
        condition.notify_one();
    });

    const auto wait_for_notification = [&](int expected) {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock, std::chrono::seconds(30),
                                   [&] { return notifications >= expected; }),
                "Worker did not notify when its result was ready");
    };

    worker.submit({42, std::string("%unknown\n"), "invalid.sm", {}, {}});
    wait_for_notification(1);
    auto result = worker.take_result();
    require(result && result->revision == 42 && !result->diagnostics.empty(),
            "Worker notification did not publish the result");
    worker.submit({43, std::nullopt, "graph.sm",
                   dot_layout::DirectedGraph{{{"a", "A"}}, {}, {}}, {}});
    wait_for_notification(2);
    result = worker.take_result();
    require(result && result->revision == 43 && result->layout,
            "Worker did not publish and notify for a subsequent successful job");
}

void test_cached_offsets() {
    const std::string text = "a\r\n猫z\n";
    const auto offsets = source_line_offsets(text);
    require(editor_offset_at_position(text, offsets, TextEditor::DocPos(1, 1)) == 6,
            "Cached UTF-8/CRLF position is incorrect");
    require(editor_offset_at_position(text, offsets, TextEditor::DocPos(2, 0)) == text.size(),
            "Cached final line position is incorrect");
}

void test_semantic_highlighting() {
    using namespace std::chrono_literals;
    const std::string source =
        "// Shared 猫\r\n"
        "%start Shared::Shared\r\n%class Context\r\n%header context.h\r\n"
        "%map /* 猫 */ Shared\r\n%%\r\n"
        "Shared\r\n{\r\n"
        " Shared Shared { Shared(); }\r\n"
        " leave Shared/push(Other::Shared) {}\r\n"
        " jumpTo jump(\r\n Other::Shared \r\n) {}\r\n"
        " bounce push( Shared ) {}\r\n"
        " back pop( Shared ) {}\r\n"
        "}\r\n%%\r\n%map Other\r\n%%\r\nShared\r\n{\r\n Shared nil {}\r\n}\r\n%%\r\n";
    std::optional<std::vector<SemanticRange>> ranges;
    require(make_graph_from_contents(source, "semantic.sm", nullptr, nullptr, nullptr,
                                     nullptr, nullptr, &ranges).has_value() &&
                ranges.has_value(),
            "Semantic fixture did not parse");
    const auto at = [&](std::size_t offset) -> std::optional<SemanticKind> {
        for (const auto& range : *ranges)
            if (range.begin <= offset && offset < range.end)
                return range.kind;
        return std::nullopt;
    };
    const auto check = [&](std::string_view text, std::size_t local, SemanticKind kind) {
        const auto start = source.find(text);
        require(start != std::string::npos && at(start + local) == kind,
                "Semantic name has an incorrect type or position");
    };
    check("%start Shared::Shared", 7, SemanticKind::state_map);
    check("%start Shared::Shared", 15, SemanticKind::state);
    check("*/ Shared\r\n%%", 3, SemanticKind::state_map);
    check("\r\nShared\r\n{", 2, SemanticKind::state);
    check(" Shared Shared {", 1, SemanticKind::transition);
    check(" Shared Shared {", 8, SemanticKind::state);
    check("push(Other::Shared)", 5, SemanticKind::state_map);
    check("push(Other::Shared)", 12, SemanticKind::state);
    check("jump(\r\n Other::Shared", 8, SemanticKind::state_map);
    check("jump(\r\n Other::Shared", 15, SemanticKind::state);
    check("push( Shared )", 6, SemanticKind::state);
    check("pop( Shared )", 5, SemanticKind::transition);
    check("%map Other", 5, SemanticKind::state_map);
    require(!at(source.find("// Shared") + 3) &&
                !at(source.find("Shared();")) &&
                !at(source.find("nil {}")) &&
                !at(source.find("Shared::Shared") + 6),
            "Comments, actions, keywords, or scope punctuation were colored");
    require(ranges->size() == 20, "Unexpected number of semantic ranges");
    for (std::size_t i = 0; i < ranges->size(); ++i) {
        require((*ranges)[i].begin < (*ranges)[i].end &&
                    (i == 0 || (*ranges)[i - 1].end <= (*ranges)[i].begin),
                "Semantic ranges are empty, unsorted, or overlapping");
    }

    TextEditor editor;
    editor.SetText(source);
    const auto editor_text = editor.GetText();
    AnalysisWorker worker;
    dot_layout::LayoutOptions options;
    options.rank_separation = 0;
    worker.submit({1, editor_text, "semantic.sm", {}, options});
    auto failure = wait_for(worker, 1);
    require(!failure.layout && failure.semantic_ranges && !failure.semantic_ranges->empty(),
            "Layout failure discarded valid semantic ranges");

    AnalysisCoordinator coordinator(true, AnalysisCoordinator::Clock::time_point{});
    CanvasState canvas;
    DiagnosticLog log(DiagnosticLog::default_capacity, false);
    const auto now = AnalysisCoordinator::Clock::time_point{};
    auto job = coordinator.next_job(now, editor_text, "semantic.sm", canvas);
    require(job.has_value(), "Semantic analysis was not scheduled");
    failure.revision = job->revision;
    coordinator.accept(std::move(failure), canvas, log, &editor);
    const auto offsets = source_line_offsets(editor_text);
    const auto map_pos = editor_position_at_offset(editor_text, offsets, editor_text.find("*/ Shared\n%%") + 3);
    const auto state_pos = editor_position_at_offset(editor_text, offsets, editor_text.find("\nShared\n{") + 1);
    const auto trans_pos = editor_position_at_offset(editor_text, offsets, editor_text.find(" Shared Shared {") + 1);
    require(editor.GetSemanticColor(map_pos) == TextEditor::Color::semanticMap &&
                editor.GetSemanticColor(state_pos) == TextEditor::Color::semanticState &&
                editor.GetSemanticColor(trans_pos) == TextEditor::Color::semanticTransition,
            "Editor did not apply semantic colors after layout failure");
    auto layout_job = coordinator.next_job(now, editor_text, "semantic.sm", canvas);
    require(layout_job && !layout_job->source, "Layout-only job was not scheduled");
    AnalysisResult layout{layout_job->revision, false};
    coordinator.accept(std::move(layout), canvas, log, &editor);
    require(editor.GetSemanticColor(map_pos) == TextEditor::Color::semanticMap,
            "Layout-only result cleared semantic colors");
    coordinator.text_changed(now, &editor);
    require(editor.GetSemanticColor(map_pos) == TextEditor::Color::count,
            "Edit did not clear semantic colors");
    auto stale_job = coordinator.next_job(now + 500ms, editor_text, "semantic.sm", canvas);
    require(stale_job.has_value(), "Changed source was not scheduled");
    coordinator.text_changed(now + 600ms, &editor);
    AnalysisResult stale{stale_job->revision, true};
    stale.semantic_ranges = *ranges;
    coordinator.accept(std::move(stale), canvas, log, &editor);
    require(editor.GetSemanticColor(map_pos) == TextEditor::Color::count,
            "Stale analysis restored semantic colors");
    auto invalid_job = coordinator.next_job(now + 1100ms, editor_text, "semantic.sm", canvas);
    require(invalid_job.has_value(), "Latest source was not scheduled");
    AnalysisResult invalid_result{invalid_job->revision, true};
    coordinator.accept(std::move(invalid_result), canvas, log, &editor);
    require(editor.GetSemanticColor(map_pos) == TextEditor::Color::count,
            "Invalid source restored semantic colors");
    std::optional<std::vector<SemanticRange>> invalid = *ranges;
    require(!make_graph_from_contents("%unknown\n", "semantic.sm", nullptr, nullptr,
                                      nullptr, nullptr, nullptr, &invalid) && !invalid,
            "Invalid source retained semantic ranges");
}

void test_semantic_rendering() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(900, 600);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        static_cast<const void*>(&dejavu), dejavuSize, 15.0f);
    TextEditor editor;
    editor.SetLanguage(smc_editor::language());
    editor.SetText("%map state transition");
    editor.SetSemanticRanges({
        {{0, 1}, {0, 4}, TextEditor::Color::semanticMap},
        {{0, 5}, {0, 10}, TextEditor::Color::semanticState},
        {{0, 11}, {0, 21}, TextEditor::Color::semanticTransition}});
    for (const auto* palette : {&TextEditor::GetDarkPalette(), &TextEditor::GetLightPalette()}) {
        editor.SetPalette(*palette);
        bool seen[3]{};
        for (int frame = 0; frame < 2; ++frame) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Always);
            ImGui::Begin("Semantic test");
            editor.Render("Source", ImVec2(0, 0));
            ImGui::End();
            ImGui::Render();
            for (const auto* draw_list : ImGui::GetDrawData()->CmdLists)
                for (const auto& vertex : draw_list->VtxBuffer) {
                    seen[0] |= vertex.col == palette->get(TextEditor::Color::semanticMap);
                    seen[1] |= vertex.col == palette->get(TextEditor::Color::semanticState);
                    seen[2] |= vertex.col == palette->get(TextEditor::Color::semanticTransition);
                }
        }
        require(seen[0] && seen[1] && seen[2],
                "Semantic colors were not rendered for the selected theme");
    }
    ImGui::DestroyContext();
}

void test_error_markers() {
    using Severity = smc::DiagnosticSeverity;
    DiagnosticLog parse_log(DiagnosticLog::default_capacity, false);
    smc::Diagnostics parsed;
    require(!make_graph_from_contents("\n%unknown\n", "test.sm", &parse_log,
                                      nullptr, nullptr, nullptr, &parsed) &&
                !parsed.empty() && parsed[0].severity == Severity::error &&
                parsed[0].location.file == "test.sm" && parsed[0].location.line == 2 &&
                parsed[0].location.column == 1 && !parsed[0].message.empty() &&
                !parse_log.snapshot().records.empty(),
            "Structured parser diagnostic or existing log was lost");
    const std::string invalid_semantics =
        "%start Main::Missing\n%class Example\n%map Main\n%%\nReady\n{\n}\n%%\n";
    require(!make_graph_from_contents(invalid_semantics, "test.sm", &parse_log,
                                      nullptr, nullptr, nullptr, &parsed) &&
                parsed.size() >= 2 && parsed[0].location.line == 1 &&
                parsed[0].message.find("%header") != std::string::npos &&
                parsed[1].message.find("does not exist") != std::string::npos,
            "Structured syntax-checker diagnostics were not retained");
    AnalysisWorker worker;
    worker.submit({1, std::string("\n%unknown\n"), "test.sm", {}, {}});
    auto worker_result = wait_for(worker, 1);
    require(!worker_result.graph && worker_result.source_name == "test.sm" &&
                !worker_result.source_diagnostics.empty() &&
                worker_result.source_diagnostics[0].location.line == 2 &&
                !worker_result.diagnostics.empty(),
            "Worker did not propagate structured source errors and logs");

    const smc::Diagnostics diagnostics{
        {Severity::error, {"test.sm", 2, 3}, "first error"},
        {Severity::error, {"test.sm", 2, 8}, "second error"},
        {Severity::warning, {"test.sm", 1, 1}, "warning"},
        {Severity::error, {"other.sm", 1, 1}, "other file"},
        {Severity::error, {"test.sm", 0, 1}, "invalid line"},
        {Severity::error, {"test.sm", 4, 1}, "past end"},
        {Severity::error, {"test.sm", 3, 1}, "last line"}};
    const auto markers = source_error_markers(diagnostics, "test.sm", 3);
    require(markers.size() == 2 && markers[0].line == 1 &&
                markers[0].tooltip == "Column 3: first error\nColumn 8: second error" &&
                markers[1].line == 2 && markers[1].tooltip == "Column 1: last line",
            "Source errors were not filtered, grouped and mapped to editor lines");

    TextEditor editor;
    editor.SetText("\n%unknown\n");
    set_source_error_markers(editor, diagnostics, "test.sm");
    require(editor.HasMarkers(), "Valid source errors did not create editor markers");
    set_source_error_markers(editor, {}, "test.sm");
    require(!editor.HasMarkers(), "Empty diagnostics did not clear editor markers");

    using namespace std::chrono_literals;
    const auto start = AnalysisCoordinator::Clock::time_point{};
    AnalysisCoordinator coordinator(true, start);
    CanvasState canvas;
    DiagnosticLog log(DiagnosticLog::default_capacity, false);
    auto first = coordinator.next_job(start, editor.GetText(), "test.sm", canvas);
    require(first && first->source, "Source analysis was not scheduled");
    worker_result.revision = first->revision;
    coordinator.accept(std::move(worker_result), canvas, log, &editor);
    require(editor.HasMarkers(), "Accepted source errors did not mark the editor");

    auto relayout = coordinator.next_job(start, editor.GetText(), "test.sm", canvas);
    require(relayout && !relayout->source, "Layout-only job was not scheduled");
    AnalysisResult layout_result{relayout->revision, false};
    layout_result.graph = canvas.graph;
    layout_result.layout = dot_layout::LayoutResult{};
    coordinator.accept(std::move(layout_result), canvas, log, &editor);
    require(editor.HasMarkers(), "Layout-only result cleared source error markers");

    coordinator.text_changed(start, &editor);
    require(!editor.HasMarkers(), "Editing did not immediately clear error markers");
    auto pending = coordinator.next_job(start + 500ms, editor.GetText(), "test.sm", canvas);
    require(pending && pending->source, "Changed source was not scheduled");
    coordinator.text_changed(start + 600ms, &editor);
    AnalysisResult stale{pending->revision, true};
    stale.source_name = "test.sm";
    stale.source_diagnostics = diagnostics;
    coordinator.accept(std::move(stale), canvas, log, &editor);
    require(!editor.HasMarkers(), "Stale analysis restored error markers");
    auto latest = coordinator.next_job(start + 1100ms, editor.GetText(), "test.sm", canvas);
    require(latest && latest->source, "Latest source was not scheduled");
    AnalysisResult success{latest->revision, true};
    success.graph = dot_layout::DirectedGraph{};
    success.layout = dot_layout::LayoutResult{};
    set_source_error_markers(editor, diagnostics, "test.sm");
    coordinator.accept(std::move(success), canvas, log, &editor);
    require(!editor.HasMarkers(), "Successful source analysis did not clear error markers");
}

void test_error_marker_rendering() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(900, 600);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        static_cast<const void*>(&dejavu), dejavuSize, 15.0f);
    TextEditor editor;
    editor.SetText("first\nsecond\nthird");
    set_source_error_markers(editor,
        {{smc::DiagnosticSeverity::error, {"test.sm", 2, 1}, "error"}}, "test.sm");
    bool line_number = false;
    bool text = false;
    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Always);
        ImGui::Begin("Marker test");
        editor.Render("Source", ImVec2(0, 0));
        ImGui::End();
        ImGui::Render();
        for (const auto* draw_list : ImGui::GetDrawData()->CmdLists) {
            for (const auto& vertex : draw_list->VtxBuffer) {
                line_number |= vertex.col == IM_COL32(200, 45, 45, 160);
                text |= vertex.col == IM_COL32(200, 45, 45, 55);
            }
        }
    }
    ImGui::DestroyContext();
    require(line_number && text, "Error marker backgrounds were not rendered");
}

void test_coordinator() {
    using namespace std::chrono_literals;
    const auto start = AnalysisCoordinator::Clock::time_point{};
    AnalysisCoordinator coordinator(false, start);
    CanvasState canvas;
    DiagnosticLog log;
    auto initial = coordinator.next_job(start, "", "test.sm", canvas);
    require(initial && !initial->source, "Initial fallback layout was not scheduled");
    require(!coordinator.next_deadline(), "Fallback job created an analysis deadline");
    AnalysisResult initial_result{initial->revision, false};
    initial_result.graph = canvas.graph;
    initial_result.layout = dot_layout::LayoutResult{};
    coordinator.accept(std::move(initial_result), canvas, log);
    coordinator.text_changed(start);
    require(coordinator.next_deadline() == start + 500ms,
            "Debounce deadline was not exposed to the event loop");
    require(!coordinator.next_job(start + 499ms, "a", "test.sm", canvas),
            "Analysis ran before debounce elapsed");
    auto first = coordinator.next_job(start + 500ms, "a", "test.sm", canvas);
    require(first && first->source && *first->source == "a",
            "Analysis did not run at debounce deadline");
    require(!coordinator.next_deadline(), "Active analysis retained its deadline");
    coordinator.text_changed(start + 600ms);
    require(coordinator.next_deadline() == start + 1100ms,
            "A later edit did not update the wake deadline");
    AnalysisResult stale{first->revision, true};
    stale.diagnostics.push_back({std::chrono::system_clock::now(), LogLevel::error,
                                 "SMC", "stale diagnostic"});
    stale.graph = dot_layout::DirectedGraph{};
    stale.layout = dot_layout::LayoutResult{};
    stale.source_ranges.push_back({1, 2, GraphObjectKind::state, {0}});
    stale.ast.roots.push_back(0);
    std::ostringstream stale_output;
    auto* previous_stderr = std::cerr.rdbuf(stale_output.rdbuf());
    coordinator.accept(std::move(stale), canvas, log);
    std::cerr.rdbuf(previous_stderr);
    require(log.snapshot().records.empty() && !coordinator.source_is_indexed() &&
                coordinator.source_ranges().empty() && coordinator.ast().roots.empty() &&
                !canvas.graph.empty(),
            "Stale analysis replaced the current graph or logged diagnostics");
    require(stale_output.str().empty(), "Stale diagnostic reached standard error");
    require(!coordinator.next_job(start + 1099ms, "b", "test.sm", canvas),
            "A later edit did not reset the debounce deadline");
    auto second = coordinator.next_job(start + 1100ms, "b", "test.sm", canvas);
    require(second && second->source && *second->source == "b",
            "Latest edit was not scheduled");
    canvas.dirty = true;
    coordinator.settings_changed();
    auto newest = coordinator.next_job(start + 1100ms, "b", "test.sm", canvas);
    require(newest && newest->revision != second->revision && newest->source,
            "Settings change did not invalidate running source analysis");
    AnalysisResult outdated{second->revision, true};
    outdated.graph = dot_layout::DirectedGraph{};
    outdated.layout = dot_layout::LayoutResult{};
    coordinator.accept(std::move(outdated), canvas, log);
    require(!coordinator.source_is_indexed(), "Outdated settings result was applied");
    AnalysisResult success{newest->revision, true};
    success.graph = dot_layout::DirectedGraph{{{"new", "New"}}, {}, {}};
    success.layout = dot_layout::LayoutResult{};
    success.source_ranges.push_back({1, 3, GraphObjectKind::state, {0}});
    success.ast.nodes.push_back({"state", {1, 3, GraphObjectKind::state, {0}}, {}});
    success.ast.roots.push_back(0);
    coordinator.accept(std::move(success), canvas, log);
    require(!coordinator.next_deadline(), "Completed analysis retained its deadline");
    require(coordinator.source_is_indexed() && canvas.graph.nodes[0].id == "new" &&
                coordinator.source_ranges().size() == 1 &&
                coordinator.source_ranges()[0].begin == 1 &&
                coordinator.ast().roots.size() == 1,
            "Latest valid analysis was not applied");
    coordinator.text_changed(start + 1200ms);
    auto invalid_job = coordinator.next_job(start + 1700ms, "bad", "test.sm", canvas);
    require(invalid_job.has_value(), "Invalid input was not scheduled");
    AnalysisResult invalid{invalid_job->revision, true};
    invalid.diagnostics.push_back({std::chrono::system_clock::now(), LogLevel::error,
                                   "SMC", "bad input"});
    coordinator.accept(std::move(invalid), canvas, log);
    require(!coordinator.source_is_indexed() && canvas.graph.nodes[0].id == "new" &&
                canvas.dirty && log.snapshot().records.back().detail.find("retaining") != std::string::npos,
            "Invalid input did not retain the last valid graph");
}

void test_layout_cache_and_culling() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(900, 600);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        static_cast<const void*>(&dejavu), dejavuSize, 15.0f);
    auto& app = test_app();
    app.canvas = CanvasState{};
    app.canvas.asynchronous = false;
    app.settings = UserSettings{};
    const dot_layout::DirectedGraph graph{{{"single", "Single"}}, {}, {}};
    const auto frame = [&app](
                           std::optional<dot_layout::DirectedGraph> input = std::nullopt) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Always);
        if (input) {
            app.canvas.graph = std::move(*input);
            app.canvas.layout.reset();
            app.canvas.dirty = true;
            app.canvas.recently_replaced = true;
        }
        render_graph_window(app);
        ImGui::Render();
        return ImGui::FindWindowByName("canvas")->DrawList->VtxBuffer.Size;
    };
    frame(graph);
    const auto visible_vertices = frame();
    auto& canvas = app.canvas;
    require(canvas.layout && canvas.layout->nodes.size() == 1, "Missing cached layout");
    canvas.layout->nodes[0].center.x += 100000.0;
    const auto moved_x = canvas.layout->nodes[0].center.x;
    const auto hidden_vertices = frame();
    require(canvas.layout->nodes[0].center.x == moved_x,
            "Stable frame recomputed the layout");
    require(hidden_vertices < visible_vertices, "Offscreen node was not culled");
    frame(graph);
    require(canvas.layout->nodes[0].center.x != moved_x,
            "Graph replacement did not invalidate the layout");
    const dot_layout::DirectedGraph edge_graph{
        {{"a", "A"}, {"b", "B"}}, {{"a", "b", ""}}, {}};
    frame(edge_graph);
    auto& layout = *canvas.layout;
    layout.bounds = {0, 0, 100, 100};
    for (auto& node : layout.nodes)
        node.center = {10000, 10000};
    auto& edge = layout.edges[0];
    edge.label.reset();
    edge.spline_points = {{{-1000, 80}, {-500, 80}, {500, 80}, {1000, 80}}};
    edge.arrow_tips = {std::nullopt};
    const auto crossing_vertices = frame();
    for (auto& point : edge.spline_points[0])
        point.y = 10000;
    const auto offscreen_vertices = frame();
    require(crossing_vertices > offscreen_vertices,
            "Curve crossing the viewport was incorrectly culled");

    app.log.clear();
    for (int i = 0; i < 999; ++i)
        app.log.write(LogLevel::info, "Test", "diagnostic\nsecond line");
    app.log.write(LogLevel::warning, "Test", std::string(500, 'x'));
    ImGuiWindow* child = nullptr;
    for (int i = 0; i < 2; ++i) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Always);
        render_log_window(app);
        ImGui::Render();
        for (auto* window : ImGui::GetCurrentContext()->Windows)
            if (std::string_view(window->Name).find("DiagnosticLog") != std::string_view::npos)
                child = window;
    }
    require(child && child->DrawList->VtxBuffer.Size < 10000,
            "Diagnostic list rendered all offscreen lines");
    require(child->ScrollMax.x > 0 && app.log.snapshot().records.size() == 1000,
            "Virtualized diagnostics lost horizontal scrolling or history");

    const auto log_frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Always);
        render_log_window(app);
        ImGui::Render();
    };
    app.log.clear();
    app.log.write(LogLevel::error, "Parser", "first\nsecond");
    app.log.write(LogLevel::warning, "File", "file warning");
    app.log.write(LogLevel::info, "Parser", "info");
    log_frame();
    require(app.diagnostic_view.lines.size() == 4 &&
                app.diagnostic_view.lines[0].text.find("[Error] [Parser] first") != std::string::npos &&
                app.diagnostic_view.lines[1].text == "second" &&
                app.diagnostic_view.lines[0].level == LogLevel::error,
            "Structured or multiline log rendering is incorrect");
    app.diagnostic_view.levels[static_cast<int>(LogLevel::info)] = false;
    std::copy_n("Parser", 7, app.diagnostic_view.source_filter);
    log_frame();
    require(app.diagnostic_view.lines.size() == 2 &&
                app.diagnostic_view.lines[0].text.find("first") != std::string::npos,
            "Combined level and source filtering failed");
    app.log.clear();
    log_frame();
    require(app.diagnostic_view.lines.empty(), "Clear retained cached lines");
    std::ostringstream log_output;
    auto* previous_stderr = std::cerr.rdbuf(log_output.rdbuf());
    for (std::size_t i = 0; i < DiagnosticLog::default_capacity; ++i)
        app.log.write(LogLevel::error, "Parser", std::to_string(i));
    log_frame();
    app.log.write(LogLevel::error, "Parser", "newest");
    std::cerr.rdbuf(previous_stderr);
    log_frame();
    require(app.diagnostic_view.lines.size() == DiagnosticLog::default_capacity &&
                app.diagnostic_view.lines.front().text.ends_with(" 1") &&
                app.diagnostic_view.lines.back().text.ends_with(" newest"),
            "Capacity eviction did not invalidate the visible log cache");
    app.log.clear();
    app.diagnostic_view.source_filter[0] = '\0';
    app.diagnostic_view.levels[static_cast<int>(LogLevel::info)] = true;
    app.log.write(LogLevel::debug, "Test", "debug");
    app.log.write(LogLevel::info, "Test", "info");
    app.log.write(LogLevel::warning, "Test", "warning");
    app.log.write(LogLevel::error, "Test", "error");
    log_frame();
    log_frame();
    for (std::size_t level = 0; level < 4; ++level) {
        app.diagnostic_view.levels[level] = false;
        log_frame();
        require(app.diagnostic_view.lines.size() == 3,
                "A log level filter did not hide its records");
        app.diagnostic_view.levels[level] = true;
    }
    log_frame();
    const ImU32 colors[] = {
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.65f, 0.65f, 0.65f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.70f, 0.85f, 1.0f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.78f, 0.30f, 1.0f)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.38f, 0.38f, 1.0f))};
    for (const auto color : colors) {
        bool found = false;
        for (const auto& vertex : child->DrawList->VtxBuffer)
            found |= vertex.col == color;
        require(found, "Log level color was not rendered");
    }
    ImGui::DestroyContext();
}

void benchmark(const char* path) {
    const auto contents = read_input_file(path);
    require(contents.has_value(), "Could not read benchmark fixture");
    const auto analysis_start = std::clock();
    const auto graph = make_graph_from_contents(*contents, path);
    const double analysis_ms = 1000.0 * (std::clock() - analysis_start) / CLOCKS_PER_SEC;
    require(graph.has_value(), "Benchmark fixture is invalid");

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(900, 600);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        static_cast<const void*>(&dejavu), dejavuSize, 15.0f);
    auto& app = test_app();
    app.canvas = CanvasState{};
    app.canvas.asynchronous = false;
    app.settings = UserSettings{};
    const auto frame = [&app](
                           std::optional<dot_layout::DirectedGraph> input = std::nullopt) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Always);
        if (input) {
            app.canvas.graph = std::move(*input);
            app.canvas.layout.reset();
            app.canvas.dirty = true;
            app.canvas.recently_replaced = true;
        }
        render_graph_window(app);
        ImGui::Render();
    };
    frame(*graph);
    constexpr int frames = 5;
    auto start = std::clock();
    for (int i = 0; i < frames; ++i)
        frame();
    const double cached_ms = 1000.0 * (std::clock() - start) / CLOCKS_PER_SEC / frames;
    start = std::clock();
    for (int i = 0; i < frames; ++i) {
        app.canvas.dirty = true;
        frame();
    }
    const double relayout_ms = 1000.0 * (std::clock() - start) / CLOCKS_PER_SEC / frames;
    std::cout << path << ": bytes=" << contents->size()
              << " nodes=" << graph->nodes.size() << " edges=" << graph->edges.size()
              << " analysis_cpu_ms=" << analysis_ms
              << " cached_frame_cpu_ms=" << cached_ms
              << " forced_relayout_frame_cpu_ms=" << relayout_ms << '\n';
    ImGui::DestroyContext();
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--benchmark") {
            benchmark(argv[2]);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--error-markers") {
            test_error_markers();
            test_error_marker_rendering();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--semantic") {
            test_semantic_highlighting();
            test_semantic_rendering();
            return 0;
        }
        test_cached_offsets();
        test_semantic_highlighting();
        test_semantic_rendering();
        test_error_markers();
        test_error_marker_rendering();
        test_coordinator();
        test_worker();
        test_worker_notification();
        test_layout_cache_and_culling();
    } catch (const std::exception& error) {
        std::cerr << "performance_pipeline_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
