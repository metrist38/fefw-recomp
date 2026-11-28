// SPDX-License-Identifier: GPL-3.0-or-later
// Right-to-left text in display order (src/core/rtl_text.cpp): letter forms, the line's order, the caret, and the way back.
#include <string>
#include <vector>

#include "nxrt/rtl_text.h"
#include "test_framework.h"

NXTEST_GROUP(rtl_text)

namespace {

std::u32string u(const char* s) { return nxrt::rtl::utf8_to_u32(s); }
std::u32string display(const char* s, bool printf_tokens = false) {
    nxrt::rtl::Options o;
    o.printf_tokens = printf_tokens;
    return nxrt::rtl::to_display(u(s), o);
}

}  // namespace

NXTEST(text_without_rtl_letters_is_unchanged) {
    CHECK(display("Player 1 (ready)") == U"Player 1 (ready)");
    CHECK(nxrt::rtl::to_display_utf8("Frame rate: %d") == "Frame rate: %d");
    CHECK(!nxrt::rtl::has_rtl(u("Ångström 3D")));
}

NXTEST(letters_join_and_read_right_to_left) {
    // "rama": reh and alef join nothing after them, meem joins the last alef
    CHECK(display("راما") == std::u32string({0xFE8E, 0xFEE3, 0xFE8D, 0xFEAD}));
    // "la ba's": the lam-alef ligature, beh joined to alef-hamza
    CHECK(display("لا بأس") == std::u32string({0xFEB1, 0xFE84, 0xFE91, 0x0020, 0xFEFB}));
}

NXTEST(numbers_stay_left_to_right) {
    // "version 1.5": the number reads left to right at the line's left end
    CHECK(display("الإصدار 1.5") ==
          std::u32string({0x0031, 0x002E, 0x0035, 0x0020, 0xFEAD, 0xFE8D, 0xFEAA, 0xFEBB, 0xFEF9, 0xFE8D}));
}

NXTEST(brackets_are_mirrored) {
    const std::u32string d = display("نص (بين قوسين) هنا");
    CHECK(d.find(U'(') < d.find(U')'));   // the opening bracket of the right-to-left text is drawn on the right: as '(' left of ')'
    CHECK(d[4] == U'(' && d[14] == U')');
}

NXTEST(printf_conversions_stay_whole_and_in_order) {
    const std::u32string d = display("الإطارات: %d / %d", true);
    CHECK(d.substr(0, 7) == U"%d / %d");
    CHECK(display("تم حفظ %s بنجاح", true).find(U"%s") != std::u32string::npos);
    // without the option a conversion is only letters and signs: "d%" could come out
    CHECK(display("القيمة %d", true).find(U"%d") != std::u32string::npos);
}

NXTEST(marks_follow_their_letter) {
    // shadda on ain: drawn after the ain, so a zero-width mark sits over it
    const std::u32string d = display("مفعّل");
    const size_t ain = d.find(char32_t{0xFECC});
    CHECK(ain != std::u32string::npos && d[ain + 1] == 0x0651);
}

NXTEST(caret_follows_the_reading_direction) {
    std::vector<size_t> caret;
    nxrt::rtl::to_display(u("راما"), {}, &caret);
    CHECK(caret == std::vector<size_t>({4, 3, 2, 1, 0}));   // the start of the word is its right edge
    nxrt::rtl::to_display(u("لا بأس"), {}, &caret);
    CHECK(caret[0] == 5 && caret[1] == 5);                  // both letters of the ligature share one place
    nxrt::rtl::to_display(u("abc"), {}, &caret);
    CHECK(caret == std::vector<size_t>({0, 1, 2, 3}));
}

NXTEST(display_order_is_recognised_and_undone) {
    const std::u32string d = display("السلام عليكم");
    CHECK(nxrt::rtl::is_display_order(d));
    CHECK(!nxrt::rtl::is_display_order(u("السلام عليكم")));
    CHECK(!nxrt::rtl::is_display_order(u("Player")));
    for (const char* s : {"راما", "السلام عليكم", "لا بأس", "الإصدار 1.5", "نص (بين قوسين) هنا"})
        CHECK(nxrt::rtl::from_display(display(s)) == u(s));
    // text already in display order is not turned again
    CHECK(nxrt::rtl::to_display_utf8(nxrt::rtl::u32_to_utf8(d)) == nxrt::rtl::u32_to_utf8(d));
}

NXTEST(lines_are_laid_out_one_by_one) {
    const std::string two = nxrt::rtl::to_display_utf8("راما\nلا بأس");
    CHECK(two == nxrt::rtl::u32_to_utf8(display("راما")) + "\n" + nxrt::rtl::u32_to_utf8(display("لا بأس")));
}
