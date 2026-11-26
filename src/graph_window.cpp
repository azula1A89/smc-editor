#include <app_context.hpp>
#include <graph_editor.hpp>
#include <ui_windows.hpp>

void render_graph_window(AppContext& app) {
    if (!app.graph_editor)
        app.graph_editor = std::make_unique<GraphEditor>(app);
    app.graph_editor->render();
}
