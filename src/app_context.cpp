#include <chrono>
#include <memory>
#include <utility>

#include <TextEditor.h>
#include <TrieAutoComplete.h>

#include <smc_text_editor_language.hpp>

#include <analysis.hpp>
#include <app_context.hpp>

AppContext::AppContext() = default;

AppContext::~AppContext() = default;

void AppContext::initialize(std::string source_file) {
    settings.initialize();

    input_file = std::move(source_file);
    editor = std::make_unique<TextEditor>();
    autocomplete = std::make_unique<TrieAutoComplete>();
    autocomplete->Connect(editor.get());
    editor->SetLanguage(smc_editor::language());
    editor->SetShowMiniMapEnabled(true);

    const auto contents = read_input_file(input_file, &log);
    if (contents)
        editor->SetText(*contents);
    else
        append_log(&log, "Displaying the built-in example graph until valid SMC is available.");

    editor_text = editor->GetText();
    line_offsets = source_line_offsets(editor_text);
    canvas.asynchronous = true;
    canvas.options.rank_direction = settings.rank_direction;
    canvas.options.rank_separation = settings.rank_separation;
    canvas.options.node_separation = settings.node_separation;
    canvas.options.compound = settings.compound;
    coordinator = std::make_unique<AnalysisCoordinator>(
        contents.has_value(), AnalysisCoordinator::Clock::now());
    worker = std::make_unique<AnalysisWorker>();
}
