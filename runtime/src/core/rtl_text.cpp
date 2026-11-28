// SPDX-License-Identifier: GPL-3.0-or-later
// Right-to-left text in display order (include/nxrt/rtl_text.h).
#include "nxrt/rtl_text.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace nxrt::rtl {

namespace {

// Joining letters and their first presentation form. Dual-joining letters have four forms in a row (isolated, final, initial,
// medial), right-joining ones two (isolated, final), hamza one.
enum Join : uint8_t { kNone, kRight, kDual };
struct Letter {
    char32_t base, form;
    Join join;
};
constexpr Letter kLetters[] = {
    {0x0621, 0xFE80, kNone},  {0x0622, 0xFE81, kRight}, {0x0623, 0xFE83, kRight}, {0x0624, 0xFE85, kRight},
    {0x0625, 0xFE87, kRight}, {0x0626, 0xFE89, kDual},  {0x0627, 0xFE8D, kRight}, {0x0628, 0xFE8F, kDual},
    {0x0629, 0xFE93, kRight}, {0x062A, 0xFE95, kDual},  {0x062B, 0xFE99, kDual},  {0x062C, 0xFE9D, kDual},
    {0x062D, 0xFEA1, kDual},  {0x062E, 0xFEA5, kDual},  {0x062F, 0xFEA9, kRight}, {0x0630, 0xFEAB, kRight},
    {0x0631, 0xFEAD, kRight}, {0x0632, 0xFEAF, kRight}, {0x0633, 0xFEB1, kDual},  {0x0634, 0xFEB5, kDual},
    {0x0635, 0xFEB9, kDual},  {0x0636, 0xFEBD, kDual},  {0x0637, 0xFEC1, kDual},  {0x0638, 0xFEC5, kDual},
    {0x0639, 0xFEC9, kDual},  {0x063A, 0xFECD, kDual},  {0x0641, 0xFED1, kDual},  {0x0642, 0xFED5, kDual},
    {0x0643, 0xFED9, kDual},  {0x0644, 0xFEDD, kDual},  {0x0645, 0xFEE1, kDual},  {0x0646, 0xFEE5, kDual},
    {0x0647, 0xFEE9, kDual},  {0x0648, 0xFEED, kRight}, {0x0649, 0xFEEF, kRight}, {0x064A, 0xFEF1, kDual},
    // Persian and Urdu letters (Presentation Forms-A)
    {0x0671, 0xFB50, kRight}, {0x067E, 0xFB56, kDual},  {0x0686, 0xFB7A, kDual},  {0x0698, 0xFB8A, kRight},
    {0x06A9, 0xFB8E, kDual},  {0x06AF, 0xFB92, kDual},  {0x06CC, 0xFBFC, kDual},
};
constexpr char32_t kLam = 0x0644, kTatweel = 0x0640;
// lam + alef: the alef, the isolated ligature (the final one follows it)
constexpr std::pair<char32_t, char32_t> kLamAlef[] = {{0x0622, 0xFEF5}, {0x0623, 0xFEF7}, {0x0625, 0xFEF9}, {0x0627, 0xFEFB}};

const Letter* letter(char32_t c) {
    for (const Letter& l : kLetters)
        if (l.base == c) return &l;
    return nullptr;
}

// combining marks: no joining of their own, the letters around them join through them
bool is_mark(char32_t c) {
    return (c >= 0x064B && c <= 0x065F) || c == 0x0670 || (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) ||
           c == 0x06E7 || c == 0x06E8 || (c >= 0x06EA && c <= 0x06ED) || (c >= 0x0591 && c <= 0x05C7 && c != 0x05BE && c != 0x05C0 &&
                                                                          c != 0x05C3 && c != 0x05C6);
}
bool joins_prev(char32_t c) {   // can reach back to the letter before it
    if (c == kTatweel) return true;
    const Letter* l = letter(c);
    return l && l->join != kNone;
}
bool joins_next(char32_t c) {   // can reach on to the letter after it
    if (c == kTatweel) return true;
    const Letter* l = letter(c);
    return l && l->join == kDual;
}

const std::unordered_map<char32_t, std::u32string>& plain_of() {
    static const std::unordered_map<char32_t, std::u32string> m = [] {
        std::unordered_map<char32_t, std::u32string> r;
        for (const Letter& l : kLetters) {
            const int n = l.join == kDual ? 4 : l.join == kRight ? 2 : 1;
            for (int k = 0; k < n; ++k) r[l.form + k] = std::u32string(1, l.base);
        }
        for (const auto& [alef, lig] : kLamAlef) {
            r[lig] = std::u32string{kLam, alef};
            r[lig + 1] = std::u32string{kLam, alef};
        }
        return r;
    }();
    return m;
}

// ---- bidi ----
enum Type : uint8_t { L, R, EN, AN, ES, ET, CS, NSM, WS, ON };

Type classify(char32_t c) {
    if (is_mark(c)) return NSM;
    if (is_rtl(c)) return R;
    if (c >= '0' && c <= '9') return EN;
    if ((c >= 0x0660 && c <= 0x0669) || (c >= 0x06F0 && c <= 0x06F9)) return AN;
    if (c == '+' || c == '-') return ES;
    if (c == '#' || c == '$' || c == '%' || c == 0xB0 || (c >= 0xA2 && c <= 0xA5) || c == 0x2030 || c == 0x066A) return ET;
    if (c == ',' || c == '.' || c == ':' || c == '/' || c == 0xA0 || c == 0x060C) return CS;
    if (c == ' ' || c == '\t' || c == 0x3000 || (c >= 0x2000 && c <= 0x200A)) return WS;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return L;
    if (c < 0xC0) return ON;
    if ((c >= 0x2010 && c <= 0x2BFF) || (c >= 0x3001 && c <= 0x303F) || (c >= 0xFE10 && c <= 0xFE6F) || (c >= 0xFF01 && c <= 0xFF0F))
        return ON;
    return L;
}

// the printf conversions of a format string, as [start, end) ranges
std::vector<std::pair<size_t, size_t>> conversions(std::u32string_view s) {
    std::vector<std::pair<size_t, size_t>> out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') continue;
        size_t j = i + 1;
        if (j < s.size() && s[j] == '%') {
            out.push_back({i, j + 1});
            i = j;
            continue;
        }
        while (j < s.size() && (s[j] == '-' || s[j] == '+' || s[j] == '#' || s[j] == '0' || s[j] == ' ')) ++j;
        while (j < s.size() && ((s[j] >= '0' && s[j] <= '9') || s[j] == '*')) ++j;
        if (j < s.size() && s[j] == '.') {
            ++j;
            while (j < s.size() && ((s[j] >= '0' && s[j] <= '9') || s[j] == '*')) ++j;
        }
        while (j < s.size() && (s[j] == 'h' || s[j] == 'l' || s[j] == 'L' || s[j] == 'j' || s[j] == 'z' || s[j] == 't')) ++j;
        if (j < s.size() && s[j] < 0x80 && std::u32string_view(U"diouxXeEfFgGaAcsp").find(s[j]) != std::u32string_view::npos) {
            out.push_back({i, j + 1});
            i = j;
        }
    }
    return out;
}

char32_t mirror(char32_t c) {
    switch (c) {
        case '(': return ')';
        case ')': return '(';
        case '[': return ']';
        case ']': return '[';
        case '{': return '}';
        case '}': return '{';
        case '<': return '>';
        case '>': return '<';
        case 0xAB: return 0xBB;
        case 0xBB: return 0xAB;
        default: return c;
    }
}

}  // namespace

bool is_rtl(char32_t c) {
    return (c >= 0x0590 && c <= 0x08FF && !is_mark(c) && !(c >= 0x0660 && c <= 0x0669) && !(c >= 0x06F0 && c <= 0x06F9)) ||
           (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFC);
}

bool has_rtl(std::u32string_view s) { return std::any_of(s.begin(), s.end(), [](char32_t c) { return is_rtl(c); }); }

bool is_display_order(std::u32string_view s) {
    bool forms = false;
    for (const char32_t c : s) {
        if (letter(c) || c == kTatweel) return false;
        if ((c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFC)) forms = true;
    }
    return forms;
}

std::u32string shape(std::u32string_view in, std::vector<size_t>* owner) {
    std::u32string out;
    if (owner) owner->clear();
    const size_t n = in.size();
    auto prev_of = [&](size_t i) -> char32_t {   // the nearest letter before i, marks skipped; 0 = none
        while (i > 0) {
            --i;
            if (!is_mark(in[i])) return in[i];
        }
        return 0;
    };
    auto next_at = [&](size_t i) -> size_t {   // the index of the nearest letter after i, marks skipped; n = none
        for (++i; i < n; ++i)
            if (!is_mark(in[i])) return i;
        return n;
    };
    for (size_t i = 0; i < n; ++i) {
        const char32_t c = in[i];
        const Letter* l = letter(c);
        if (!l) {
            out.push_back(c);
            if (owner) owner->push_back(i);
            continue;
        }
        const char32_t p = prev_of(i);
        const bool to_prev = l->join != kNone && p && joins_next(p);
        const size_t nx = next_at(i);
        if (c == kLam && nx < n) {
            const auto lig = std::find_if(std::begin(kLamAlef), std::end(kLamAlef), [&](const auto& e) { return e.first == in[nx]; });
            if (lig != std::end(kLamAlef)) {
                out.push_back(lig->second + (to_prev ? 1 : 0));
                if (owner) owner->push_back(i);
                for (size_t k = i + 1; k < nx; ++k) {   // marks between the lam and the alef follow the ligature
                    out.push_back(in[k]);
                    if (owner) owner->push_back(k);
                }
                i = nx;   // the alef is in the ligature
                continue;
            }
        }
        const bool to_next = l->join == kDual && nx < n && joins_prev(in[nx]);
        int k = 0;
        if (l->join == kDual) k = to_prev && to_next ? 3 : to_prev ? 1 : to_next ? 2 : 0;
        else if (l->join == kRight) k = to_prev ? 1 : 0;
        out.push_back(l->form + k);
        if (owner) owner->push_back(i);
    }
    return out;
}

namespace {
std::u32string layout(std::u32string_view logical, const Options& o, std::vector<size_t>* caret, bool shaped);
}

std::u32string to_display(std::u32string_view logical, const Options& o, std::vector<size_t>* caret) {
    return layout(logical, o, caret, true);
}

namespace {
std::u32string layout(std::u32string_view logical, const Options& o, std::vector<size_t>* caret, bool shaped) {
    std::vector<size_t> owner;
    std::u32string s;
    if (shaped) {
        s = shape(logical, &owner);
    } else {
        s = logical;
        owner.resize(s.size());
        for (size_t i = 0; i < s.size(); ++i) owner[i] = i;
    }
    const size_t n = s.size();
    if (caret) caret->assign(logical.size() + 1, 0);
    if (!has_rtl(s)) {
        if (caret)
            for (size_t i = 0; i <= logical.size(); ++i) (*caret)[i] = i;
        return std::u32string(logical);
    }

    // types; a printf conversion is one left-to-right unit
    std::vector<Type> t(n);
    for (size_t i = 0; i < n; ++i) t[i] = classify(s[i]);
    if (o.printf_tokens)
        for (const auto& [a, b] : conversions(s))
            for (size_t i = a; i < b; ++i) t[i] = L;
    const int base = 1;   // a line with right-to-left text reads right to left
    const std::vector<Type> orig = t;

    // W1: a mark takes the type of what it sits on
    for (size_t i = 0; i < n; ++i)
        if (t[i] == NSM) t[i] = i ? t[i - 1] : R;
    // W2: a European number after an Arabic letter reads as an Arabic number
    {
        Type last = R;
        for (size_t i = 0; i < n; ++i) {
            if (t[i] == L || t[i] == R) last = t[i];
            else if (t[i] == EN && last == R) t[i] = AN;
        }
    }
    // W4: one separator between two numbers of a kind joins them
    for (size_t i = 1; i + 1 < n; ++i) {
        if (t[i] == ES && t[i - 1] == EN && t[i + 1] == EN) t[i] = EN;
        else if (t[i] == CS && t[i - 1] == t[i + 1] && (t[i - 1] == EN || t[i - 1] == AN)) t[i] = t[i - 1];
    }
    // W5: terminators next to a European number belong to it
    for (size_t i = 0; i < n; ++i) {
        if (t[i] != ET) continue;
        size_t j = i;
        while (j < n && t[j] == ET) ++j;
        const bool num = (i > 0 && t[i - 1] == EN) || (j < n && t[j] == EN);
        for (size_t k = i; k < j; ++k) t[k] = num ? EN : ON;
        i = j - 1;
    }
    // W6: the separators left are neutral
    for (Type& x : t)
        if (x == ES || x == CS || x == ET) x = ON;
    // W7: a European number after a left-to-right letter is left to right
    {
        Type last = base ? R : L;
        for (size_t i = 0; i < n; ++i) {
            if (t[i] == L || t[i] == R) last = t[i];
            else if (t[i] == EN && last == L) t[i] = L;
        }
    }
    // N1 / N2: neutrals between two of one direction take it (numbers count as right to left), else the line's
    auto dir = [](Type x) { return x == L ? L : R; };
    for (size_t i = 0; i < n; ++i) {
        if (t[i] != WS && t[i] != ON) continue;
        size_t j = i;
        while (j < n && (t[j] == WS || t[j] == ON)) ++j;
        const Type before = i ? dir(t[i - 1]) : (base ? R : L);
        const Type after = j < n ? dir(t[j]) : (base ? R : L);
        const Type d = before == after ? before : (base ? R : L);
        for (size_t k = i; k < j; ++k) t[k] = d;
        i = j - 1;
    }
    // levels (I1 / I2), trailing white space at the line's level (L1)
    std::vector<int> lv(n);
    for (size_t i = 0; i < n; ++i) lv[i] = t[i] == R ? 1 : 2;
    for (size_t i = n; i > 0 && (orig[i - 1] == WS); --i) lv[i - 1] = base;

    // L2: reverse every run at each level down to the lowest odd one
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    const int top = *std::max_element(lv.begin(), lv.end());
    for (int level = top; level >= 1; --level) {
        for (size_t i = 0; i < n;) {
            if (lv[order[i]] < level) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < n && lv[order[j]] >= level) ++j;
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
        }
    }
    // a reversed mark would come before its letter: put it back after (ImGui draws a mark over what came before it)
    for (size_t i = 0; i < n; ++i) {
        if (!is_mark(s[order[i]]) || !(lv[order[i]] & 1)) continue;
        size_t j = i;
        while (j < n && is_mark(s[order[j]])) ++j;
        if (j < n) {
            std::rotate(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(j),
                        order.begin() + static_cast<std::ptrdiff_t>(j + 1));
            i = j;
        }
    }

    std::u32string out(n, 0);
    std::vector<size_t> pos(n);   // shaped index -> display index
    for (size_t v = 0; v < n; ++v) {
        const size_t k = order[v];
        out[v] = (lv[k] & 1) ? mirror(s[k]) : s[k];
        pos[k] = v;
    }
    if (caret) {
        // the shaped character that holds each logical one
        std::vector<size_t> held(logical.size(), 0);
        for (size_t k = 0; k < n; ++k) {
            const size_t end = k + 1 < n ? owner[k + 1] : logical.size();
            for (size_t i = owner[k]; i < end; ++i) held[i] = k;
        }
        const size_t m = logical.size();
        for (size_t p = 0; p <= m; ++p) {
            if (m == 0) break;
            if (p < m) {
                const size_t k = held[p];
                (*caret)[p] = (lv[k] & 1) ? pos[k] + 1 : pos[k];   // before a right-to-left letter = at its right edge
            } else {
                const size_t k = held[m - 1];
                (*caret)[p] = (lv[k] & 1) ? pos[k] : pos[k] + 1;
            }
        }
    }
    return out;
}
}  // namespace

std::string to_display_utf8(const std::string& logical, const Options& o) {
    const std::u32string u = utf8_to_u32(logical);
    if (!has_rtl(u) || is_display_order(u)) return logical;
    std::u32string out;
    size_t a = 0;
    while (true) {
        const size_t b = u.find(U'\n', a);
        out += to_display(std::u32string_view(u).substr(a, b == std::u32string::npos ? std::u32string::npos : b - a), o);
        if (b == std::u32string::npos) break;
        out.push_back(U'\n');
        a = b + 1;
    }
    return u32_to_utf8(out);
}

std::u32string from_display(std::u32string_view display) {
    std::u32string plain;
    const auto& m = plain_of();
    for (const char32_t c : display) {
        const auto it = m.find(c);
        if (it == m.end()) {
            plain.push_back(c);
        } else if (it->second.size() == 2) {
            plain.push_back(it->second[1]);   // display order: the alef is drawn left of the lam
            plain.push_back(it->second[0]);
        } else {
            plain += it->second;
        }
    }
    // laying the plain letters out again (without forming letters) undoes the order
    return layout(plain, {}, nullptr, false);
}

std::u32string utf8_to_u32(std::string_view s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp;
        int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; len = 4; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + static_cast<size_t>(len) > s.size()) { out.push_back(0xFFFD); break; }
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            const unsigned char d = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
            if ((d >> 6) != 2) { ok = false; break; }
            cp = (cp << 6) | (d & 0x3F);
        }
        if (!ok) { out.push_back(0xFFFD); ++i; continue; }
        out.push_back(cp);
        i += static_cast<size_t>(len);
    }
    return out;
}

std::string u32_to_utf8(std::u32string_view s) {
    std::string out;
    for (const char32_t c : s) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(static_cast<char>(0xC0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

}  // namespace nxrt::rtl
