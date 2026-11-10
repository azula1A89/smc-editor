#include <imgui.h>
#include <app_context.hpp>
#include <ui_windows.hpp>

#include <string>
#include <vector>

void render_log_window(AppContext& app) {
    auto& log = app.log;
    auto& view = app.diagnostic_view;
    if (!ImGui::Begin("Status / Diagnostics")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear"))
        log.clear();

    ImGui::Separator();
    if (view.measured_log != &log || view.measured_count > log.size() ||
        view.measured_font != ImGui::GetFont() ||
        view.measured_font_size != ImGui::GetFontSize()) {
        view.measured_log = &log;
        view.measured_count = 0;
        view.measured_font = ImGui::GetFont();
        view.measured_font_size = ImGui::GetFontSize();
        view.max_width = 0.0f;
        view.lines.clear();
    }
    for (; view.measured_count < log.size(); ++view.measured_count) {
        const auto& message = log[view.measured_count];
        std::size_t begin = 0;
        for (;;) {
            const auto newline = message.find('\n', begin);
            const auto end = newline == std::string::npos ? message.size() : newline;
            view.lines.push_back({view.measured_count, begin, end});
            view.max_width = std::max(view.max_width, ImGui::CalcTextSize(
                message.data() + begin, message.data() + end).x);
            if (newline == std::string::npos)
                break;
            begin = newline + 1;
        }
    }
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
                const auto& message = log[line.message];
                ImGui::TextUnformatted(message.data() + line.begin, message.data() + line.end);
            }
        if (was_at_bottom)
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}
