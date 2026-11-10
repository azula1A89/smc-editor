#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <analysis.hpp>
#include <dot_layout.hpp>
#include <ui_windows.hpp>

namespace {

class FontSize {
public:
    explicit FontSize(float size) : active_(size > 0.0f) {
        if (active_)
            ImGui::PushFont(nullptr, size);
    }

    ~FontSize() {
        if (active_)
            ImGui::PopFont();
    }

    FontSize(const FontSize&) = delete;
    FontSize& operator=(const FontSize&) = delete;

private:
    bool active_;
};

} // namespace

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

namespace {
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

void render_graph_window(AppContext& app) {
    auto& canvas = app.canvas;
    auto& settings = app.settings;
    auto* log = &app.log;
    const auto& focus = app.focus;
    auto& graph = canvas.graph;
    const bool graph_replaced = canvas.recently_replaced;
    canvas.recently_replaced = false;
    
    ImGui::Begin("canvas", NULL, ImGuiWindowFlags_MenuBar);
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->ChannelsSplit(3);
    ImGuiStyle &style = ImGui::GetStyle();
    if (!canvas.origin_initialized) {
        const ImVec2 initial_origin = ImGui::GetCursorScreenPos();
        canvas.origin_x = initial_origin.x;
        canvas.origin_y = initial_origin.y;
        canvas.origin_initialized = true;
    }
    ImVec2 origin(canvas.origin_x, canvas.origin_y);
    float& scale = settings.canvas_scale;
    style.FontScaleMain = scale;
    auto& options = canvas.options;
    const auto previous_scale = settings.canvas_scale;
    const auto previous_rank_separation = settings.rank_separation;
    const auto previous_node_separation = settings.node_separation;
    const auto previous_arrow_size = settings.arrow_size;
    const auto previous_rank_direction = settings.rank_direction;
    const auto previous_compound = settings.compound;
    const auto previous_ratio_selection = settings.ratio_selection;
    const auto previous_ratio_value = settings.ratio_value;
    const auto previous_size_width = settings.size_width;
    const auto previous_size_height = settings.size_height;
    options.rank_direction = settings.rank_direction;
    options.rank_separation = settings.rank_separation;
    options.node_separation = settings.node_separation;
    options.compound = settings.compound;
    bool graph_changed = false;
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

            ImGui::DragFloat("Rank Separation", &settings.rank_separation, 0.01f, 0.01f, 100.0f);
            ImGui::DragFloat("Node Separation", &settings.node_separation, 0.01f, 0.01f, 100.0f);
            ImGui::DragFloat("Arrow Size", &settings.arrow_size, 0.01f, 0.0f, 100.0f);
            ImGui::Checkbox("Compound", &options.compound);
            if (ImGui::BeginMenu("Ratio")) {
                if (ImGui::RadioButton("Default", settings.ratio_selection == UserSettings::RatioSelection::Default))
                    settings.ratio_selection = UserSettings::RatioSelection::Default;
                if (ImGui::RadioButton("Fill", settings.ratio_selection == UserSettings::RatioSelection::Fill))
                    settings.ratio_selection = UserSettings::RatioSelection::Fill;
                if (ImGui::RadioButton("Compress", settings.ratio_selection == UserSettings::RatioSelection::Compress))
                    settings.ratio_selection = UserSettings::RatioSelection::Compress;
                if (ImGui::RadioButton("Expand", settings.ratio_selection == UserSettings::RatioSelection::Expand))
                    settings.ratio_selection = UserSettings::RatioSelection::Expand;
                if (ImGui::RadioButton("Numeric", settings.ratio_selection == UserSettings::RatioSelection::Numeric))
                    settings.ratio_selection = UserSettings::RatioSelection::Numeric;
                if (settings.ratio_selection == UserSettings::RatioSelection::Numeric)
                    drag_positive_value("Height / width", settings.ratio_value, 0.01f, log);
                if (settings.ratio_selection == UserSettings::RatioSelection::Fill ||
                    settings.ratio_selection == UserSettings::RatioSelection::Compress ||
                    settings.ratio_selection == UserSettings::RatioSelection::Expand) {
                    drag_positive_value("Width (in)", settings.size_width, 0.05f, log);
                    drag_positive_value("Height (in)", settings.size_height, 0.05f, log);
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
    settings.rank_direction = options.rank_direction;
    settings.compound = options.compound;
    options.rank_separation = settings.rank_separation;
    options.node_separation = settings.node_separation;
    options.size.reset();
    switch (settings.ratio_selection) {
    case UserSettings::RatioSelection::Default: options.ratio.reset(); break;
    case UserSettings::RatioSelection::Numeric: options.ratio = static_cast<double>(settings.ratio_value); break;
    case UserSettings::RatioSelection::Fill:
    case UserSettings::RatioSelection::Compress:
    case UserSettings::RatioSelection::Expand:
        options.size = dot_layout::Size{settings.size_width, settings.size_height};
        if (settings.ratio_selection == UserSettings::RatioSelection::Fill)
            options.ratio = dot_layout::RatioMode::Fill;
        else if (settings.ratio_selection == UserSettings::RatioSelection::Compress)
            options.ratio = dot_layout::RatioMode::Compress;
        else
            options.ratio = dot_layout::RatioMode::Expand;
        break;
    }

    if (graph_changed || !same_layout_options(previous_options, options)) {
        canvas.dirty = true;
        ++canvas.settings_revision;
    }
    if (graph_changed)
        settings.capture_graph(graph);
    if (graph_changed || previous_scale != settings.canvas_scale ||
        previous_rank_separation != settings.rank_separation ||
        previous_node_separation != settings.node_separation ||
        previous_arrow_size != settings.arrow_size ||
        previous_rank_direction != settings.rank_direction ||
        previous_compound != settings.compound ||
        previous_ratio_selection != settings.ratio_selection ||
        previous_ratio_value != settings.ratio_value ||
        previous_size_width != settings.size_width ||
        previous_size_height != settings.size_height)
        settings.sync();
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
        canvas.origin_x = origin.x;
        canvas.origin_y = origin.y;
        return;
    }
    const auto& layout = *canvas.layout;

    if (graph_replaced || !same_focus(canvas.previous_focus, focus)) {
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
        canvas.previous_focus = focus;
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
        return visible_rect(min, max, settings.arrow_size + 4.0f);
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
                        ImVec2(width * 0.5f, height * 0.5f), focus_col, 0, 0, 3.0f);
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
                                       highlighted ? 3.0f : 1.0f, settings.arrow_size);
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
    canvas.origin_x = origin.x;
    canvas.origin_y = origin.y;
}
