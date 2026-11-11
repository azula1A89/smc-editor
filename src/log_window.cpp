#include <imgui.h>
#include <app_context.hpp>
#include <ui_windows.hpp>

#include <algorithm>
#include <string>

namespace {
ImVec4 level_color(LogLevel level) {
    switch (level) {
    case LogLevel::debug: return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
    case LogLevel::info: return ImVec4(0.70f, 0.85f, 1.0f, 1.0f);
    case LogLevel::warning: return ImVec4(1.0f, 0.78f, 0.30f, 1.0f);
    case LogLevel::error: return ImVec4(1.0f, 0.38f, 0.38f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}
}

void render_log_window(AppContext& app) {
    auto& log = app.log;
    auto& view = app.diagnostic_view;
    if (!ImGui::Begin("Status / Diagnostics")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear"))
        log.clear();
    bool filter_changed = false;
    constexpr LogLevel levels[] = {
        LogLevel::debug, LogLevel::info, LogLevel::warning, LogLevel::error};
    for (std::size_t i = 0; i < 4; ++i) {
        ImGui::SameLine();
        filter_changed |= ImGui::Checkbox(log_level_name(levels[i]), &view.levels[i]);
    }
    filter_changed |= ImGui::InputText("Source contains", view.source_filter,
                                       sizeof(view.source_filter));

    bool rebuild = filter_changed || !view.initialized ||
        !std::equal(std::begin(view.levels), std::end(view.levels),
                    std::begin(view.measured_levels)) ||
        view.measured_source_filter != view.source_filter ||
        view.measured_font != ImGui::GetFont() ||
        view.measured_font_size != ImGui::GetFontSize();
    auto snapshot = view.initialized ? log.snapshot_if_changed(view.version)
                                     : std::optional<LogSnapshot>(log.snapshot());
    if (snapshot) {
        view.version = snapshot->version;
        view.records = std::move(snapshot->records);
        view.initialized = true;
        rebuild = true;
    }

    if (rebuild) {
        std::copy(std::begin(view.levels), std::end(view.levels),
                  std::begin(view.measured_levels));
        view.measured_source_filter = view.source_filter;
        view.measured_font = ImGui::GetFont();
        view.measured_font_size = ImGui::GetFontSize();
        view.max_width = 0.0f;
        view.lines.clear();
        for (const auto& record : view.records) {
            const auto level = static_cast<std::size_t>(record.level);
            if (level >= 4 || !view.levels[level] ||
                record.source.find(view.source_filter) == std::string::npos)
                continue;
            const std::string prefix = "[" + format_log_timestamp(record.timestamp) +
                "] [" + log_level_name(record.level) + "] [" + record.source + "] ";
            std::size_t begin = 0;
            for (;;) {
                const auto newline = record.detail.find('\n', begin);
                const auto end = newline == std::string::npos ? record.detail.size() : newline;
                std::string text = (begin == 0 ? prefix : std::string{}) +
                                   record.detail.substr(begin, end - begin);
                view.max_width = std::max(view.max_width, ImGui::CalcTextSize(text.c_str()).x);
                view.lines.push_back({std::move(text), record.level});
                if (newline == std::string::npos)
                    break;
                begin = newline + 1;
            }
        }
    }
    ImGui::Separator();
    ImGui::SetNextWindowContentSize(ImVec2(
        view.max_width + ImGui::GetStyle().WindowPadding.x * 2.0f, 0.0f));
    if (ImGui::BeginChild("DiagnosticLog", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        const bool was_at_bottom =
            ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(view.lines.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& line = view.lines[static_cast<std::size_t>(i)];
                ImGui::PushStyleColor(ImGuiCol_Text, level_color(line.level));
                ImGui::TextUnformatted(line.text.c_str());
                ImGui::PopStyleColor();
            }
        if (was_at_bottom)
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}
