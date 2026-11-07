#pragma once

#include "smc/ast.hpp"

#include <filesystem>

namespace smc {

class CppGenerator {
public:
    virtual ~CppGenerator() = default;

    virtual Diagnostics generate(const Fsm& fsm,
                                 const std::filesystem::path& output_directory) const = 0;
};

class CppGeneratorInterface final : public CppGenerator {
public:
    Diagnostics generate(const Fsm& fsm,
                         const std::filesystem::path& output_directory) const override;
};

} // namespace smc
