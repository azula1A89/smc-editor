#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

#include <user_settings.hpp>

namespace {

bool parse_integer(std::string_view value, int& result) {
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

bool parse_unsigned(std::string_view value, std::uint64_t& result) {
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), result, 16);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

bool parse_float(std::string_view value, float& result) {
    const std::string text(value);
    char* end = nullptr;
    result = std::strtof(text.c_str(), &end);
    return end != text.c_str() && end == text.c_str() + text.size() &&
           std::isfinite(result);
}

std::vector<int> parse_integer_list(std::string_view value, bool& valid) {
    std::vector<int> result;
    valid = true;
    while (!value.empty()) {
        const auto separator = value.find(',');
        const auto item = value.substr(0, separator);
        int parsed = 0;
        if (item.empty() || !parse_integer(item, parsed)) {
            valid = false;
            return {};
        }
        result.push_back(parsed);
        if (separator == std::string_view::npos)
            break;
        value.remove_prefix(separator + 1);
    }
    return result;
}

std::uint64_t fingerprint(const dot_layout::DirectedGraph& graph) {
    std::uint64_t value = 14695981039346656037ull;
    const auto add_bytes = [&](const char* data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            value ^= static_cast<unsigned char>(data[i]);
            value *= 1099511628211ull;
        }
    };
    const auto add_string = [&](const std::string& text) {
        const auto add_integer = [&](std::uint64_t integer) {
            for (unsigned int shift = 0; shift < 64; shift += 8) {
                const char byte = static_cast<char>((integer >> shift) & 0xffu);
                add_bytes(&byte, 1);
            }
        };
        add_integer(text.size());
        add_bytes(text.data(), text.size());
    };
    const auto add_count = [&](std::size_t count) {
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            const char byte = static_cast<char>(
                (static_cast<std::uint64_t>(count) >> shift) & 0xffu);
            add_bytes(&byte, 1);
        }
    };

    add_count(graph.nodes.size());
    for (const auto& node : graph.nodes) {
        add_string(node.id);
        add_string(node.label);
    }
    add_count(graph.edges.size());
    for (const auto& edge : graph.edges) {
        add_string(edge.from);
        add_string(edge.to);
        add_string(edge.label);
    }
    add_count(graph.clusters.size());
    for (const auto& cluster : graph.clusters) {
        add_string(cluster.id);
        add_string(cluster.label);
        add_string(cluster.parent.value_or(""));
        add_count(cluster.nodes.size());
        for (const auto& node : cluster.nodes)
            add_string(node);
    }
    return value;
}

void report_invalid(const std::string& key) {
    std::cerr << "Ignoring invalid UserSettings entry '" << key << "'\n";
}

} // namespace

void* UserSettings::read_open_func(
    ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) {
    return std::string_view(name) == "Data" ? handler->UserData : nullptr;
}

void UserSettings::read_line_func(
    ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
    auto& settings = *static_cast<UserSettings*>(entry);
    const std::string_view text(line);
    const auto separator = text.find('=');
    if (separator == std::string_view::npos)
        return;

    const auto key = text.substr(0, separator);
    const auto value = text.substr(separator + 1);
    int integer = 0;
    float floating = 0.0f;
    bool valid = false;

    if (key == "window_width" || key == "window_height") {
        valid = parse_integer(value, integer) && integer >= 320 && integer <= 10000;
        if (valid && key == "window_width")
            settings.window_width = integer;
        else if (valid)
            settings.window_height = integer;
    } else if (key == "canvas_scale" || key == "rank_separation" ||
               key == "node_separation" || key == "arrow_size" ||
               key == "ratio_value" || key == "size_width" || key == "size_height") {
        valid = parse_float(value, floating);
        if (key == "canvas_scale")
            valid = valid && floating >= 0.2f && floating <= 3.0f;
        else if (key == "arrow_size")
            valid = valid && floating >= 0.0f && floating <= 100.0f;
        else
            valid = valid && floating > 0.0f && floating <= 100.0f;
        if (valid && key == "canvas_scale")
            settings.canvas_scale = floating;
        else if (valid && key == "rank_separation")
            settings.rank_separation = floating;
        else if (valid && key == "node_separation")
            settings.node_separation = floating;
        else if (valid && key == "arrow_size")
            settings.arrow_size = floating;
        else if (valid && key == "ratio_value")
            settings.ratio_value = floating;
        else if (valid && key == "size_width")
            settings.size_width = floating;
        else if (valid && key == "size_height")
            settings.size_height = floating;
    } else if (key == "rank_direction") {
        valid = parse_integer(value, integer) &&
                integer >= static_cast<int>(dot_layout::RankDirection::TopToBottom) &&
                integer <= static_cast<int>(dot_layout::RankDirection::RightToLeft);
        if (valid)
            settings.rank_direction = static_cast<dot_layout::RankDirection>(integer);
    } else if (key == "compound") {
        valid = value == "0" || value == "1";
        if (valid)
            settings.compound = value == "1";
    } else if (key == "ratio_selection") {
        valid = parse_integer(value, integer) &&
                integer >= static_cast<int>(RatioSelection::Default) &&
                integer <= static_cast<int>(RatioSelection::Numeric);
        if (valid)
            settings.ratio_selection = static_cast<RatioSelection>(integer);
    } else if (key == "graph_fingerprint") {
        valid = parse_unsigned(value, settings.graph_preferences.fingerprint);
    } else if (key == "node_shapes" || key == "edge_weights") {
        auto values = parse_integer_list(value, valid);
        if (valid) {
            for (const int item : values) {
                if (key == "node_shapes")
                    valid = item >= 0 &&
                            item <= static_cast<int>(dot_layout::NodeShape::Point);
                else
                    valid = item >= 1 && item <= 1000;
                if (!valid)
                    break;
            }
        }
        if (valid && key == "node_shapes")
            settings.graph_preferences.node_shapes = std::move(values);
        else if (valid)
            settings.graph_preferences.edge_weights = std::move(values);
    } else {
        return;
    }

    if (!valid)
        report_invalid(std::string(key));
}

void UserSettings::write_all_func(
    ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer) {
    const auto& settings = *static_cast<const UserSettings*>(handler->UserData);
    buffer->appendf("[%s][Data]\n", handler->TypeName);
    buffer->appendf("window_width=%d\n", settings.window_width);
    buffer->appendf("window_height=%d\n", settings.window_height);
    buffer->appendf("canvas_scale=%.6g\n", settings.canvas_scale);
    buffer->appendf("rank_separation=%.6g\n", settings.rank_separation);
    buffer->appendf("node_separation=%.6g\n", settings.node_separation);
    buffer->appendf("arrow_size=%.6g\n", settings.arrow_size);
    buffer->appendf("rank_direction=%d\n", static_cast<int>(settings.rank_direction));
    buffer->appendf("compound=%d\n", settings.compound ? 1 : 0);
    buffer->appendf("ratio_selection=%d\n", static_cast<int>(settings.ratio_selection));
    buffer->appendf("ratio_value=%.6g\n", settings.ratio_value);
    buffer->appendf("size_width=%.6g\n", settings.size_width);
    buffer->appendf("size_height=%.6g\n", settings.size_height);
    buffer->appendf("graph_fingerprint=%016llx\n",
                    static_cast<unsigned long long>(settings.graph_preferences.fingerprint));
    buffer->append("node_shapes=");
    for (std::size_t i = 0; i < settings.graph_preferences.node_shapes.size(); ++i)
        buffer->appendf("%s%d", i == 0 ? "" : ",", settings.graph_preferences.node_shapes[i]);
    buffer->append("\nedge_weights=");
    for (std::size_t i = 0; i < settings.graph_preferences.edge_weights.size(); ++i)
        buffer->appendf("%s%d", i == 0 ? "" : ",", settings.graph_preferences.edge_weights[i]);
    buffer->append("\n\n");
}

void UserSettings::initialize() {
    ImGuiSettingsHandler handler{};
    handler.TypeName = "UserSettings";
    handler.TypeHash = ImHashStr(handler.TypeName);
    handler.ReadOpenFn = read_open_func;
    handler.ReadLineFn = read_line_func;
    handler.WriteAllFn = write_all_func;
    handler.UserData = this;
    ImGui::AddSettingsHandler(&handler);
    ImGui::LoadIniSettingsFromDisk(ImGui::GetIO().IniFilename);
}

void UserSettings::sync() const {
    if (ImGui::GetCurrentContext())
        ImGui::MarkIniSettingsDirty();
}

void UserSettings::capture_graph(const dot_layout::DirectedGraph& graph) {
    graph_preferences.fingerprint = fingerprint(graph);
    graph_preferences.node_shapes.clear();
    graph_preferences.edge_weights.clear();
    graph_preferences.node_shapes.reserve(graph.nodes.size());
    graph_preferences.edge_weights.reserve(graph.edges.size());
    for (const auto& node : graph.nodes)
        graph_preferences.node_shapes.push_back(static_cast<int>(node.shape));
    for (const auto& edge : graph.edges)
        graph_preferences.edge_weights.push_back(edge.weight);
}

void UserSettings::apply_graph_preferences(dot_layout::DirectedGraph& graph) const {
    ::apply_graph_preferences(graph, graph_preferences);
}

void apply_graph_preferences(
    dot_layout::DirectedGraph& graph, const GraphPreferences& preferences) {
    if (preferences.fingerprint == 0 || fingerprint(graph) != preferences.fingerprint ||
        preferences.node_shapes.size() != graph.nodes.size() ||
        preferences.edge_weights.size() != graph.edges.size())
        return;

    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        const int shape = preferences.node_shapes[i];
        if (shape >= 0 && shape <= static_cast<int>(dot_layout::NodeShape::Point))
            graph.nodes[i].shape = static_cast<dot_layout::NodeShape>(shape);
    }
    for (std::size_t i = 0; i < graph.edges.size(); ++i) {
        const int weight = preferences.edge_weights[i];
        if (weight >= 1 && weight <= 1000)
            graph.edges[i].weight = weight;
    }
}
