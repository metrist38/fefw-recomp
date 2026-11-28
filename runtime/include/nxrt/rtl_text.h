// SPDX-License-Identifier: GPL-3.0-or-later
// Right-to-left text for the overlay (src/core/rtl_text.cpp). ImGui draws a string's characters left to right as they come and
// knows no Arabic letter forms, so Arabic or Hebrew text is turned into display order first: each Arabic letter takes the form
// its neighbours ask for (Arabic Presentation Forms-B, with the lam-alef ligatures), then the line is laid out by a reduced
// Unicode bidi algorithm (strong, weak and neutral types, numbers kept left to right, mirrored brackets; no explicit embeddings,
// isolates or Arabic-Indic number shaping). Text without a right-to-left character comes back unchanged.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace nxrt::rtl {

bool is_rtl(char32_t c);                         // a letter of a right-to-left script (Hebrew, Arabic and its presentation forms)
bool has_rtl(std::u32string_view s);
// Already in display order: holds Arabic presentation forms and no plain Arabic letter. Fan translations of console games
// often store their text this way (the game draws left to right), so a mod's own strings must not be turned a second time.
bool is_display_order(std::u32string_view s);

// Contextual letter forms, logical order kept. owner (optional): for each output character, the index of the first input
// character it stands for (a lam-alef ligature stands for two).
std::u32string shape(std::u32string_view logical, std::vector<size_t>* owner = nullptr);

struct Options {
    bool printf_tokens = false;   // a printf conversion ("%d", "%.1f", "%ls") is one left-to-right unit: a format string stays usable
};

// One line (no '\n') from logical to display order, shaped. caret (optional) gets logical.size() + 1 entries: for each caret
// position in the logical text, how many display characters lie left of it.
std::u32string to_display(std::u32string_view logical, const Options& o = {}, std::vector<size_t>* caret = nullptr);
// The same for UTF-8 text, line by line; text without a right-to-left character, or already in display order, is returned as is.
std::string to_display_utf8(const std::string& logical, const Options& o = {});
// Display order back to logical: letter forms to plain letters, the line laid out again. For a game's initial text that a mod
// stored in display order, so the player edits it in typing order.
std::u32string from_display(std::u32string_view display);

std::u32string utf8_to_u32(std::string_view s);
std::string u32_to_utf8(std::u32string_view s);

}  // namespace nxrt::rtl
