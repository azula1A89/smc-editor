#define main dot_layout_demo_main
#include "../src/main.cpp"
#undef main

#include <cstdint>

namespace {

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void frame(float x, float y, std::optional<bool> mouse = std::nullopt,
           std::optional<dot_layout::DirectedGraph> graph = std::nullopt,
           std::optional<GraphFocus> focus = std::nullopt) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    if (mouse)
        io.AddMouseButtonEvent(0, *mouse);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Always);
    render_graph(std::move(graph), nullptr, std::move(focus));
    ImGui::Render();
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
        ImGui::DestroyContext();
    } catch (const std::exception& error) {
        std::cerr << "render_graph_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
