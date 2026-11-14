# Apply the small position-based rendering extension to the build-local copy.
function(replace_once variable old new)
    string(FIND "${${variable}}" "${old}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "ImGuiColorTextEdit semantic patch does not match ${variable}")
    endif()
    string(LENGTH "${old}" old_length)
    math(EXPR remaining_start "${position} + ${old_length}")
    string(SUBSTRING "${${variable}}" ${remaining_start} -1 remaining)
    string(FIND "${remaining}" "${old}" duplicate)
    if(NOT duplicate EQUAL -1)
        message(FATAL_ERROR "ImGuiColorTextEdit semantic patch is ambiguous in ${variable}")
    endif()
    string(REPLACE "${old}" "${new}" updated "${${variable}}")
    set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

function(write_if_changed path contents)
    if(EXISTS "${path}")
        file(READ "${path}" current)
    endif()
    if(NOT current STREQUAL contents)
        file(WRITE "${path}" "${contents}")
    endif()
endfunction()

file(READ "${color_te_original_source_dir}/TextEditor.h" header)
replace_once(header
    "		currentLineHighlightBorder,\n		count"
    "		currentLineHighlightBorder,\n		semanticMap,\n		semanticState,\n		semanticTransition,\n		count")
replace_once(header
    "	inline void SetPalette(const Palette& newPalette)"
    "	struct SemanticRange { DocPos start; DocPos end; Color color; };\n	void SetSemanticRanges(const std::vector<SemanticRange>& ranges);\n	Color GetSemanticColor(DocPos pos) const;\n\n	inline void SetPalette(const Palette& newPalette)")
# Glyphs keep semantic color separately so editor tokenization and autocomplete
# continue to see the original lexical color.
replace_once(header
    "		Color color = Color::text;\n\n		// maintained by the TypeSetter"
    "		Color color = Color::text;\n		Color semanticColor = Color::count;\n\n		// maintained by the TypeSetter")
replace_once(header
    "		bool showMiniMap = false;\n\n		// support functions"
    "		bool showMiniMap = false;\n		bool semanticDirty = false;\n\n		// support functions")
replace_once(header
    "	Palette paletteBase;\n	Palette palette;"
    "	Palette paletteBase;\n	bool semanticColorsActive = false;\n	Palette palette;")
write_if_changed("${color_te_SOURCE_DIR}/TextEditor.h" "${header}")

file(READ "${color_te_original_source_dir}/TextEditor.cpp" source)
replace_once(source
    "		documentChanged = updateState();\n\n		// handle mouse inputs"
    "		documentChanged = updateState();\n		if (documentChanged) {\n			SetSemanticRanges({});\n			miniMap.update(config, document, typeSetter);\n		}\n\n		// handle mouse inputs")
replace_once(source
    "		renderFindReplace();\n		renderPopups();\n\n		// handle scrolling"
    "		renderFindReplace();\n		renderPopups();\n		if (document.isUpdated()) {\n			documentChanged = true;\n			SetSemanticRanges({});\n		}\n\n		// handle scrolling")
replace_once(source
    "font->RenderChar(drawList, fontSize, glyphPos, palette.get(glyph.color), codepoint);"
    "font->RenderChar(drawList, fontSize, glyphPos, palette.get(glyph.semanticColor == Color::count ? glyph.color : glyph.semanticColor), codepoint);")
replace_once(source
    "if (configChanged || typeSetter.isUpdated()) {\n		// reset state\n		const auto rowCount = typeSetter.getRowCount();"
    "if (configChanged || typeSetter.isUpdated() || semanticDirty) {\n		semanticDirty = false;\n		// reset state\n		const auto rowCount = typeSetter.getRowCount();")
replace_once(source
    "		// detect end of section\n		if (glyph.color != color) {"
    "		const auto effectiveColor = glyph.semanticColor == Color::count ? glyph.color : glyph.semanticColor;\n		// detect end of section\n		if (effectiveColor != color) {")
replace_once(source
    "			color = glyph.color;\n		}\n\n		// update column number"
    "			color = effectiveColor;\n		}\n\n		// update column number")
replace_once(source
    "		IM_COL32(255, 255, 255,  16)	// current line highlight border"
    "		IM_COL32(255, 255, 255,  16),	// current line highlight border\n		IM_COL32( 78, 201, 176, 255),\n		IM_COL32( 86, 156, 214, 255),\n		IM_COL32(220, 180, 110, 255)")
replace_once(source
    "		IM_COL32(  0,   0,   0,  16)	// current line highlight border"
    "		IM_COL32(  0,   0,   0,  16),	// current line highlight border\n		IM_COL32(  0, 112,  90, 255),\n		IM_COL32(  0,  75, 170, 255),\n		IM_COL32(140,  70,   0, 255)")
replace_once(source
    "void TextEditor::SetLanguage(const Language* language) {"
    [=[void TextEditor::SetSemanticRanges(const std::vector<SemanticRange>& ranges) {
	if (ranges.empty() && !semanticColorsActive)
		return;
	for (auto& line : document)
		for (auto& glyph : line)
			glyph.semanticColor = Color::count;
	semanticColorsActive = !ranges.empty();
	for (const auto& range : ranges) {
		const auto start = normalizePos(range.start);
		const auto end = normalizePos(range.end);
		if (start < end && range.color != Color::count)
			document.iterateGlyphs(start, end, [&](Glyph& glyph) { glyph.semanticColor = range.color; });
	}
	miniMap.semanticDirty = true;
}

TextEditor::Color TextEditor::GetSemanticColor(DocPos pos) const {
	pos = normalizePos(pos);
	if (pos.index >= document[pos.line].size())
		return Color::count;
	return document[pos.line][pos.index].semanticColor;
}

void TextEditor::SetLanguage(const Language* language) {]=])
write_if_changed("${color_te_SOURCE_DIR}/TextEditor.cpp" "${source}")
