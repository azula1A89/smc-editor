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
    app.editor->SetText(source);
    app.editor_text = source;
    app.input_file = (directory / "menu.sm").string();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(600, 400);
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
    const auto generate_from_menu = [&] {
        editor_frame(550, 350);
        ImGuiWindow* editor_window = ImGui::FindWindowByName("SMC Editor");
        const ImRect bar = editor_window->MenuBarRect();
        click(ImVec2(bar.Min.x + 70.0f, (bar.Min.y + bar.Max.y) * 0.5f));
        auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
        require(!popups.empty() && popups[0].Window,
                "C++ generation menu did not open");
        ImGuiWindow* menu = popups[0].Window;
        click(find_item(menu, menu->GetID("Generate .hpp and .cpp"),
                        "Generate .hpp and .cpp"));
    };

    generate_from_menu();
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
    require(app.pending_cpp_generation.has_value() &&
                read_file(header_path) == "preserve until confirmation",
            "Existing C++ output was overwritten without confirmation");
    app.pending_cpp_generation.reset();
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
