#include <cpp_generation.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <smc/generator.hpp>
#include <smc/parser.hpp>
#include <smc/syntax_checker.hpp>

namespace {

std::string describe_diagnostics(const smc::Diagnostics& diagnostics) {
    std::ostringstream message;
    for (const auto& diagnostic : diagnostics) {
        if (message.tellp() > 0)
            message << '\n';
        message << diagnostic.location.file << ':' << diagnostic.location.line << ':'
                << diagnostic.location.column << ": " << diagnostic.message;
    }
    if (message.tellp() == 0)
        return "SMC parsing or validation failed without a diagnostic";
    return message.str();
}

smc::Fsm parse_fsm(
    std::string_view source, const std::filesystem::path& input_path,
    smc::Diagnostics& diagnostics) {
    smc::Parser parser(source, input_path.string(), diagnostics);
    auto fsm = parser.parse();
    if (!fsm)
        throw std::runtime_error(describe_diagnostics(diagnostics));
    return std::move(*fsm);
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("Cannot open generated file '" + path.string() + "'");
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output)
        throw std::runtime_error("Failed to write generated file '" + path.string() + "'");
}

} // namespace

CppGenerationFiles generate_cpp_files(
    std::string_view source, const std::filesystem::path& input_path) {
    const auto stem = input_path.stem();
    if (stem.empty())
        throw std::invalid_argument("Input file must have a filename");

    CppGenerationFiles files;
    files.header_path = input_path.parent_path() / (stem.string() + ".hpp");
    files.source_path = input_path.parent_path() / (stem.string() + ".cpp");

    smc::Diagnostics diagnostics;
    auto fsm = parse_fsm(source, input_path, diagnostics);

    smc::SyntaxChecker checker(diagnostics);
    if (!checker.check(fsm, "c++"))
        throw std::runtime_error(describe_diagnostics(diagnostics));

    std::ostringstream header;
    smc::CppHeaderGenerator header_generator(header, files.header_path.filename().string());
    fsm.accept(header_generator);

    std::ostringstream implementation;
    smc::CppSourceGenerator source_generator(
        implementation, files.header_path.filename().string());
    fsm.accept(source_generator);

    files.header_contents = header.str();
    files.source_contents = implementation.str();
    return files;
}

std::string generate_cpp_todo(
    std::string_view source, const std::filesystem::path& input_path) {
    smc::Diagnostics diagnostics;
    const auto fsm = parse_fsm(source, input_path, diagnostics);
    return smc::generate_cpp_todo(fsm);
}

void write_cpp_files(const CppGenerationFiles& files) {
    write_file(files.header_path, files.header_contents);
    write_file(files.source_path, files.source_contents);
}
