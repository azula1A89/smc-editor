#include "smc/generator.hpp"
#include "smc/parser.hpp"
#include "smc/syntax_checker.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_usage(std::ostream& output) {
    output << "Usage: smc_cpp -c++ [-d OUTPUT_DIR] [--check] INPUT.sm\n"
              "  --check  lex, parse, and validate without invoking code generation\n";
}

void print_diagnostics(const smc::Diagnostics& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        const char* severity = diagnostic.severity == smc::DiagnosticSeverity::error
                                   ? "error"
                                   : "warning";
        std::cerr << diagnostic.location.file << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column << ": " << severity << ": "
                  << diagnostic.message << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string input_file;
    std::string output_directory{"."};
    bool check_only = false;
    bool cpp_target = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "-c++") {
            cpp_target = true;
        } else if (argument == "--check") {
            check_only = true;
        } else if (argument == "-d") {
            if (index + 1 >= argc) {
                std::cerr << "smc_cpp: -d requires an output directory\n";
                return 2;
            }
            output_directory = argv[++index];
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "smc_cpp: unknown option '" << argument << "'\n";
            print_usage(std::cerr);
            return 2;
        } else if (input_file.empty()) {
            input_file = argument;
        } else {
            std::cerr << "smc_cpp: only one input .sm file is supported\n";
            return 2;
        }
    }

    if (!cpp_target || input_file.empty()) {
        print_usage(std::cerr);
        return 2;
    }

    std::ifstream input(input_file, std::ios::binary);
    if (!input) {
        std::cerr << "smc_cpp: cannot open input file '" << input_file << "'\n";
        return 2;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    if (input.bad()) {
        std::cerr << "smc_cpp: failed while reading input file '" << input_file << "'\n";
        return 2;
    }

    smc::Diagnostics diagnostics;
    smc::Parser parser(contents, input_file, diagnostics);
    const auto fsm = parser.parse();
    if (!fsm) {
        print_diagnostics(diagnostics);
        return 1;
    }

    smc::SyntaxChecker checker(diagnostics);
    if (!checker.check(*fsm, "c++")) {
        print_diagnostics(diagnostics);
        return 1;
    }

    print_diagnostics(diagnostics);
    if (check_only) {
        return 0;
    }

    smc::CppGeneratorInterface generator;
    const auto generation_diagnostics = generator.generate(*fsm, output_directory);
    print_diagnostics(generation_diagnostics);
    return generation_diagnostics.empty() ? 0 : 1;
}
