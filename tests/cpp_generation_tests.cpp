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

void test_editor_menu(const std::filesystem::path& directory) {
    const std::string source =
        "%start Main::Ready\n%class Context\n%header context.h\n%map Main\n%%\n"
        "Ready\n{\n    go [x] Done {}\n}\nDone\n{\n}\n%%\n";
    AppContext app;
    app.editor = std::make_unique<TextEditor>();
    app.coordinator = std::make_unique<AnalysisCoordinator>(false, AnalysisCoordinator::Clock::now());
    app.editor->SetText(source);
    app.editor_text = source;
    app.input_file = (directory / "menu.sm").string();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(900, 700);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        static_cast<const void*>(&dejavu), dejavuSize, 15.0f);

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
    const auto generate_from_menu = [&] {
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
        click(find_item(generate_menu, generate_menu->GetID("generate .hpp and .cpp"),
                        "generate .hpp and .cpp"));
    };

    auto& selector = FileSelector::Instance();
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
        test_editor_menu(directory);
        std::filesystem::remove_all(directory);
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << "cpp_generation_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
