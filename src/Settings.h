#pragma once
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

struct Settings {
    float mouseSensitivity = 0.12f;
    float fov = 75.0f;
    int renderDistance = 6;
    bool vsync = true;

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
        return s;
    }

    void save(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return;
        f << "# Minecraft clone settings\n"
          << "mouse_sensitivity=" << mouseSensitivity << "\n"
          << "fov=" << fov << "\n"
          << "render_distance=" << renderDistance << "\n"
          << "vsync=" << (vsync ? 1 : 0) << "\n";
    }
};
