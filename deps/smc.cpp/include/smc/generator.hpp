#pragma once

#include "smc/ast.hpp"

namespace smc {

class Generator {
public:
    virtual ~Generator() = default;

    virtual void visit(const Fsm& fsm) = 0;
    virtual void visit(const StateMap& map) = 0;
    virtual void visit(const State& state) = 0;
    virtual void visit(const Transition& transition) = 0;
    virtual void visit(const Guard& guard) = 0;
};

} // namespace smc
