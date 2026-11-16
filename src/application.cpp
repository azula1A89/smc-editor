#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>
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
    static bool rendering = false;
    if (rendering)
        return;
    auto* app = static_cast<AppContext*>(glfwGetWindowUserPointer(window));
    if (!app)
        return;

    struct RenderingGuard {
        bool& flag;
        explicit RenderingGuard(bool& value) : flag(value) { flag = true; }
        ~RenderingGuard() { flag = false; }
    } guard(rendering);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    draw_dockspace();
    render_editor_window(*app);
    auto title = app->document_title();
    // Repeated title updates can themselves wake GLFW's event wait on X11.
    if (title != app->window_title) {
        glfwSetWindowTitle(window, title.c_str());
        app->window_title = std::move(title);
    }

    if (auto result = app->worker->take_result())
        app->coordinator->accept(std::move(*result), app->canvas, app->log, app->editor.get());

    update_graph_focus(*app);

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

bool needs_interactive_frames(const AppContext& app) {
    const ImGuiContext& context = *ImGui::GetCurrentContext();
    if (!context.InputEventsQueue.empty() ||
        (app.editor_visible && app.editor->IsMiddleMouseScrolling()))
        return true;
    for (int i = 0; i < ImGuiMouseButton_COUNT; ++i)
        if (context.IO.MouseDown[i])
            return true;
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
        if (ImGui::IsKeyDown(static_cast<ImGuiKey>(key)))
            return true;
    return false;
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
    const std::string input_file = argc > 1 ? argv[1] : "";
    app.initialize(input_file, [] { glfwPostEmptyEvent(); });
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

    int settling_frames = 0;
    while (!app.close_confirmed) {
        if (glfwWindowShouldClose(window) == GLFW_TRUE) {
            glfwSetWindowShouldClose(window, GLFW_FALSE);
            request_window_close(app);
            if (app.close_confirmed)
                break;
        }
        const auto frame_start = AnalysisCoordinator::Clock::now();
        render_frame(window);
        const auto& context = *ImGui::GetCurrentContext();
        if (!context.InputEventsTrail.empty())
            settling_frames = 2;
        else if (settling_frames > 0)
            --settling_frames;

        const bool interactive = settling_frames > 0 || needs_interactive_frames(app);
        auto wake_at = frame_start + (interactive ? std::chrono::milliseconds(16)
                                                  : std::chrono::milliseconds(100));
        if (const auto deadline = app.coordinator->next_deadline())
            wake_at = std::min(wake_at, *deadline);
        const auto remaining = std::chrono::duration<double>(
            wake_at - AnalysisCoordinator::Clock::now()).count();
        glfwWaitEventsTimeout(std::max(remaining, 0.001));
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
