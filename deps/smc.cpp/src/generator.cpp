#include "smc/generator.hpp"

#include <utility>

namespace smc {

Diagnostics CppGeneratorInterface::generate(
    const Fsm& fsm, const std::filesystem::path& output_directory) const {
    (void)fsm;
    (void)output_directory;
    return {Diagnostic{DiagnosticSeverity::error,
                       fsm.location,
                       "C++ code generation is not implemented; use --check to validate the FSM."}};
}

} // namespace smc
