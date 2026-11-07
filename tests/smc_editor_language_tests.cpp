#include <smc_text_editor_language.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void testLanguageDefinition() {
    const auto* language = smc_editor::language();
    require(language->name == "SMC", "Language name is not SMC");
    require(language->preprocess == '%', "SMC directives are not recognized");
    require(language->singleLineComment == "//" &&
                language->commentStart == "/*" && language->commentEnd == "*/",
            "SMC comments are not configured");
    require(language->hasSingleQuotedStrings && language->hasDoubleQuotedStrings &&
                language->stringEscape == '\\',
            "SMC string literals are not configured");
    require(language->keywords.contains("nil") &&
                language->keywords.contains("push") &&
                language->keywords.contains("pop") &&
                language->keywords.contains("jump"),
            "SMC transition keywords are missing");
}

void testQualifiedIdentifiers() {
    const auto* language = smc_editor::language();
    const std::string source = "Main::Ready";
    std::vector<TextEditor::Glyph> glyphs;
    glyphs.reserve(source.size());
    for (const unsigned char character : source)
        glyphs.emplace_back(character);

    auto end = TextEditor::Iterator(glyphs.data());
    for (std::size_t index = 0; index < glyphs.size(); ++index)
        ++end;
    auto token_end = language->getIdentifier(TextEditor::Iterator(glyphs.data()), end);
    require(token_end - TextEditor::Iterator(glyphs.data()) == source.size(),
            "Qualified SMC state names must be tokenized as one identifier");
}

} // namespace

int main() {
    try {
        testLanguageDefinition();
        testQualifiedIdentifiers();
    } catch (const std::exception& error) {
        std::cerr << "smc_editor_language_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
