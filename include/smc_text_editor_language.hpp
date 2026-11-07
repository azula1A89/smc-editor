#pragma once

#include <TextEditor.h>

#include <cctype>
#include <string_view>

namespace smc_editor {

inline const TextEditor::Language* language() {
    static const TextEditor::Language definition = [] {
        TextEditor::Language result;
        result.name = "SMC";
        result.preprocess = '%';
        result.singleLineComment = "//";
        result.commentStart = "/*";
        result.commentEnd = "*/";
        result.hasSingleQuotedStrings = true;
        result.hasDoubleQuotedStrings = true;
        result.stringEscape = '\\';
        result.keywords = {"nil", "push", "pop", "jump"};
        result.isPunctuation = [](ImWchar character) {
            return character < 128 &&
                std::string_view("{}[](),:;/.*$=!<>+-").find(
                    static_cast<char>(character)) != std::string_view::npos;
        };
        result.getIdentifier = [](TextEditor::Iterator start, TextEditor::Iterator end) {
            const auto is_start = [](ImWchar character) {
                return character < 128 &&
                    (std::isalpha(static_cast<unsigned char>(character)) != 0 ||
                     character == '_');
            };
            const auto is_part = [](ImWchar character) {
                return character < 128 &&
                    (std::isalnum(static_cast<unsigned char>(character)) != 0 ||
                     character == '_' || character == '.' ||
                     character == '<' || character == '>');
            };

            if (start == end || !is_start(*start))
                return start;

            auto current = start;
            while (current != end && is_part(*current))
                ++current;

            while (current != end && *current == ':') {
                auto separator_end = current;
                ++separator_end;
                if (separator_end == end || *separator_end != ':')
                    break;

                auto qualified_name = separator_end;
                ++qualified_name;
                if (qualified_name == end || !is_start(*qualified_name))
                    break;

                current = qualified_name;
                while (current != end && is_part(*current))
                    ++current;
            }
            return current;
        };
        return result;
    }();
    return &definition;
}

} // namespace smc_editor
