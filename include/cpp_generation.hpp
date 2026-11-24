#pragma once

#include <filesystem>
#include <string>
#include <string_view>

struct CppGenerationFiles {
    std::filesystem::path header_path;
    std::filesystem::path source_path;
    std::string header_contents;
    std::string source_contents;
};

CppGenerationFiles generate_cpp_files(
    std::string_view source, const std::filesystem::path& input_path);
std::string generate_cpp_todo(
    std::string_view source, const std::filesystem::path& input_path);
void write_cpp_files(const CppGenerationFiles& files);
