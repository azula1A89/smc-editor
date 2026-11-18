#include <cstdint>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

#include <dejavu.h>

#include <app_context.hpp>
#include <analysis.hpp>
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

void frame(float x, float y, std::optional<bool> mouse = std::nullopt,
           std::optional<dot_layout::DirectedGraph> graph = std::nullopt,
           std::optional<GraphFocus> focus = std::nullopt, bool synchronize = false) {
    auto& app = test_app();
    if (graph) {
        app.canvas.graph = std::move(*graph);
        app.canvas.layout.reset();
        app.canvas.dirty = true;
        app.canvas.recently_replaced = true;
    }
    if (!synchronize)
        app.focus = std::move(focus);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    if (mouse)
        io.AddMouseButtonEvent(0, *mouse);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Always);
    if (synchronize)
        update_graph_focus(app);
    render_graph_window(app);
    ImGui::Render();
}

void test_canvas_selection() {
    auto& app = test_app();
    const std::string source =
        "%start Main::Ready\n%class Context\n%header context.h\n%map Main\n%%\n"
        "Ready\n{\n    go [x] Done {}\n       [y] Ready {}\n}\nDone\n{\n}\n%%\n";
    app.editor = std::make_unique<TextEditor>();
    app.editor->SetText(source);
    app.editor_text = app.editor->GetText();
    app.line_offsets = source_line_offsets(app.editor_text);
    app.coordinator = std::make_unique<AnalysisCoordinator>(true, AnalysisCoordinator::Clock::now());
    const auto job = app.coordinator->next_job(AnalysisCoordinator::Clock::now(),
                                               app.editor_text, "canvas.sm", app.canvas);
    require(job.has_value(), "Source analysis was not scheduled");
    AnalysisResult result{job->revision, true};
    result.graph = make_graph_from_contents(app.editor_text, "canvas.sm", nullptr,
                                            &result.source_segments, &result.source_ranges);
    require(result.graph.has_value(), "Canvas fixture failed to parse");
    result.layout = dot_layout::DotLayout{}.layout(*result.graph);
    app.coordinator->accept(std::move(result), app.canvas, app.log);
    app.canvas.dirty = false;
    app.canvas.previous_focus.reset();
    app.canvas_focus.reset();
    frame(800, 500, std::nullopt, std::nullopt, std::nullopt, true);

    const auto place_at_center = [&](dot_layout::Point point) {
        const auto& bounds = app.canvas.layout->bounds;
        const auto center = ImGui::FindWindowByName("canvas")->ContentRegionRect.GetCenter();
        const float scale = app.settings.canvas_scale;
        app.canvas.origin_x = center.x - static_cast<float>((point.x - bounds.min_x) * scale);
        app.canvas.origin_y = center.y - static_cast<float>((bounds.max_y - point.y) * scale);
        return center;
    };
    const auto select = [&](ImVec2 point) {
        frame(point.x, point.y, true, std::nullopt, std::nullopt, true);
        frame(point.x, point.y, false, std::nullopt, std::nullopt, true);
    };
    const auto selected_text = [&] {
        return app.editor->GetSectionText(app.editor->GetMainCursorSelection());
    };

    app.settings.canvas_scale = 1.6f;
    auto point = place_at_center(app.canvas.layout->nodes[0].center);
    select(point);
    require(app.focus && app.focus->kind == GraphObjectKind::state &&
                app.focus->graph_indices == std::vector<std::size_t>{0} &&
                selected_text() == "Ready\n{\n    go [x] Done {}\n       [y] Ready {}\n}",
            "Node click did not focus the state and select its complete source");
    const float origin_x = app.canvas.origin_x;
    const float origin_y = app.canvas.origin_y;
    frame(800, 500, std::nullopt, std::nullopt, std::nullopt, true);
    require(app.focus && app.focus->kind == GraphObjectKind::state &&
                app.canvas.origin_x == origin_x && app.canvas.origin_y == origin_y,
            "Click focus or canvas position changed on the following frame");

    app.settings.canvas_scale = 0.7f;
    require(app.canvas.layout->edges[1].label.has_value(), "Edge label missing");
    point = place_at_center(app.canvas.layout->edges[1].label->center);
    select(point);
    require(app.focus && app.focus->kind == GraphObjectKind::guard &&
                app.focus->graph_indices == std::vector<std::size_t>{1} &&
                selected_text() == "go [x] Done {}\n       [y] Ready {}",
            "Edge click did not select its transition and focus only that edge");
    frame(800, 500, std::nullopt, std::nullopt, std::nullopt, true);
    require(app.focus && app.focus->kind == GraphObjectKind::guard &&
                app.focus->graph_indices == std::vector<std::size_t>{1},
            "Programmatic selection replaced the clicked edge focus");

    const auto& spline = app.canvas.layout->edges[0].spline_points[0];
    require(spline.size() >= 4, "Edge curve missing");
    const dot_layout::Point curve_midpoint{
        (spline[0].x + 3 * spline[1].x + 3 * spline[2].x + spline[3].x) / 8,
        (spline[0].y + 3 * spline[1].y + 3 * spline[2].y + spline[3].y) / 8};
    point = place_at_center(curve_midpoint);
    select(point);
    require(app.focus && app.focus->kind == GraphObjectKind::guard &&
                app.focus->graph_indices == std::vector<std::size_t>{0} &&
                selected_text() == "go [x] Done {}\n       [y] Ready {}",
            "Curve click did not focus the first edge or select its transition");

    app.editor->SetCursor(editor_position_at_offset(
        app.editor_text, app.line_offsets, app.editor_text.find("go [x]")));
    update_graph_focus(app);
    require(!app.canvas_focus && app.focus && app.focus->kind == GraphObjectKind::transition &&
                app.focus->graph_indices == std::vector<std::size_t>({0, 1}),
            "Editor cursor did not restore the transition's full edge focus");
    frame(800, 500, std::nullopt, std::nullopt, std::nullopt, true);

    const auto& cluster = app.canvas.layout->clusters[0];
    require(cluster.label.has_value(), "Cluster label missing");
    point = place_at_center(cluster.label->center);
    select(point);
    require(app.focus && app.focus->kind == GraphObjectKind::state_map &&
                selected_text() == app.editor_text.substr(
                    app.editor_text.find("%map Main"),
                    app.editor_text.rfind("%%") + 2 - app.editor_text.find("%map Main")),
            "Cluster label click did not select the complete map");

    const auto previous_selection = selected_text();
    point = place_at_center({cluster.bounds.min_x,
                             (cluster.bounds.min_y + cluster.bounds.max_y) * 0.5});
    select(point);
    require(app.focus && app.focus->kind == GraphObjectKind::state_map &&
                selected_text() == previous_selection,
            "Cluster border click did not select its map");
    point = place_at_center(app.canvas.layout->nodes[2].center);
    select(point);
    require(app.focus && app.focus->kind == GraphObjectKind::state &&
                app.focus->graph_indices == std::vector<std::size_t>{2} &&
                selected_text() == previous_selection,
            "Synthetic default node changed the editor selection");

    app.coordinator->text_changed(AnalysisCoordinator::Clock::now());
    point = place_at_center(app.canvas.layout->nodes[1].center);
    select(point);
    require(app.focus && app.focus->kind == GraphObjectKind::state &&
                app.focus->graph_indices == std::vector<std::size_t>{1} &&
                selected_text() == previous_selection,
            "Stale source mapping changed the selection or prevented focus");
    point = place_at_center(app.canvas.layout->nodes[2].center);
    select(point);
    require(app.focus && app.focus->graph_indices == std::vector<std::size_t>{2} &&
                selected_text() == previous_selection,
            "Unmapped node did not retain the editor selection");
    frame(30, 550, true, std::nullopt, std::nullopt, true);
    frame(30, 550, false, std::nullopt, std::nullopt, true);
    require(app.focus && app.focus->graph_indices == std::vector<std::size_t>{2} &&
                selected_text() == previous_selection,
            "Blank click changed the selection");
    const auto bar = ImGui::FindWindowByName("canvas")->MenuBarRect();
    const ImVec2 settings_button(bar.Min.x + 20.0f, (bar.Min.y + bar.Max.y) * 0.5f);
    select(settings_button);
    require(app.focus && app.focus->graph_indices == std::vector<std::size_t>{2} &&
                selected_text() == previous_selection,
            "Menu click changed the canvas selection");
    select(settings_button);
    point = place_at_center(app.canvas.layout->nodes[0].center);
    const float before_drag = app.canvas.origin_x;
    frame(point.x, point.y, true, std::nullopt, std::nullopt, true);
    frame(point.x + 30.0f, point.y, std::nullopt, std::nullopt, std::nullopt, true);
    frame(point.x + 30.0f, point.y, false, std::nullopt, std::nullopt, true);
    require(app.focus && app.focus->graph_indices == std::vector<std::size_t>{2} &&
                selected_text() == previous_selection && app.canvas.origin_x > before_drag,
            "Dragging selected an element or failed to pan");
    app.editor->SetCursor(TextEditor::DocPos(0, 0));
    update_graph_focus(app);
    require(!app.canvas_focus && !app.focus,
            "Editor cursor movement did not release canvas focus");
}

void test_ast_menu() {
    auto& app = test_app();
    const std::string source =
        "%start Main::Ready\n%class Context\n%header context.h\n%map Main\n%%\n"
        "Ready\n{\n    go [x] Done {}\n       [y] Ready {}\n}\nDone\n{\n}\n%%\n";
    app.editor = std::make_unique<TextEditor>();
    app.editor->SetText(source);
    app.editor_text = app.editor->GetText();
    app.line_offsets = source_line_offsets(app.editor_text);
    const auto now = AnalysisCoordinator::Clock::now();
    app.coordinator = std::make_unique<AnalysisCoordinator>(true, now);
    const auto job = app.coordinator->next_job(now, app.editor_text, "menu.sm", app.canvas);
    require(job.has_value(), "AST menu analysis was not scheduled");
    AnalysisResult result{job->revision, true};
    result.graph = make_graph_from_contents(app.editor_text, "menu.sm", nullptr,
        &result.source_segments, &result.source_ranges, &result.ast);
    require(result.graph.has_value(), "AST menu fixture failed to parse");
    result.layout = dot_layout::DotLayout{}.layout(*result.graph);
    app.coordinator->accept(std::move(result), app.canvas, app.log);
    app.canvas.dirty = false;
    app.canvas_focus.reset();
    app.canvas.previous_focus.reset();

    const auto editor_frame = [&](float x, float y, std::optional<bool> mouse = std::nullopt) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        if (mouse)
            io.AddMouseButtonEvent(0, *mouse);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(440, 600), ImGuiCond_Always);
        render_editor_window(app);
        update_graph_focus(app);
        ImGui::SetNextWindowPos(ImVec2(440, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_Always);
        render_graph_window(app);
        ImGui::Render();
    };
    const auto click_at = [&](ImVec2 p) {
        editor_frame(p.x, p.y, true);
        editor_frame(p.x, p.y, false);
    };
    editor_frame(800, 500);
    auto* editor_window = ImGui::FindWindowByName("SMC Editor");
    const auto bar = editor_window->MenuBarRect();
    const auto ast_id = ImHashStr("ast", 0, editor_window->GetID("##MenuBar"));
    const float bar_y = (bar.Min.y + bar.Max.y) * 0.5f;
    bool found_ast = false;
    for (float x = bar.Min.x + 2.0f; x < bar.Max.x; x += 2.0f) {
        editor_frame(x, bar_y);
        if (ImGui::GetCurrentContext()->HoveredId == ast_id) {
            click_at(ImVec2(x, bar_y));
            found_ast = true;
            break;
        }
    }
    require(found_ast, "AST menu item not found");
    editor_frame(800, 500);
    auto& popup_stack = ImGui::GetCurrentContext()->OpenPopupStack;
    require(!popup_stack.empty() && popup_stack[0].Window, "AST Nodes menu did not open");
    ImGuiWindow* popup = popup_stack[0].Window;

    const auto tree_id = [](ImGuiID seed, std::size_t index) {
        const int id = static_cast<int>(index);
        return ImHashStr("node", 0, ImHashData(&id, sizeof(id), seed));
    };
    const auto& ast = app.coordinator->ast();
    const auto map_index = ast.roots[0];
    const auto state_index = ast.nodes[map_index].children[0];
    const auto transition_index = ast.nodes[state_index].children[0];
    const auto guard_index = ast.nodes[transition_index].children[1];
    const ImGuiID map_id = tree_id(popup->IDStack[0], map_index);
    const ImGuiID state_id = tree_id(map_id, state_index);
    const ImGuiID transition_id = tree_id(state_id, transition_index);
    const ImGuiID guard_id = tree_id(transition_id, guard_index);
    const auto find_tree = [&](ImGuiID id) {
        const float x = popup->Pos.x + popup->Size.x - 12.0f;
        for (float y = popup->Pos.y + 3.0f; y < popup->Pos.y + popup->Size.y - 3.0f; y += 2.0f) {
            editor_frame(x, y);
            if (ImGui::GetCurrentContext()->HoveredId == id)
                return ImVec2(x, y);
        }
        throw std::runtime_error(std::string("AST tree item not found in ") + popup->Name);
    };
    const auto expand_tree = [&](ImGuiID id, int depth) {
        const auto row = find_tree(id);
        const float x = popup->Pos.x + popup->WindowPadding.x +
                        depth * ImGui::GetStyle().IndentSpacing + 5.0f;
        click_at(ImVec2(x, row.y));
        require(popup->StateStorage.GetInt(id) == 1, "AST tree arrow did not expand");
    };
    expand_tree(map_id, 0);
    expand_tree(state_id, 1);
    expand_tree(transition_id, 2);
    const auto check_selection = [&](ImGuiID id, GraphObjectKind kind,
                                     std::vector<std::size_t> indices,
                                     const std::string& text) {
        click_at(find_tree(id));
        require(app.focus && app.focus->kind == kind && app.focus->graph_indices == indices &&
                    app.editor->GetSectionText(app.editor->GetMainCursorSelection()) == text,
                "AST menu click selected the wrong source or graph focus");
        editor_frame(800, 500);
        require(app.focus && app.focus->kind == kind && app.focus->graph_indices == indices,
                "AST menu focus was not preserved on the next frame");
    };
    check_selection(map_id, GraphObjectKind::state_map, {0},
                    source.substr(source.find("%map Main"), source.rfind("%%") + 2 - source.find("%map Main")));
    check_selection(state_id, GraphObjectKind::state, {0},
                    "Ready\n{\n    go [x] Done {}\n       [y] Ready {}\n}");
    check_selection(transition_id, GraphObjectKind::transition, {0, 1},
                    "go [x] Done {}\n       [y] Ready {}");
    check_selection(guard_id, GraphObjectKind::guard, {1}, "[y] Ready {}");

    app.editor->SetCursor(editor_position_at_offset(
        app.editor_text, app.line_offsets, app.editor_text.find("go [x]")));
    editor_frame(800, 500);
    require(!app.canvas_focus && app.focus &&
                app.focus->kind == GraphObjectKind::transition &&
                app.focus->graph_indices == std::vector<std::size_t>({0, 1}),
            "Editor navigation did not restore cursor-driven focus");
    check_selection(guard_id, GraphObjectKind::guard, {1}, "[y] Ready {}");

    const auto selection = app.editor->GetMainCursorSelection();
    app.coordinator->text_changed(AnalysisCoordinator::Clock::now());
    editor_frame(800, 500);
    editor_frame(800, 500);
    click_at(ImVec2(popup->Pos.x + 15.0f, popup->Pos.y + 12.0f));
    require(!app.coordinator->source_is_indexed(), "Stale AST is still indexed");
    require(app.editor->GetMainCursorSelection().start == selection.start &&
                app.editor->GetMainCursorSelection().end == selection.end,
            "Stale AST changed the selection");
}

void click(float x, float y) {
    frame(x, y, true);
    frame(x, y, false);
}

void drag(ImVec2 point, float delta) {
    frame(point.x, point.y);
    frame(point.x, point.y, true);
    frame(point.x + delta, point.y);
    frame(point.x + delta, point.y, false);
}

ImGuiWindow* menuWindow(std::size_t level) {
    auto& stack = ImGui::GetCurrentContext()->OpenPopupStack;
    require(stack.Size > static_cast<int>(level), "Menu did not open");
    ImGuiWindow* menu = stack[static_cast<int>(level)].Window;
    require(menu != nullptr, "Menu window is unavailable");
    return menu;
}

ImVec2 findItem(ImGuiWindow* window, const char* label) {
    const ImGuiID id = window->GetID(label);
    const float x = window->Pos.x + 20.0f;
    for (float y = window->Pos.y + 2.0f; y < window->Pos.y + window->Size.y; y += 2.0f) {
        frame(x, y);
        if (ImGui::GetCurrentContext()->HoveredId == id)
            return ImVec2(x, y);
    }
    throw std::runtime_error(std::string("Menu item not found: ") + label);
}

std::uint64_t canvasGeometry() {
    ImGuiWindow* canvas = ImGui::FindWindowByName("canvas");
    require(canvas && canvas->DrawList->VtxBuffer.Size > 0, "Canvas is empty");
    std::uint64_t hash = 1469598103934665603ull;
    for (const ImDrawVert& vertex : canvas->DrawList->VtxBuffer) {
        const auto x = static_cast<std::int64_t>(vertex.pos.x * 100.0f);
        const auto y = static_cast<std::int64_t>(vertex.pos.y * 100.0f);
        hash = (hash ^ static_cast<std::uint64_t>(x)) * 1099511628211ull;
        hash = (hash ^ static_cast<std::uint64_t>(y)) * 1099511628211ull;
    }
    return hash;
}

bool hasVertexColor(ImU32 color) {
    ImGuiWindow* canvas = ImGui::FindWindowByName("canvas");
    for (const ImDrawVert& vertex : canvas->DrawList->VtxBuffer)
        if (vertex.col == color)
            return true;
    return false;
}

bool hasVertexNear(ImVec2 point, ImU32 color, float tolerance = 3.0f) {
    ImGuiWindow* canvas = ImGui::FindWindowByName("canvas");
    for (const ImDrawVert& vertex : canvas->DrawList->VtxBuffer)
        if (vertex.col == color && std::abs(vertex.pos.x - point.x) <= tolerance &&
            std::abs(vertex.pos.y - point.y) <= tolerance)
            return true;
    return false;
}

void test_outgoing_focus() {
    auto& app = test_app();
    app.settings.canvas_scale = 1.0f;
    app.settings.arrow_size = 0.0f;
    app.settings.focus_color[0] = 1.0f;
    app.settings.focus_color[1] = 0.0f;
    app.settings.focus_color[2] = 0.0f;
    app.settings.focus_thickness = 6.0f;
    app.settings.secondary_focus_thickness = 8.0f;
    app.canvas.graph = {
        {{"a", "A"}, {"b", "B"}, {"c", "C"}},
        {{"a", "b", ""}, {"a", "b", ""}, {"a", "a", ""},
         {"b", "a", ""}, {"c", "b", ""}}};
    dot_layout::LayoutResult layout;
    layout.bounds = {0, 0, 600, 400};
    layout.nodes = {{"a", {500, 300}, 40, 30},
                    {"b", {500, 200}, 40, 30},
                    {"c", {500, 100}, 40, 30}};
    for (std::size_t i = 0; i < app.canvas.graph.edges.size(); ++i) {
        const auto& edge = app.canvas.graph.edges[i];
        const double y = 350.0 - 50.0 * i;
        layout.edges.push_back({edge.from, edge.to, {{{50, y}, {250, y}}},
                                std::nullopt, {std::nullopt}});
    }
    app.canvas.layout = layout;
    app.canvas.dirty = false;
    app.canvas.recently_replaced = false;
    app.canvas.origin_x = 100.0f;
    app.canvas.origin_y = 100.0f;
    app.canvas.origin_initialized = true;

    const auto render = [&](std::optional<GraphFocus> focus) {
        app.canvas.previous_focus = focus;
        frame(800, 500, std::nullopt, std::nullopt, focus);
    };
    const auto edge_width = [&](int index, ImU32 color) {
        const float y = 150.0f + 50.0f * index;
        float min_y = y + 20.0f;
        float max_y = y - 20.0f;
        for (const ImDrawVert& vertex : ImGui::FindWindowByName("canvas")->DrawList->VtxBuffer) {
            if ((vertex.col & 0x00ffffff) == (color & 0x00ffffff) &&
                vertex.pos.x >= 149.0f && vertex.pos.x <= 351.0f &&
                std::abs(vertex.pos.y - y) < 15.0f) {
                min_y = std::min(min_y, vertex.pos.y);
                max_y = std::max(max_y, vertex.pos.y);
            }
        }
        return max_y - min_y;
    };
    const ImU32 line_color = ImGui::GetColorU32(ImGuiCol_PlotLines);
    render(std::nullopt);
    const float normal_width = edge_width(0, line_color);
    require(normal_width > 0.0f, "Normal edge was not rendered");

    render(GraphFocus{GraphObjectKind::state, {0}});
    require(hasVertexColor(IM_COL32(255, 0, 0, 255)),
            "Configured primary focus color was not rendered");
    for (int i = 0; i < 3; ++i)
        require(edge_width(i, line_color) > normal_width + 5.0f,
                "Outgoing, parallel or self-loop edge was not thickened");
    for (int i = 3; i < 5; ++i)
        require(std::abs(edge_width(i, line_color) - normal_width) < 0.1f,
                "Incoming or unrelated edge was incorrectly thickened");

    render(GraphFocus{GraphObjectKind::state, {0, 1, 999}});
    require(edge_width(3, line_color) > normal_width + 5.0f &&
                std::abs(edge_width(4, line_color) - normal_width) < 0.1f,
            "Multiple focused nodes did not highlight their outgoing edges");

    render(GraphFocus{GraphObjectKind::transition, {3}});
    require(edge_width(3, IM_COL32(255, 0, 0, 255)) > normal_width + 3.0f &&
                std::abs(edge_width(0, line_color) - normal_width) < 0.1f,
            "Focused edge did not use the configured primary style");
}

void checkClusterBorders() {
    const dot_layout::DirectedGraph graph{
        {{"a", "A"}, {"b", "B"}, {"c", "C"}},
        {{"a", "b", "a->b", 1, "inner", "right"}, {"b", "c", "b->c"}},
        {{"inner", "Inner", {"a"}, "outer"},
         {"right", "Right", {"b"}, std::nullopt},
         {"outer", "Outer", {"c"}, std::nullopt}}};
    dot_layout::LayoutOptions options;
    options.compound = true;
    options.rank_separation = 4.0;
    options.node_separation = 1.0;
    const auto layout = dot_layout::DotLayout{}.layout(graph, options);
    ImGuiWindow* canvas = ImGui::FindWindowByName("canvas");
    const ImVec2 origin = canvas->DC.CursorStartPos;
    const float scale = ImGui::GetStyle().FontScaleMain;
    for (const auto& cluster : layout.clusters) {
        const float x = origin.x + (cluster.bounds.min_x - layout.bounds.min_x) * scale;
        const float y = origin.y + (layout.bounds.max_y - cluster.bounds.max_y) * scale;
        bool found = false;
        for (const ImDrawVert& vertex : canvas->DrawList->VtxBuffer) {
            if (std::abs(vertex.pos.x - x) <= 2.0f &&
                std::abs(vertex.pos.y - y) <= 2.0f) {
                found = true;
                break;
            }
        }
        require(found, "Cluster border is missing from canvas");
    }
}

} // namespace

int main() {
    try {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(900, 600);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontFromMemoryCompressedTTF(
            static_cast<const void*>(&dejavu), dejavuSize, 15.0f);
        frame(800, 500);
        checkClusterBorders();
        const ImRect bar = ImGui::FindWindowByName("canvas")->MenuBarRect();
        const ImVec2 settings_button(bar.Min.x + 20.0f, (bar.Min.y + bar.Max.y) * 0.5f);
        click(settings_button.x, settings_button.y);
        frame(800, 500);
        const auto original = canvasGeometry();
        ImGuiWindow* settings = menuWindow(0);
        const ImVec2 compound = findItem(settings, "Compound");
        click(compound.x, compound.y);
        frame(800, 500);
        const auto without_compound = canvasGeometry();
        require(original != without_compound, "Compound toggle did not change canvas");

        settings = menuWindow(0);
        const ImVec2 compound_again = findItem(settings, "Compound");
        click(compound_again.x, compound_again.y);
        frame(800, 500);
        require(canvasGeometry() == original, "Compound toggle did not restore canvas");
        settings = menuWindow(0);
        const ImVec2 ratio = findItem(settings, "Ratio");
        click(ratio.x, ratio.y);
        ImGuiWindow* ratio_menu = menuWindow(1);
        const ImVec2 fill = findItem(ratio_menu, "Fill");
        click(fill.x, fill.y);
        frame(800, 500);
        require(canvasGeometry() != original, "Fill ratio did not change canvas");
        ratio_menu = menuWindow(1);
        const ImVec2 numeric = findItem(ratio_menu, "Numeric");
        click(numeric.x, numeric.y);
        frame(800, 500);
        const auto numeric_before = canvasGeometry();
        ratio_menu = menuWindow(1);
        const ImVec2 numeric_value = findItem(ratio_menu, "Height / width");
        drag(numeric_value, 40.0f);
        frame(800, 500);
        require(canvasGeometry() != numeric_before, "Numeric ratio input did not change canvas");
        ratio_menu = menuWindow(1);
        const ImVec2 compress = findItem(ratio_menu, "Compress");
        click(compress.x, compress.y);
        ratio_menu = menuWindow(1);
        const ImVec2 width = findItem(ratio_menu, "Width (in)");
        drag(width, 20.0f);
        ratio_menu = menuWindow(1);
        findItem(ratio_menu, "Height (in)");
        ratio_menu = menuWindow(1);
        const ImVec2 expand = findItem(ratio_menu, "Expand");
        click(expand.x, expand.y);
        ratio_menu = menuWindow(1);
        const ImVec2 default_ratio = findItem(ratio_menu, "Default");
        click(default_ratio.x, default_ratio.y);
        frame(800, 500);
        require(canvasGeometry() == original, "Default ratio did not restore canvas");
        settings = menuWindow(0);
        const ImVec2 focus_style = findItem(settings, "Focus style");
        click(focus_style.x, focus_style.y);
        ImGuiWindow* focus_menu = menuWindow(1);
        const ImVec2 focus_thickness = findItem(focus_menu, "Focus thickness");
        const float old_thickness = test_app().settings.focus_thickness;
        drag(focus_thickness, 40.0f);
        require(test_app().settings.focus_thickness != old_thickness,
                "Focus thickness control did not update settings");
        focus_menu = menuWindow(1);
        const ImVec2 outgoing_thickness = findItem(focus_menu, "Outgoing edge thickness");
        const float old_outgoing_thickness = test_app().settings.secondary_focus_thickness;
        drag(outgoing_thickness, 40.0f);
        require(test_app().settings.secondary_focus_thickness != old_outgoing_thickness,
                "Outgoing edge thickness control did not update settings");
        test_app().settings.focus_thickness = old_thickness;
        test_app().settings.secondary_focus_thickness = old_outgoing_thickness;
        click(settings_button.x, settings_button.y);
        const auto fsm_graph = make_graph(std::string(TEST_SOURCE_DIR) + "/tests/fsm_graph.sm");
        require(fsm_graph.has_value(), "Could not load FSM for rendering");
        frame(800, 500, std::nullopt, fsm_graph);
        const auto fsm_canvas = canvasGeometry();
        frame(800, 500);
        require(canvasGeometry() == fsm_canvas, "FSM graph did not persist across frames");
        const GraphFocus focused_state{GraphObjectKind::state, {0}};
        frame(800, 500, std::nullopt, fsm_graph, focused_state);
        const ImU32 focus_color = IM_COL32(45, 220, 95, 255);
        require(hasVertexColor(focus_color), "Focused graph object is not highlighted");
        ImGuiWindow* canvas = ImGui::FindWindowByName("canvas");
        const ImVec2 content_center = canvas->ContentRegionRect.GetCenter();
        require(hasVertexNear(content_center, focus_color, 20.0f),
                "Focused graph object was not centered in the canvas");
        const GraphFocus focused_map{GraphObjectKind::state_map, {0}};
        frame(800, 500, std::nullopt, fsm_graph, focused_map);
        require(hasVertexColor(focus_color), "Focused state map is not highlighted");
        const GraphFocus focused_guard{GraphObjectKind::guard, {0}};
        frame(800, 500, std::nullopt, fsm_graph, focused_guard);
        require(hasVertexColor(focus_color), "Focused transition guard is not highlighted");
        auto renamed = *fsm_graph;
        renamed.nodes[0].label = "A much longer state label";
        frame(800, 500, std::nullopt, renamed);
        require(canvasGeometry() != fsm_canvas, "State label did not affect rendering");
        test_canvas_selection();
        test_ast_menu();
        test_outgoing_focus();
        ImGui::DestroyContext();
    } catch (const std::exception& error) {
        std::cerr << "render_graph_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
