#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include <dejavu.h>
#include <FileSelector.h>

#include <analysis.hpp>
#include <app_context.hpp>
#include <cpp_generation.hpp>
#include <ui_windows.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Could not read generated file");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void test_generation_and_write(const std::filesystem::path& directory) {
    const std::string source =
        "%start Main::Ready\n%class Context\n%header context.h\n%map Main\n%%\n"
        "Ready\n{\n    go [x] Done {}\n}\nDone\n{\n}\n%%\n";
    const auto files = generate_cpp_files(source, directory / "machine.sm");
    require(files.header_path == directory / "machine.hpp" &&
                files.source_path == directory / "machine.cpp",
            "Generated file paths were not derived from the input name");
    require(files.header_contents.find("class Context") != std::string::npos,
            "Generated header did not contain the FSM context");
    require(files.source_contents.find("#include \"machine.hpp\"") != std::string::npos,
            "Generated source did not include its generated header");

    write_cpp_files(files);
    require(read_file(files.header_path) == files.header_contents &&
                read_file(files.source_path) == files.source_contents,
            "Generated files were not written correctly");
}

void test_invalid_source(const std::filesystem::path& directory) {
    bool failed = false;
    try {
        (void)generate_cpp_files("%map Main\n", directory / "invalid.sm");
    } catch (const std::runtime_error& error) {
        failed = std::string(error.what()).find("invalid.sm:") != std::string::npos;
    }
    require(failed, "Invalid source did not report its diagnostic location");
    require(!std::filesystem::exists(directory / "invalid.hpp") &&
                !std::filesystem::exists(directory / "invalid.cpp"),
            "Invalid source created output files");
}

void test_todo_generation(const std::filesystem::path& directory) {
    const std::string source =
        "%start Main::Ready\n%class Owner\n%map Main\n%%\n"
        "Ready\nEntry { begin(); }\n"
        "{ go(value: int) [ctxt.enabled() && ctxt.count > value] nil { update(value); } }\n%%\n";
    const auto todo = generate_cpp_todo(source, directory / "incomplete.sm");
    require(todo.find("void begin(); // action") != std::string::npos &&
                todo.find("enabled (guard function reference)") != std::string::npos &&
                todo.find("count (guard member reference)") != std::string::npos &&
                todo.find("update (action): determine declaration") != std::string::npos,
            "TODO generation did not include host references from incomplete source");

    bool files_rejected = false;
    try {
        (void)generate_cpp_files(source, directory / "incomplete.sm");
    } catch (const std::runtime_error&) {
        files_rejected = true;
    }
    require(files_rejected, "C++ file generation accepted an incomplete definition");

    bool parse_rejected = false;
    try {
        (void)generate_cpp_todo("%map Main\n", directory / "invalid.sm");
    } catch (const std::runtime_error& error) {
        parse_rejected = std::string(error.what()).find("invalid.sm:") != std::string::npos;
    }
    require(parse_rejected, "TODO generation did not report parsing diagnostics");
}

void test_editor_menu(const std::filesystem::path& directory) {
    const std::string source =
        "%start Main::Ready\n%class Context\n%header context.h\n%map Main\n%%\n"
        "Ready\n{\n    go [x] Done {}\n}\nDone\n{\n}\n%%\n";
    AppContext app;
    app.editor = std::make_unique<TextEditor>();
    app.coordinator = std::make_unique<AnalysisCoordinator>(false, AnalysisCoordinator::Clock::now());
    app.editor->SetText(source);
    app.editor_text = source;
    app.saved_text = app.editor->GetText();
    app.input_file = (directory / "menu.sm").string();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(900, 700);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        static_cast<const void*>(&dejavu), dejavuSize, 15.0f);

    std::string clipboard = "previous clipboard contents";
    auto& platform = ImGui::GetPlatformIO();
    platform.Platform_ClipboardUserData = &clipboard;
    platform.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
        *static_cast<std::string*>(ImGui::GetPlatformIO().Platform_ClipboardUserData) = text;
    };

    const auto editor_frame = [&](float x, float y, std::optional<bool> mouse = std::nullopt) {
        io.AddMousePosEvent(x, y);
        if (mouse)
            io.AddMouseButtonEvent(0, *mouse);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(440, 380), ImGuiCond_Always);
        render_editor_window(app);
        ImGui::Render();
    };
    const auto find_item = [&](ImGuiWindow* window, ImGuiID id, const char* label) {
        if (window->Flags & ImGuiWindowFlags_MenuBar) {
            const ImRect bar = window->MenuBarRect();
            const float y = (bar.Min.y + bar.Max.y) * 0.5f;
            for (float x = bar.Min.x + 2.0f; x < bar.Max.x; x += 2.0f) {
                editor_frame(x, y);
                if (ImGui::GetCurrentContext()->HoveredId == id)
                    return ImVec2(x, y);
            }
        } else {
            const float x = window->Pos.x + 20.0f;
            for (float y = window->Pos.y + 2.0f; y < window->Pos.y + window->Size.y; y += 2.0f) {
                editor_frame(x, y);
                if (ImGui::GetCurrentContext()->HoveredId == id)
                    return ImVec2(x, y);
            }
        }
        throw std::runtime_error(std::string("C++ generation menu item was not found: ") + label);
    };
    const auto click = [&](ImVec2 point) {
        editor_frame(point.x, point.y, true);
        editor_frame(point.x, point.y, false);
    };
    const auto key = [&](ImGuiKey value) {
        io.AddKeyEvent(value, true);
        editor_frame(850, 650);
        io.AddKeyEvent(value, false);
        editor_frame(850, 650);
    };
    const auto click_button = [&](ImGuiWindow* window, const char* label) {
        const auto id = window->GetID(label);
        for (float y = window->Pos.y + window->Size.y - 60.0f;
             y < window->Pos.y + window->Size.y; y += 4.0f) {
            for (float x = window->Pos.x; x < window->Pos.x + window->Size.x; x += 4.0f) {
                editor_frame(x, y);
                if (ImGui::GetCurrentContext()->HoveredId == id) {
                    click(ImVec2(x, y));
                    return;
                }
            }
        }
        throw std::runtime_error(std::string("Button not found: ") + label);
    };
    const auto selector_main_area = [&]() {
        ImGuiWindow* main_area = nullptr;
        for (auto* window : ImGui::GetCurrentContext()->Windows) {
            const std::string name = window->Name;
            if (window->Active && name.find("ImGuiFileSelector/mainArea_") != std::string::npos &&
                name.find("/listView_") == std::string::npos)
                main_area = window;
        }
        require(main_area != nullptr, "Selector main area was not found");
        return main_area;
    };
    const auto select_entry = [&](const char* label) {
        ImGuiWindow* main_area = selector_main_area();
        ImGuiWindow* list = nullptr;
        for (auto* window : ImGui::GetCurrentContext()->Windows) {
            if (window->Active && std::string(window->Name).find("ImGuiFileSelector/mainArea_") != std::string::npos &&
                std::string(window->Name).find("/listView_") != std::string::npos)
                list = window;
        }
        require(list != nullptr, "Selector file list was not found");
        const auto id = ImHashStr(label, 0, main_area->GetID("listView"));
        click(find_item(list, id, label));
    };
    const auto select_directory = [&] {
        click_button(selector_main_area(), "Select");
    };
    const auto generate_from_menu = [&](const char* item = "generate .hpp and .cpp") {
        editor_frame(550, 350);
        ImGuiWindow* editor_window = ImGui::FindWindowByName("SMC Editor");
        const auto menu_bar_id = editor_window->GetID("##MenuBar");
        const auto file_menu_id = ImHashStr("file", 0, menu_bar_id);
        click(find_item(editor_window, file_menu_id, "file"));
        auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
        require(!popups.empty() && popups[0].Window,
                "File menu did not open");
        ImGuiWindow* file_menu = popups[0].Window;
        click(find_item(file_menu, file_menu->GetID("generate"), "generate"));
        require(popups.size() > 1 && popups[1].Window,
                "Generate submenu did not open");
        ImGuiWindow* generate_menu = popups[1].Window;
        click(find_item(generate_menu, generate_menu->GetID(item), item));
    };

    auto& selector = FileSelector::Instance();
    const std::string unsaved_source =
        "%start Main::Ready\n%class Owner\n%map Main\n%%\n"
        "Ready\nEntry { unsaved_action(); }\n{ go [ctxt.ready()] nil {} }\n%%\n";
    app.editor->SetText(unsaved_source);
    generate_from_menu("generate to-do list");
    require(!selector.IsOpen() &&
                clipboard == generate_cpp_todo(unsaved_source, app.input_file) &&
                clipboard.find("void unsaved_action();") != std::string::npos &&
                app.document_modified(),
            "TODO menu did not copy the current unsaved source");
    require(app.log.snapshot().records.back().level == LogLevel::info,
            "TODO menu did not report its clipboard operation");

    const auto previous_clipboard = clipboard;
    app.editor->SetText("%map Main\n");
    generate_from_menu("generate to-do list");
    auto todo_records = app.log.snapshot().records;
    require(clipboard == previous_clipboard && !selector.IsOpen() &&
                todo_records.back().level == LogLevel::error &&
                todo_records.back().detail.find(app.input_file + ":") != std::string::npos,
            "Invalid TODO source changed the clipboard or failed to report diagnostics");

    app.editor->SetText(unsaved_source);
    const auto set_clipboard = platform.Platform_SetClipboardTextFn;
    platform.Platform_SetClipboardTextFn = nullptr;
    generate_from_menu("generate to-do list");
    todo_records = app.log.snapshot().records;
    require(clipboard == previous_clipboard && todo_records.back().level == LogLevel::error &&
                todo_records.back().detail.find("Clipboard is unavailable") != std::string::npos,
            "Missing clipboard callback was not reported");
    platform.Platform_SetClipboardTextFn = set_clipboard;
    app.editor->SetText(source);

    const auto default_directory = std::filesystem::canonical(directory);
    const auto other_directory = directory / "other";
    require(std::filesystem::create_directory(other_directory), "Could not create output directory");
    generate_from_menu();
    require(selector.IsOpen() && selector.GetCurrentPath() == default_directory,
            "Generation selector did not start in the source directory");
    key(ImGuiKey_Escape);
    require(!selector.IsOpen() && !std::filesystem::exists(directory / "menu.hpp"),
            "Canceling generation created output");

    generate_from_menu();
    require(selector.GetCurrentPath() == default_directory, "Default directory was not restored");
    select_directory();
    const auto header_path = directory / "menu.hpp";
    const auto source_path = directory / "menu.cpp";
    require(std::filesystem::exists(header_path) && std::filesystem::exists(source_path),
            "C++ generation menu did not create both output files");
    require(read_file(source_path).find("#include \"menu.hpp\"") != std::string::npos,
            "C++ generation menu did not use the derived header name");

    {
        std::ofstream header(header_path, std::ios::binary | std::ios::trunc);
        header << "preserve until confirmation";
    }
    generate_from_menu();
    require(selector.GetCurrentPath() == default_directory, "Generation did not reset its directory");
    select_entry("other");
    select_directory();
    require(std::filesystem::exists(other_directory / "menu.hpp") &&
                std::filesystem::exists(other_directory / "menu.cpp") &&
                read_file(other_directory / "menu.cpp").find("#include \"menu.hpp\"") != std::string::npos,
            "Selected output directory was not used");

    generate_from_menu();
    require(selector.GetCurrentPath() == default_directory, "Generation reused the previous output directory");
    select_directory();
    require(app.pending_cpp_generation.has_value() &&
                read_file(header_path) == "preserve until confirmation",
            "Existing C++ output was overwritten without confirmation");
    auto* confirmation = ImGui::FindWindowByName("Overwrite generated C++ files?");
    require(confirmation && confirmation->Active, "Overwrite confirmation was not displayed");
    click_button(confirmation, "Cancel");
    require(!app.pending_cpp_generation && read_file(header_path) == "preserve until confirmation",
            "Canceling overwrite modified an output file");

    generate_from_menu();
    select_directory();
    confirmation = ImGui::FindWindowByName("Overwrite generated C++ files?");
    require(confirmation && confirmation->Active, "Overwrite confirmation did not reopen");
    click_button(confirmation, "Overwrite");
    require(!app.pending_cpp_generation && read_file(header_path).find("class Context") != std::string::npos,
            "Confirmed overwrite did not write output");

    app.editor->SetText("%map Main\n");
    generate_from_menu();
    select_entry("other");
    select_directory();
    const auto invalid_records = app.log.snapshot().records;
    require(!invalid_records.empty() && invalid_records.back().level == LogLevel::error &&
                invalid_records.back().detail.find(app.input_file + ":") != std::string::npos &&
                !app.pending_cpp_generation,
            "Generation diagnostics did not retain the original input path");
    app.editor->SetText(source);

    const auto previous_directory = std::filesystem::current_path();
    std::filesystem::current_path(directory);
    app.input_file = "relative.sm";
    generate_from_menu();
    require(selector.IsOpen() && selector.GetCurrentPath() == std::filesystem::current_path(),
            "Relative source did not default to the current directory");
    ImGui::SetWindowCollapsed("SMC Editor", true);
    editor_frame(850, 650);
    require(selector.IsOpen(), "Collapsing the editor closed the selector");
    key(ImGuiKey_Escape);
    ImGui::SetWindowCollapsed("SMC Editor", false);
    std::filesystem::current_path(previous_directory);

    app.input_file = (directory / "missing" / "menu.sm").string();
    generate_from_menu();
    const auto records = app.log.snapshot().records;
    require(!selector.IsOpen() && !records.empty() &&
                records.back().level == LogLevel::error &&
                records.back().detail.find("Cannot open folder") != std::string::npos,
            "An inaccessible source directory did not produce an error");

    const auto open_path = directory / "opened.sm";
    {
        std::ofstream output(open_path);
        output << source;
    }
    app.input_file = (directory / "menu.sm").string();
    editor_frame(550, 350);
    auto* editor_window = ImGui::FindWindowByName("SMC Editor");
    const auto menu_bar_id = editor_window->GetID("##MenuBar");
    click(find_item(editor_window, ImHashStr("file", 0, menu_bar_id), "file"));
    auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
    require(!popups.empty() && popups[0].Window, "File menu did not open");
    auto* file_menu = popups[0].Window;
    click(find_item(file_menu, file_menu->GetID("open"), "open"));
    require(selector.IsOpen() && selector.GetCurrentPath() == default_directory,
            "Open selector did not start in the source directory");
    select_entry("opened.sm");
    click_button(selector_main_area(), "Open");
    require(!selector.IsOpen() && app.input_file == open_path.string() &&
                app.editor->GetText() == source,
            "Selected .sm file was not opened");
    const auto recent_path = open_path.lexically_normal().string();
    require(app.settings.recent_files.size() == 1 &&
                app.settings.recent_files.front() == recent_path,
            "Successfully opened .sm file was not added to recent files");

    editor_frame(550, 350);
    editor_window = ImGui::FindWindowByName("SMC Editor");
    const auto recent_menu_bar_id = editor_window->GetID("##MenuBar");
    click(find_item(editor_window, ImHashStr("file", 0, recent_menu_bar_id), "file"));
    auto* recent_file_menu = ImGui::GetCurrentContext()->OpenPopupStack[0].Window;
    click(find_item(recent_file_menu, recent_file_menu->GetID("open recent"), "open recent"));
    require(ImGui::GetCurrentContext()->OpenPopupStack.size() > 1 &&
                ImGui::GetCurrentContext()->OpenPopupStack[1].Window,
            "Open recent submenu did not open");
    auto* recent_files_popup = ImGui::GetCurrentContext()->OpenPopupStack[1].Window;
    click(find_item(recent_files_popup, recent_files_popup->GetID(recent_path.c_str()),
                    recent_path.c_str()));
    require(app.input_file == open_path.string() && app.editor->GetText() == source,
            "Recent-file menu item did not reopen its .sm file");

    const auto file_menu_action = [&](const char* label) {
        editor_frame(550, 350);
        auto* window = ImGui::FindWindowByName("SMC Editor");
        const auto bar = window->GetID("##MenuBar");
        click(find_item(window, ImHashStr("file", 0, bar), "file"));
        auto* menu = ImGui::GetCurrentContext()->OpenPopupStack[0].Window;
        click(find_item(menu, menu->GetID(label), label));
    };
    const auto type_filename = [&](const char* name) {
        editor_frame(550, 350);
        io.AddInputCharactersUTF8(name);
        editor_frame(550, 350);
    };
    const auto unsaved_popup = [&]() {
        editor_frame(550, 350);
        auto* popup = ImGui::FindWindowByName("Unsaved changes");
        require(popup && popup->Active, "Unsaved changes confirmation was not displayed");
        return popup;
    };

    file_menu_action("new");
    require(selector.IsOpen(), "New did not open the save selector");
    type_filename("AppClass.sm");
    click_button(selector_main_area(), "Save");
    const auto new_path = directory / "AppClass.sm";
    const std::string expected =
        "%start StartMap::Start\n%class AppClass\n%header AppClass.h\n\n"
        "%map StartMap\n%%\nStart\n{\n\n}\n%%\n";
    require(app.input_file == new_path.string() && read_file(new_path) == expected &&
                app.editor->GetText() == expected && !app.document_modified() &&
                app.document_title() == "AppClass.sm",
            "New file was not created and opened with the expected template");

    app.editor->ReplaceSectionText({0, 0}, {0, 0}, "// changed\n");
    require(app.document_modified() && app.document_title() == "AppClass.sm *",
            "Changed document was not marked modified in the title");
    app.editor->Undo();
    require(!app.document_modified() && app.document_title() == "AppClass.sm",
            "Restoring saved text did not clear modified state and title");
    app.editor->SetText(expected + "// changed\n");
    editor_frame(550, 350);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_S, true);
    editor_frame(550, 350);
    io.AddKeyEvent(ImGuiKey_S, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    editor_frame(550, 350);
    require(read_file(new_path) == app.editor->GetText() && !app.document_modified(),
            "Ctrl+S did not save and clear the modified state");

    app.editor->SetText(expected + "// unsaved\n");
    file_menu_action("open");
    click_button(unsaved_popup(), "Cancel");
    require(!selector.IsOpen() && app.input_file == new_path.string() &&
                app.document_modified(), "Cancel did not preserve the modified document");
    file_menu_action("open");
    click_button(unsaved_popup(), "Save");
    require(selector.IsOpen() && !app.document_modified() &&
                read_file(new_path) == expected + "// unsaved\n",
            "Save before opening did not save and continue");
    editor_frame(550, 350);
    key(ImGuiKey_Escape);
    require(!selector.IsOpen(), "Open selector remained open after cancellation");
    app.editor->SetText(expected + "// discard\n");
    file_menu_action("new");
    click_button(unsaved_popup(), "Discard");
    require(selector.IsOpen() && app.document_modified(),
            "Discard did not open new selector while retaining current content");
    editor_frame(550, 350);
    request_window_close(app);
    click_button(unsaved_popup(), "Cancel");
    require(!selector.IsOpen() && !app.close_confirmed && app.document_modified(),
            "Canceling close from the selector did not preserve the current contents");

    request_window_close(app);
    click_button(unsaved_popup(), "Cancel");
    require(!app.close_confirmed && app.document_modified(),
            "Canceling close did not retain the modified document");
    request_window_close(app);
    click_button(unsaved_popup(), "Discard");
    require(app.close_confirmed, "Discarding changes did not confirm close");
    app.close_confirmed = false;
    app.editor->SetText(app.saved_text);

    file_menu_action("new");
    type_filename("Invalid-Name");
    click_button(selector_main_area(), "Save");
    require(!std::filesystem::exists(directory / "Invalid-Name.sm") &&
                app.input_file == new_path.string() &&
                app.log.snapshot().records.back().level == LogLevel::error,
            "Invalid class name was not rejected");

    file_menu_action("new");
    type_filename("AppClass");
    click_button(selector_main_area(), "Save");
    auto* overwrite = ImGui::FindWindowByName("Overwrite source file?");
    require(overwrite && overwrite->Active &&
                read_file(new_path) == app.saved_text,
            "Appending .sm did not require confirmation for an existing file");
    click_button(overwrite, "Cancel");
    require(read_file(new_path) == app.saved_text, "Canceling overwrite changed the file");

    file_menu_action("new");
    type_filename("AppClass");
    click_button(selector_main_area(), "Save");
    overwrite = ImGui::FindWindowByName("Overwrite source file?");
    require(overwrite && overwrite->Active, "Overwrite confirmation did not reopen");
    click_button(overwrite, "Overwrite");
    require(read_file(new_path) == expected && !app.document_modified(),
            "Confirmed overwrite did not create the template");

    file_menu_action("new");
    type_filename("Another");
    click_button(selector_main_area(), "Save");
    const auto another_path = directory / "Another.sm";
    require(app.input_file == another_path.string() &&
                read_file(another_path).find("%class Another\n%header Another.h\n") !=
                    std::string::npos && !app.document_modified(),
            "Missing .sm extension was not added to a new file");

    app.editor->SetText("recent unsaved");
    editor_frame(550, 350);
    auto* recent_editor = ImGui::FindWindowByName("SMC Editor");
    const auto recent_bar = recent_editor->GetID("##MenuBar");
    click(find_item(recent_editor, ImHashStr("file", 0, recent_bar), "file"));
    auto* recent_menu = ImGui::GetCurrentContext()->OpenPopupStack[0].Window;
    click(find_item(recent_menu, recent_menu->GetID("open recent"), "open recent"));
    auto* recent_popup = ImGui::GetCurrentContext()->OpenPopupStack[1].Window;
    const auto appclass_recent = new_path.lexically_normal().string();
    click(find_item(recent_popup, recent_popup->GetID(appclass_recent.c_str()),
                    appclass_recent.c_str()));
    click_button(unsaved_popup(), "Cancel");
    require(app.input_file == another_path.string() && app.document_modified(),
            "Canceling a recent-file switch did not preserve the current document");

    app.editor->SetText("unsaved");
    app.input_file = (directory / "missing" / "unwritable.sm").string();
    request_window_close(app);
    click_button(unsaved_popup(), "Save");
    require(!app.close_confirmed && app.document_modified() &&
                app.pending_file_action == FileAction::close_window,
            "Failed save should not close or clear unsaved state");
    click_button(unsaved_popup(), "Cancel");
    app.input_file = another_path.string();

    app.editor.reset();
    ImGui::DestroyContext();
}

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("dot-static-cpp-generation-" + std::to_string(unique));
    try {
        require(std::filesystem::create_directory(directory),
                "Could not create temporary test directory");
#ifndef _WIN32
        {
            std::ofstream utf8_name(directory / "\xe6\xb5\x8b\xe8\xaf\x95.sm");
            std::ofstream invalid_name(directory / std::string("\xff", 1));
            require(utf8_name.good() && invalid_name.good(), "Could not create filename fixtures");
        }
#endif
        test_generation_and_write(directory);
        test_invalid_source(directory);
        test_todo_generation(directory);
        test_editor_menu(directory);
        std::filesystem::remove_all(directory);
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << "cpp_generation_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
