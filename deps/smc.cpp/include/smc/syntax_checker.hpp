#pragma once

#include "smc/ast.hpp"

#include <string_view>

namespace smc {

class SyntaxChecker {
public:
    explicit SyntaxChecker(Diagnostics& diagnostics);

    bool check(const Fsm& fsm, std::string_view target_language = "c++");

private:
    void report(const SourceLocation& location, std::string message);
    bool state_exists(std::string_view qualified_state,
                      std::string_view current_map,
                      const Fsm& fsm) const;

    Diagnostics& diagnostics_;
};

} // namespace smc
