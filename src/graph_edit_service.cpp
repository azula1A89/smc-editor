#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <analysis.hpp>
#include <graph_edit_service.hpp>
#include <smc/generator.hpp>
#include <smc/parser.hpp>
#include <smc/syntax_checker.hpp>

namespace {

struct TextEdit {
    std::size_t begin;
    std::size_t end;
    std::string replacement;
};

struct StateLocation {
    std::size_t map_index;
    std::size_t state_index;
};

bool valid_identifier(const std::string& value) {
    if (value.empty())
        return false;
    const auto is_start = [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
    };
    const auto is_part = [&](unsigned char ch) {
        return is_start(ch) || (ch >= '0' && ch <= '9');
    };
    return is_start(static_cast<unsigned char>(value.front())) &&
        std::all_of(value.begin() + 1, value.end(), [&](char ch) {
            return is_part(static_cast<unsigned char>(ch));
        });
}

std::vector<std::size_t> line_offsets(std::string_view source) {
    std::vector<std::size_t> offsets{0};
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\r') {
            if (i + 1 < source.size() && source[i + 1] == '\n')
                ++i;
            offsets.push_back(i + 1);
        } else if (source[i] == '\n') {
            offsets.push_back(i + 1);
        }
    }
    return offsets;
}

std::optional<std::size_t> offset_at(
    const smc::SourceLocation& location, std::string_view source,
    const std::vector<std::size_t>& offsets) {
    if (location.line == 0 || location.line > offsets.size())
        return std::nullopt;
    const auto line = location.line - 1;
    const auto end = line + 1 < offsets.size() ? offsets[line + 1] : source.size();
    return std::min(offsets[line] + (location.column > 0 ? location.column - 1 : 0), end);
}

std::optional<std::pair<std::size_t, std::size_t>> range_offsets(
    const smc::SourceLocation& begin, const smc::SourceLocation& end,
    std::string_view source, const std::vector<std::size_t>& offsets) {
    const auto first = offset_at(begin, source, offsets);
    const auto last = offset_at(end, source, offsets);
    if (!first || !last || *last < *first)
        return std::nullopt;
    return std::pair{*first, *last};
}

std::vector<StateLocation> state_locations(const smc::Fsm& fsm) {
    std::vector<StateLocation> result;
    for (std::size_t map = 0; map < fsm.maps.size(); ++map) {
        for (std::size_t state = 0; state < fsm.maps[map].states.size(); ++state)
            result.push_back({map, state});
    }
    return result;
}

std::string diagnostics_text(const smc::Diagnostics& diagnostics) {
    std::string result;
    for (const auto& diagnostic : diagnostics) {
        if (!result.empty())
            result += '\n';
        result += diagnostic.location.file + ':' +
            std::to_string(diagnostic.location.line) + ':' +
            std::to_string(diagnostic.location.column) + ": " +
            diagnostic.message;
    }
    return result;
}

bool parse_and_check(
    const std::string& source, const std::string& source_name,
    smc::Fsm& fsm, std::string& error) {
    smc::Diagnostics diagnostics;
    smc::Parser parser(source, source_name, diagnostics);
    auto parsed = parser.parse();
    if (!parsed) {
        error = diagnostics_text(diagnostics);
        return false;
    }
    smc::SyntaxChecker checker(diagnostics);
    if (!checker.check(*parsed, "c++")) {
        error = diagnostics_text(diagnostics);
        return false;
    }
    try {
        std::ostringstream generated_header;
        smc::CppHeaderGenerator generator(generated_header, parsed->header_file);
        parsed->accept(generator);
    } catch (const std::invalid_argument& exception) {
        error = exception.what();
        return false;
    }
    fsm = std::move(*parsed);
    return true;
}

bool applies_to_state(
    const std::string& reference, const std::string& current_map,
    const std::string& target_map, const std::string& target_state) {
    if (reference == target_map + "::" + target_state)
        return true;
    return current_map == target_map && reference == target_state;
}

bool apply_text_edits(
    std::string& source, std::vector<TextEdit> edits, std::string& error) {
    std::sort(edits.begin(), edits.end(), [](const TextEdit& left, const TextEdit& right) {
        if (left.begin != right.begin)
            return left.begin < right.begin;
        return left.end < right.end;
    });
    for (std::size_t i = 0; i < edits.size(); ++i) {
        if (edits[i].begin > edits[i].end || edits[i].end > source.size() ||
            (i > 0 && edits[i].begin < edits[i - 1].end)) {
            error = "Graph edit produced overlapping or invalid source ranges.";
            return false;
        }
    }
    for (auto edit = edits.rbegin(); edit != edits.rend(); ++edit)
        source.replace(edit->begin, edit->end - edit->begin, edit->replacement);
    return true;
}

} // namespace

GraphEditResult apply_graph_edit(
    const std::string& source, const std::string& source_name,
    const GraphEditCommand& command) {
    GraphEditResult result;
    const auto name = source_name.empty() ? std::string("graph_editor.sm") : source_name;
    smc::Fsm fsm;
    if (!parse_and_check(source, name, fsm, result.error))
        return result;

    const auto offsets = line_offsets(source);
    auto locations = state_locations(fsm);
    std::vector<TextEdit> edits;

    switch (command.operation) {
    case GraphEditOperation::create_state: {
        if (command.map_index >= fsm.maps.size()) {
            result.error = "The selected state map no longer exists.";
            return result;
        }
        if (!valid_identifier(command.name) || command.name == "Default") {
            result.error = "Enter a valid state name other than Default.";
            return result;
        }
        auto& map = fsm.maps[command.map_index];
        if (std::any_of(map.states.begin(), map.states.end(), [&](const auto& state) {
                return state.name == command.name;
            })) {
            result.error = "A state with that name already exists in this map.";
            return result;
        }
        const auto insertion = offset_at(map.end_location, source, offsets);
        if (!insertion || *insertion < 2 ||
            source.substr(*insertion - 2, 2) != "%%") {
            result.error = "Could not safely locate the end of the selected state map.";
            return result;
        }
        edits.push_back({*insertion - 2, *insertion - 2,
                         command.name + "\n{\n}\n"});
        smc::State state;
        state.name = command.name;
        const auto default_state = std::find_if(
            map.states.begin(), map.states.end(), [](const auto& candidate) {
                return candidate.name == "Default";
            });
        map.states.insert(default_state, std::move(state));
        break;
    }
    case GraphEditOperation::rename_state: {
        if (command.state_index >= locations.size()) {
            result.error = "The selected state no longer exists.";
            return result;
        }
        const auto location = locations[command.state_index];
        auto& state = fsm.maps[location.map_index].states[location.state_index];
        if (!state.name_range) {
            result.error = "The synthetic Default state cannot be renamed.";
            return result;
        }
        if (!valid_identifier(command.name) || command.name == "Default") {
            result.error = "Enter a valid state name other than Default.";
            return result;
        }
        const auto old_name = state.name;
        const auto map_name = fsm.maps[location.map_index].name;
        const auto name_range = range_offsets(
            state.name_range->begin, state.name_range->end, source, offsets);
        if (!name_range) {
            result.error = "Could not safely locate the selected state's name.";
            return result;
        }
        edits.push_back({name_range->first, name_range->second, command.name});
        state.name = command.name;

        const auto start_separator = fsm.start_state.find("::");
        if (start_separator != std::string::npos &&
            fsm.start_state.substr(0, start_separator) == map_name &&
            fsm.start_state.substr(start_separator + 2) == old_name &&
            fsm.start_state_range) {
            const auto start_range = range_offsets(
                fsm.start_state_range->begin, fsm.start_state_range->end,
                source, offsets);
            if (!start_range) {
                result.error = "Could not safely locate the start-state declaration.";
                return result;
            }
            edits.push_back({start_range->first, start_range->second,
                             map_name + "::" + command.name});
            fsm.start_state = map_name + "::" + command.name;
        }
        for (auto& map : fsm.maps) {
            for (auto& source_state : map.states) {
                for (auto& transition : source_state.transitions) {
                    for (auto& guard : transition.guards) {
                        if (guard.transition_type == "pop")
                            continue;
                        if (applies_to_state(
                                guard.end_state, map.name, map_name, old_name)) {
                            if (!guard.end_state_range) {
                                result.error = "Could not safely locate a transition destination.";
                                return result;
                            }
                            const auto range = range_offsets(
                                guard.end_state_range->begin, guard.end_state_range->end,
                                source, offsets);
                            if (!range) {
                                result.error = "Could not safely locate a transition destination.";
                                return result;
                            }
                            const auto replacement = guard.end_state == old_name
                                ? command.name : map_name + "::" + command.name;
                            edits.push_back({range->first, range->second, replacement});
                            guard.end_state = replacement;
                        }
                        if (!guard.push_state.empty() &&
                            applies_to_state(
                                guard.push_state, map.name, map_name, old_name)) {
                            if (!guard.push_state_range) {
                                result.error = "Could not safely locate a pushed state.";
                                return result;
                            }
                            const auto range = range_offsets(
                                guard.push_state_range->begin, guard.push_state_range->end,
                                source, offsets);
                            if (!range) {
                                result.error = "Could not safely locate a pushed state.";
                                return result;
                            }
                            const auto replacement = guard.push_state == old_name
                                ? command.name : map_name + "::" + command.name;
                            edits.push_back({range->first, range->second, replacement});
                            guard.push_state = replacement;
                        }
                    }
                }
            }
        }
        break;
    }
    case GraphEditOperation::delete_state: {
        if (command.state_index >= locations.size()) {
            result.error = "The selected state no longer exists.";
            return result;
        }
        const auto location = locations[command.state_index];
        const auto& selected = fsm.maps[location.map_index].states[location.state_index];
        if (!selected.name_range) {
            result.error = "The synthetic Default state cannot be deleted.";
            return result;
        }
        const auto target_name = selected.name;
        const auto target_map = fsm.maps[location.map_index].name;
        if (fsm.start_state == target_map + "::" + target_name) {
            result.error = "Change the FSM start state before deleting it.";
            return result;
        }
        const auto selected_range = range_offsets(
            selected.location, selected.end_location, source, offsets);
        if (!selected_range || selected_range->first == selected_range->second) {
            result.error = "Could not safely locate the selected state.";
            return result;
        }
        edits.push_back({selected_range->first, selected_range->second, {}});

        for (std::size_t map_index = 0; map_index < fsm.maps.size(); ++map_index) {
            auto& map = fsm.maps[map_index];
            for (std::size_t state_index = 0; state_index < map.states.size(); ++state_index) {
                if (map_index == location.map_index &&
                    state_index == location.state_index)
                    continue;
                auto& state = map.states[state_index];
                std::vector<smc::Transition> retained;
                retained.reserve(state.transitions.size());
                for (auto& transition : state.transitions) {
                    std::vector<smc::Guard> guards;
                    guards.reserve(transition.guards.size());
                    std::vector<std::pair<std::size_t, std::size_t>> removed_ranges;
                    for (auto& guard : transition.guards) {
                        const bool targets_deleted =
                            guard.transition_type != "pop" &&
                            (applies_to_state(guard.end_state, map.name,
                                              target_map, target_name) ||
                             applies_to_state(guard.push_state, map.name,
                                              target_map, target_name));
                        if (!targets_deleted) {
                            guards.push_back(std::move(guard));
                        } else {
                            const auto guard_range = range_offsets(
                                guard.location, guard.end_location, source, offsets);
                            if (!guard_range) {
                                result.error = "Could not safely locate an incoming transition.";
                                return result;
                            }
                            removed_ranges.push_back(*guard_range);
                        }
                    }
                    if (guards.empty()) {
                        if (!transition.guards.empty()) {
                            const auto transition_range = range_offsets(
                                transition.location, transition.end_location, source, offsets);
                            if (!transition_range) {
                                result.error = "Could not safely locate an incoming transition.";
                                return result;
                            }
                            edits.push_back(
                                {transition_range->first, transition_range->second, {}});
                        }
                        continue;
                    }
                    for (const auto& range : removed_ranges)
                        edits.push_back({range.first, range.second, {}});
                    transition.guards = std::move(guards);
                    retained.push_back(std::move(transition));
                }
                state.transitions = std::move(retained);
            }
        }
        auto& states = fsm.maps[location.map_index].states;
        states.erase(states.begin() + static_cast<std::ptrdiff_t>(location.state_index));
        break;
    }
    case GraphEditOperation::create_transition: {
        if (command.state_index >= locations.size() ||
            command.target_state_index >= locations.size()) {
            result.error = "The source or target state no longer exists.";
            return result;
        }
        const auto source_location = locations[command.state_index];
        const auto target_location = locations[command.target_state_index];
        auto& source_map = fsm.maps[source_location.map_index];
        auto& source_state = source_map.states[source_location.state_index];
        const auto& target_map = fsm.maps[target_location.map_index];
        const auto& target_state = target_map.states[target_location.state_index];
        if (!source_state.name_range || !target_state.name_range ||
            target_state.name == "Default") {
            result.error = "Transitions require source and target states defined in the source.";
            return result;
        }
        const auto close = offset_at(source_state.end_location, source, offsets);
        if (!close || *close == 0 || source[*close - 1] != '}') {
            result.error = "Could not safely locate the source state's closing brace.";
            return result;
        }
        const auto close_offset = *close - 1;
        auto line_start = source.rfind('\n', close_offset);
        line_start = line_start == std::string::npos ? 0 : line_start + 1;
        const bool close_line_is_indented = std::all_of(
            source.begin() + static_cast<std::ptrdiff_t>(line_start),
            source.begin() + static_cast<std::ptrdiff_t>(close_offset),
            [](char ch) { return ch == ' ' || ch == '\t'; });
        const auto insertion = close_line_is_indented ? line_start : close_offset;
        std::string indentation = "    ";
        if (close_line_is_indented)
            indentation.append(source, line_start, close_offset - line_start);

        std::size_t suffix = 1;
        std::string transition_name;
        const auto is_used = [&](const std::string& candidate) {
            return std::any_of(
                source_state.transitions.begin(), source_state.transitions.end(),
                [&](const auto& transition) { return transition.name == candidate; });
        };
        do {
            transition_name = "graph_transition_" + std::to_string(suffix++);
        } while (is_used(transition_name));
        const auto destination = source_location.map_index == target_location.map_index
            ? target_state.name : target_map.name + "::" + target_state.name;
        const std::string transition_text =
            indentation + transition_name + " " + destination + " {}\n";
        edits.push_back({insertion, insertion, transition_text});
        smc::Transition transition;
        transition.name = std::move(transition_name);
        smc::Guard guard;
        guard.end_state = destination;
        transition.guards.push_back(std::move(guard));
        source_state.transitions.push_back(std::move(transition));
        break;
    }
    case GraphEditOperation::update_transition_target: {
        if (command.target_state_index >= locations.size()) {
            result.error = "The selected target state no longer exists.";
            return result;
        }
        const auto target_location = locations[command.target_state_index];
        const auto& target_map = fsm.maps[target_location.map_index];
        const auto& target_state = target_map.states[target_location.state_index];
        if (!target_state.name_range || target_state.name == "Default") {
            result.error = "Transitions cannot target the synthetic Default state.";
            return result;
        }

        std::size_t edge_index = 0;
        bool updated = false;
        for (auto& map : fsm.maps) {
            for (auto& state : map.states) {
                for (auto& transition : state.transitions) {
                    for (auto& guard : transition.guards) {
                        if (edge_index++ != command.edge_index)
                            continue;
                        if (guard.transition_type != "set" &&
                            guard.transition_type != "jump") {
                            result.error =
                                "Only set and jump transition targets can be edited directly.";
                            return result;
                        }
                        if (!guard.end_state_range) {
                            result.error =
                                "Could not safely locate the selected transition target.";
                            return result;
                        }
                        const auto range = range_offsets(
                            guard.end_state_range->begin, guard.end_state_range->end,
                            source, offsets);
                        if (!range) {
                            result.error =
                                "Could not safely locate the selected transition target.";
                            return result;
                        }
                        const auto destination =
                            map.name == target_map.name
                                ? target_state.name
                                : target_map.name + "::" + target_state.name;
                        edits.push_back(
                            {range->first, range->second, destination});
                        guard.end_state = destination;
                        updated = true;
                        break;
                    }
                    if (updated)
                        break;
                }
                if (updated)
                    break;
            }
            if (updated)
                break;
        }
        if (!updated) {
            result.error = "The selected transition no longer exists.";
            return result;
        }
        break;
    }
    case GraphEditOperation::delete_transition: {
        std::size_t edge_index = 0;
        bool removed = false;
        for (auto& map : fsm.maps) {
            for (auto& state : map.states) {
                auto transition = state.transitions.begin();
                while (transition != state.transitions.end()) {
                    const auto edge_end = edge_index + transition->guards.size();
                    if (command.edge_index >= edge_index &&
                        command.edge_index < edge_end) {
                        const auto range = range_offsets(
                            transition->location, transition->end_location,
                            source, offsets);
                        if (!range || range->first == range->second) {
                            result.error = "Could not safely locate the selected transition.";
                            return result;
                        }
                        edits.push_back({range->first, range->second, {}});
                        state.transitions.erase(transition);
                        removed = true;
                        break;
                    }
                    edge_index = edge_end;
                    ++transition;
                }
                if (removed)
                    break;
            }
            if (removed)
                break;
        }
        if (!removed) {
            result.error = "The selected transition no longer exists.";
            return result;
        }
        break;
    }
    }

    std::string candidate = source;
    if (!apply_text_edits(candidate, std::move(edits), result.error))
        return result;

    smc::Fsm validated;
    if (!parse_and_check(candidate, name, validated, result.error))
        return result;
    smc::Diagnostics diagnostics;
    if (!make_graph_from_contents(candidate, name, nullptr, nullptr, nullptr, nullptr,
                                  &diagnostics)) {
        result.error = diagnostics_text(diagnostics);
        if (result.error.empty())
            result.error = "The edited SMC source could not be converted to a graph.";
        return result;
    }
    result.success = true;
    result.source = std::move(candidate);
    return result;
}
