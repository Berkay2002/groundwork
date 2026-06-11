#pragma once
#include <string>

// Key-name mapping for the key_* rebinding entries in settings.cfg.
// Values are GLFW keycodes written as plain ints (GLFW's keycodes are a
// stable public API: printable keys are their ASCII uppercase value) so
// Settings stays GLFW-free and headless-testable.

namespace keys {

constexpr int SPACE = 32, APOSTROPHE = 39, COMMA = 44, MINUS = 45,
              PERIOD = 46, SLASH = 47, SEMICOLON = 59, EQUAL = 61,
              TAB = 258, CAPSLOCK = 280,
              LSHIFT = 340, LCTRL = 341, LALT = 342,
              RSHIFT = 344, RCTRL = 345, RALT = 346;

struct Named { const char* name; int code; };
constexpr Named NAMED[] = {
    {"SPACE", SPACE},   {"TAB", TAB},       {"CAPSLOCK", CAPSLOCK},
    {"LSHIFT", LSHIFT}, {"RSHIFT", RSHIFT}, {"LCTRL", LCTRL},
    {"RCTRL", RCTRL},   {"LALT", LALT},     {"RALT", RALT},
    {"APOSTROPHE", APOSTROPHE}, {"COMMA", COMMA},   {"MINUS", MINUS},
    {"PERIOD", PERIOD},         {"SLASH", SLASH},   {"SEMICOLON", SEMICOLON},
    {"EQUAL", EQUAL},
};

// "W", "SPACE", "LSHIFT", ... -> keycode, or -1 if unrecognized.
inline int fromName(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (s.size() == 1 &&
        ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= '0' && s[0] <= '9')))
        return s[0];
    for (const Named& n : NAMED)
        if (s == n.name) return n.code;
    return -1;
}

inline std::string toName(int code) {
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9'))
        return std::string(1, char(code));
    for (const Named& n : NAMED)
        if (code == n.code) return n.name;
    return "?";
}

} // namespace keys
