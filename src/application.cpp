#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

#include <imgui.h>
#include <imgui_freetype.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <dejavu.h>
#include <notosans.h>

#include <analysis.hpp>
#include <app_context.hpp>
#include <application.hpp>
#include <ui_windows.hpp>

namespace {

void render_frame(GLFWwindow* window) {
    auto* app = static_cast<AppContext*>(glfwGetWindowUserPointer(window));
    if (!app)
        return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    draw_dockspace();
    render_editor_window(*app);

    if (auto result = app->worker->take_result())
        app->coordinator->accept(std::move(*result), app->canvas, app->log);

    app->focus.reset();
    if (app->coordinator->source_is_indexed()) {
        const auto cursor_offset = editor_offset_at_position(
            app->editor_text, app->line_offsets, app->editor->GetMainCursorPosition());
        app->focus = focus_at_offset(app->coordinator->source_segments(), cursor_offset);
    }

    const auto previous_settings_revision = app->canvas.settings_revision;
    render_graph_window(*app);
    if (app->canvas.settings_revision != previous_settings_revision)
        app->coordinator->settings_changed();

    if (auto job = app->coordinator->next_job(
            AnalysisCoordinator::Clock::now(), app->editor_text, app->input_file,
            app->canvas, app->settings))
        app->worker->submit(std::move(*job));

    render_log_window(*app);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

void configure_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig deja_vu_config;
    std::copy_n("DejaVu", 7, deja_vu_config.Name);
    deja_vu_config.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
    deja_vu_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        static_cast<const void*>(&dejavu), dejavuSize, 15.0f, &deja_vu_config);

    ImFontConfig noto_sans_config;
    std::copy_n("NotoSansSC", 11, noto_sans_config.Name);
    noto_sans_config.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
    noto_sans_config.FontDataOwnedByAtlas = false;
    noto_sans_config.MergeMode = true;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        static_cast<const void*>(&notosans), notosansSize, 15.0f, &noto_sans_config);
}

} // namespace

int run_application(int argc, char** argv) {
    if (glfwInit() == GLFW_FALSE)
        return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1280, 800, "smc", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    AppContext app;
    const std::string input_file = argc > 1 ? argv[1] : "mapmanager.sm";
    app.initialize(input_file);
    glfwSetWindowUserPointer(window, &app);
    glfwSetWindowSize(window, app.settings.window_width, app.settings.window_height);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    float xscale = 1.0f;
    float yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    ImGui::GetStyle().FontScaleDpi = std::max(xscale, yscale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    configure_fonts();

    glfwSetWindowRefreshCallback(window, render_frame);
    glfwSetFramebufferSizeCallback(
        window, [](GLFWwindow* resized_window, int width, int height) {
            glViewport(0, 0, width, height);
            render_frame(resized_window);
        });
    glfwSetWindowSizeCallback(
        window, [](GLFWwindow* resized_window, int width, int height) {
            auto* context = static_cast<AppContext*>(
                glfwGetWindowUserPointer(resized_window));
            if (!context || width <= 0 || height <= 0)
                return;
            context->settings.window_width = width;
            context->settings.window_height = height;
            context->settings.sync();
        });

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwPollEvents();
        render_frame(window);
    }

    app.worker.reset();
    if (const char* ini_filename = ImGui::GetIO().IniFilename)
        ImGui::SaveIniSettingsToDisk(ini_filename);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
