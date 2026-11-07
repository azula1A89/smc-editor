#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <optional>
#include <fstream>
#include <iostream>

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
#include <smc/generator.hpp>
#include <smc/parser.hpp>
#include <smc/syntax_checker.hpp>

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

std::optional<std::vector<dot_layout::DirectedGraph>> make_graph(const std::string& input_file = "SmcParser.sm");
void draw_dockspace();
void render_graph();

int main(int argc, char **argv) {
  const char *glsl_version = "#version 330";
  if (glfwInit() == GL_FALSE) {
    return GL_FALSE;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  GLFWwindow *main_window = glfwCreateWindow(900, 300, "smc", NULL, NULL);
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

  // Main loop
  while (glfwWindowShouldClose(main_window) == GL_FALSE) {
    // Poll for and process events
    glfwPollEvents();

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    draw_dockspace();
    render_graph();

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

void drag_positive_value(const char* label, float& value, float speed) {
    float edited = value;
    if (ImGui::DragFloat(label, &edited, speed, 0.01f, 100.0f,
                         "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        if (std::isfinite(edited) && edited > 0.0f) {
            value = edited;
        } else {
            std::cerr << label << " must be positive and finite\n";
        }
    }
}

void print_diagnostics(const smc::Diagnostics& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        const char* severity = diagnostic.severity == smc::DiagnosticSeverity::error
                                   ? "error"
                                   : "warning";
        std::cerr << diagnostic.location.file << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column << ": " << severity << ": "
                  << diagnostic.message << '\n';
    }
}

std::optional<std::vector<dot_layout::DirectedGraph>> make_graph(const std::string& input_file) {
    std::ifstream input(input_file, std::ios::binary);
    if (!input) {
        std::cerr << "smc_cpp: cannot open input file '" << input_file << "'\n";
        return std::nullopt;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    if (input.bad()) {
        std::cerr << "smc_cpp: failed while reading input file '" << input_file << "'\n";
        return std::nullopt;
    }

    smc::Diagnostics diagnostics;
    smc::Parser parser(contents, input_file, diagnostics);
    const auto fsm = parser.parse();
    if (!fsm) {
        print_diagnostics(diagnostics);
        return std::nullopt;
    }

    smc::SyntaxChecker checker(diagnostics);
    if (!checker.check(*fsm, "c++")) {
        print_diagnostics(diagnostics);
        return std::nullopt;
    }

    // Populate the graph based on the FSM here
    std::vector<dot_layout::DirectedGraph> graphs;
    const auto& maps = fsm->maps;
    for (const auto& map : maps) {}
    return graphs;
}

void render_graph() {
    static dot_layout::DirectedGraph graph{
      {{"a", "A"}, {"b", "B"}, {"c", "C"}},
      {{"a", "b", "", 1, "inner", "right"}, {"b", "c", ""}},
      {{"inner", "Inner", {"a"}, "outer"},
       {"right", "Right", {"b"}, std::nullopt},
       {"outer", "Outer", {"c"}, std::nullopt}}};
    
    ImGui::Begin("canvas", NULL, ImGuiWindowFlags_MenuBar);
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->ChannelsSplit(3);
    ImGuiStyle &style = ImGui::GetStyle();
    static auto origin = ImGui::GetCursorScreenPos();
    static float& scale = style.FontScaleMain;
    static dot_layout::LayoutOptions options = [] {
        dot_layout::LayoutOptions initial;
        initial.compound = true;
        return initial;
    }();
    static float rank_separation = 4.0f;
    static float node_separation = 1.0f;
    static float arrow_size = 10.0f;
    enum class RatioSelection { Default, Fill, Compress, Expand, Numeric };
    static RatioSelection ratio_selection = RatioSelection::Default;
    static float ratio_value = 1.0f;
    static float size_width = 8.0f;
    static float size_height = 6.0f;
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
                    drag_positive_value("Height / width", ratio_value, 0.01f);
                if (ratio_selection == RatioSelection::Fill ||
                    ratio_selection == RatioSelection::Compress ||
                    ratio_selection == RatioSelection::Expand) {
                    drag_positive_value("Width (in)", size_width, 0.05f);
                    drag_positive_value("Height (in)", size_height, 0.05f);
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
                    if (ImGui::BeginCombo(node.id.c_str(), shape_names[static_cast<int>(node.shape)])) {
                        for (std::size_t j = 0; j < 4; ++j) {
                            const bool selected = node.shape == shapes[j];
                            if (ImGui::Selectable(shape_names[j], selected))
                                node.shape = shapes[j];
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
                for (std::size_t i = 0; i < graph.edges.size(); ++i) {
                    auto& edge = graph.edges[i];
                    ImGui::PushID(static_cast<int>(i));
                    const std::string name = edge.from + " -> " + edge.to +
                        " (" + std::to_string(i + 1) + ")";
                    ImGui::SetNextItemWidth(100.0f);
                    ImGui::DragInt(name.c_str(), &edge.weight, 1.0f, 1, 1000,
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

    auto layout_graph = graph;
    if (!options.compound) {
        for (auto& edge : layout_graph.edges) {
            edge.tail_cluster.reset();
            edge.head_cluster.reset();
        }
    }
    const auto layout = dot_layout::DotLayout{}.layout(layout_graph, options);

    auto to_screen = [&](dot_layout::Point point) {
    return ImVec2( origin.x + (point.x - layout.bounds.min_x) * scale, 
        origin.y + (layout.bounds.max_y - point.y) * scale);
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
        for (const auto& cluster : layout.clusters) {
            const ImVec2 top_left = to_screen({cluster.bounds.min_x, cluster.bounds.max_y});
            const ImVec2 bottom_right = to_screen({cluster.bounds.max_x, cluster.bounds.min_y});
            draw_list->AddRect(top_left, bottom_right, line_col);
            if (cluster.label) {
                const ImVec2 center = to_screen(cluster.label->center);
                const ImVec2 text_size = ImGui::CalcTextSize(cluster.label->text.c_str());
                draw_list->AddText(ImVec2(center.x - text_size.x * 0.5f,
                                         center.y - text_size.y * 0.5f),
                                   text_col, cluster.label->text.c_str());
            }
        }

        // Draw nodes
        draw_list->ChannelsSetCurrent(2);
        for (const auto& node : layout.nodes) {
            const float x = origin.x + (node.center.x - layout.bounds.min_x) * scale;
            const float y = origin.y + (layout.bounds.max_y - node.center.y) * scale;
            const float width = node.width * scale;
            const float height = node.height * scale;
            const ImVec2 label_size = ImGui::CalcTextSize(node.id.c_str());
            const ImVec2 label_pos(x - label_size.x * 0.5f, y - label_size.y * 0.5f);
            const ImVec2 tl(x - width * 0.5f, y - height * 0.5f);
            const ImVec2 br(x + width * 0.5f, y + height * 0.5f);
            
            if (node.shape == dot_layout::NodeShape::Box) {
                draw_list->AddRectFilled(tl, br, node_col);
            } else {
                draw_list->AddEllipseFilled(ImVec2(x, y), ImVec2(width * 0.5f, height * 0.5f), node_col);
            }
            if (node.shape != dot_layout::NodeShape::Point)
                draw_list->AddText(label_pos, text_col, node.id.c_str());
        }
        
        // Draw edges
        draw_list->ChannelsSetCurrent(1);
        for (const auto& edge : layout.edges) {
            for (std::size_t i = 0; i < edge.spline_points.size(); ++i) {
                auto spl = spline_to_screen(edge.spline_points[i]);
                std::optional<ImVec2> tip;
                if (edge.arrow_tips[i])
                    tip = to_screen(*edge.arrow_tips[i]);
                draw_spline_with_arrow(draw_list, spl, tip, line_col, 1.0f, arrow_size);
            }
            if (edge.label) {
                const ImVec2 center = to_screen(edge.label->center);
                const ImVec2 half_size(edge.label->width * scale * 0.5f,
                                    edge.label->height * scale * 0.5f);
                draw_list->AddText(
                    ImVec2(center.x - half_size.x, center.y - half_size.y), text_col, edge.label->text.c_str());
            }
        }

        draw_list->ChannelsMerge();
    }

    ImGui::End();
}