#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <imgui.h>

#include <user_settings.hpp>
#include <log.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

class ImGuiContextGuard {
public:
    ImGuiContextGuard() : context_(ImGui::CreateContext()) {}
    ~ImGuiContextGuard() {
        ImGui::DestroyContext(context_);
    }

    ImGuiIO& io() const {
        ImGui::SetCurrentContext(context_);
        return ImGui::GetIO();
    }

private:
    ImGuiContext* context_;
};

void testSettingsRoundTrip() {
    const auto path = std::filesystem::temp_directory_path() /
        ("dot_static_user_settings_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".ini");

    dot_layout::DirectedGraph graph{
        {{"node", "Node", dot_layout::NodeShape::Circle}},
        {{"node", "node", "loop", 17}}};

    {
        ImGuiContextGuard context;
        const auto path_string = path.string();
        context.io().IniFilename = path_string.c_str();
        UserSettings settings;
        settings.initialize();
        settings.window_width = 1600;
        settings.window_height = 900;
        settings.canvas_scale = 1.4f;
        settings.rank_direction = dot_layout::RankDirection::TopToBottom;
        settings.rank_separation = 3.5f;
        settings.node_separation = 0.8f;
        settings.arrow_size = 12.0f;
        settings.compound = false;
        settings.ratio_selection = UserSettings::RatioSelection::Numeric;
        settings.ratio_value = 1.25f;
        settings.size_width = 10.5f;
        settings.size_height = 7.25f;
        settings.capture_graph(graph);
        ImGui::SaveIniSettingsToDisk(path_string.c_str());
    }

    {
        ImGuiContextGuard context;
        const auto path_string = path.string();
        context.io().IniFilename = path_string.c_str();
        UserSettings settings;
        settings.initialize();

        require(settings.window_width == 1600 && settings.window_height == 900,
                "Window dimensions were not restored");
        require(settings.canvas_scale == 1.4f &&
                    settings.rank_direction == dot_layout::RankDirection::TopToBottom &&
                    settings.rank_separation == 3.5f &&
                    settings.node_separation == 0.8f &&
                    settings.arrow_size == 12.0f && !settings.compound,
                "Canvas settings were not restored");
        require(settings.ratio_selection == UserSettings::RatioSelection::Numeric &&
                    settings.ratio_value == 1.25f &&
                    settings.size_width == 10.5f && settings.size_height == 7.25f,
                "Ratio settings were not restored");

        dot_layout::DirectedGraph restored{
            {{"node", "Node"}},
            {{"node", "node", "loop"}}};
        settings.apply_graph_preferences(restored);
        require(restored.nodes[0].shape == dot_layout::NodeShape::Circle &&
                    restored.edges[0].weight == 17,
                "Node shape and edge weight were not restored");

        dot_layout::DirectedGraph unrelated{
            {{"node", "Changed graph"}},
            {{"node", "node", "loop"}}};
        settings.apply_graph_preferences(unrelated);
        require(unrelated.nodes[0].shape == dot_layout::NodeShape::Box &&
                    unrelated.edges[0].weight == 1,
                "Preferences from a different graph were applied");
    }

    std::filesystem::remove(path);
}

void testInvalidSettingsAreLogged() {
    const auto path = std::filesystem::temp_directory_path() /
        ("dot_static_invalid_settings_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
    {
        std::ofstream file(path);
        file << "[UserSettings][Data]\nwindow_width=invalid\n";
    }
    {
        ImGuiContextGuard context;
        const auto path_string = path.string();
        context.io().IniFilename = path_string.c_str();
        DiagnosticLog log(10, false);
        UserSettings settings;
        settings.initialize(&log);
        const auto records = log.snapshot().records;
        require(records.size() == 1 && records[0].level == LogLevel::warning &&
                    records[0].source == "Settings" &&
                    records[0].detail.find("window_width") != std::string::npos,
                "Invalid settings were not logged");
    }
    std::filesystem::remove(path);
}

} // namespace

int main() {
    try {
        testSettingsRoundTrip();
        testInvalidSettingsAreLogged();
    } catch (const std::exception& error) {
        std::cerr << "user_settings_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
