#pragma once
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "KeyBinds.h"

struct Settings {
    float mouseSensitivity = 0.12f;
    float fov = 75.0f;
    int renderDistance = 6;
    bool vsync = true;
    // Survival-ish mode: block drops, finite stacked placement, E inventory.
    // Off = creative (the original infinite-palette behavior).
    bool survival = false;
    float volume = 0.8f; // master sound volume, 0..1
    // Rebindable keys (key_* entries, names per KeyBinds.h). Esc, the
    // hotbar digits, and the mouse buttons are fixed.
    int keyForward = 'W', keyBack = 'S', keyLeft = 'A', keyRight = 'D';
    int keyJump = keys::SPACE, keySneak = keys::LSHIFT, keySprint = keys::LCTRL;
    int keyFly = 'F', keyInventory = 'E';

    static Settings load(const std::string& path) {
        Settings s;
        std::ifstream f(path);
        if (!f) {
            s.save(path); // write defaults so the user can discover the file
            return s;
        }
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            try {
                if (key == "mouse_sensitivity") s.mouseSensitivity = std::stof(val);
                else if (key == "fov") s.fov = std::stof(val);
                else if (key == "render_distance") s.renderDistance = std::stoi(val);
                else if (key == "vsync") s.vsync = std::stoi(val) != 0;
                else if (key == "survival") s.survival = std::stoi(val) != 0;
                else if (key == "volume") s.volume = std::stof(val);
                else if (key.rfind("key_", 0) == 0) {
                    int* slot = s.bindFor(key.substr(4));
                    int code = keys::fromName(val);
                    if (!slot)
                        std::fprintf(stderr, "settings: unknown binding '%s'\n", key.c_str());
                    else if (code < 0)
                        std::fprintf(stderr, "settings: unknown key name '%s' for %s\n",
                                     val.c_str(), key.c_str());
                    else *slot = code;
                }
                else std::fprintf(stderr, "settings: unknown key '%s'\n", key.c_str());
            } catch (...) {
                std::fprintf(stderr, "settings: bad value for '%s'\n", key.c_str());
            }
        }
        // Clamp to sane ranges.
        if (s.fov < 30.0f) s.fov = 30.0f;
        if (s.fov > 110.0f) s.fov = 110.0f;
        if (s.renderDistance < 2) s.renderDistance = 2;
        if (s.renderDistance > 16) s.renderDistance = 16;
        if (s.mouseSensitivity <= 0.0f) s.mouseSensitivity = 0.12f;
        if (s.volume < 0.0f) s.volume = 0.0f;
        if (s.volume > 1.0f) s.volume = 1.0f;
        return s;
    }

    void save(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return;
        f << "# Groundwork settings\n"
          << "mouse_sensitivity=" << mouseSensitivity << "\n"
          << "fov=" << fov << "\n"
          << "render_distance=" << renderDistance << "\n"
          << "vsync=" << (vsync ? 1 : 0) << "\n"
          << "survival=" << (survival ? 1 : 0) << "\n"
          << "volume=" << volume << "\n"
          << "# key names: letters, digits, SPACE, TAB, LSHIFT, LCTRL, ...\n"
          << "key_forward=" << keys::toName(keyForward) << "\n"
          << "key_back=" << keys::toName(keyBack) << "\n"
          << "key_left=" << keys::toName(keyLeft) << "\n"
          << "key_right=" << keys::toName(keyRight) << "\n"
          << "key_jump=" << keys::toName(keyJump) << "\n"
          << "key_sneak=" << keys::toName(keySneak) << "\n"
          << "key_sprint=" << keys::toName(keySprint) << "\n"
          << "key_fly=" << keys::toName(keyFly) << "\n"
          << "key_inventory=" << keys::toName(keyInventory) << "\n";
    }

    // Maps the suffix of a key_* settings entry to its bind; null if unknown.
    int* bindFor(const std::string& action) {
        if (action == "forward") return &keyForward;
        if (action == "back") return &keyBack;
        if (action == "left") return &keyLeft;
        if (action == "right") return &keyRight;
        if (action == "jump") return &keyJump;
        if (action == "sneak") return &keySneak;
        if (action == "sprint") return &keySprint;
        if (action == "fly") return &keyFly;
        if (action == "inventory") return &keyInventory;
        return nullptr;
    }
};
