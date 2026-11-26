#include <chrono>
#include <memory>
#include <utility>

#include <TextEditor.h>
#include <TrieAutoComplete.h>

#include <smc_text_editor_language.hpp>

#include <analysis.hpp>
#include <app_context.hpp>
#include <graph_editor.hpp>

AppContext::AppContext() = default;

AppContext::~AppContext() = default;

void AppContext::initialize(std::string source_file, std::function<void()> result_ready) {
    editor = std::make_unique<TextEditor>();
    autocomplete = std::make_unique<TrieAutoComplete>();
    autocomplete->Connect(editor.get());
    editor->SetLanguage(smc_editor::language());
    settings.initialize(&log);
    settings.apply_editor_options(*editor);
    const auto recent_file = settings.recent_files.empty() ? "" : settings.recent_files.front();
    input_file = source_file.empty() ? recent_file : std::move(source_file);
    const auto contents = read_input_file(input_file, &log);
    if (contents) {
        editor->SetText(*contents);
        if (settings.record_recent_file(input_file))
            settings.sync();
    } else {
        log.write(LogLevel::warning, "Application",
                  "Displaying the built-in example graph until valid SMC is available.");
    }

    editor_text = editor->GetText();
    saved_text = editor_text;
    line_offsets = source_line_offsets(editor_text);
    canvas.asynchronous = true;
    canvas.options.rank_direction = settings.rank_direction;
    canvas.options.rank_separation = settings.rank_separation;
    canvas.options.node_separation = settings.node_separation;
    canvas.options.compound = settings.compound;
    coordinator = std::make_unique<AnalysisCoordinator>(
        contents.has_value(), AnalysisCoordinator::Clock::now());
    worker = std::make_unique<AnalysisWorker>(std::move(result_ready));
    graph_editor = std::make_unique<GraphEditor>(*this);
}

bool AppContext::document_modified() const {
    return editor && editor->GetText() != saved_text;
}

std::string AppContext::document_title() const {
    auto title = std::filesystem::path(input_file).filename().string();
    if (title.empty())
        title = "SMC Editor";
    if (document_modified())
        title += " *";
    return title;
}
