#pragma once
#include <filesystem>
#include <fstream>
#include <string>

// Crash-safe save: stream into "<path>.tmp", then rename() over the real
// file. rename() within a directory is atomic on POSIX, so a crash mid-write
// leaves at worst a stale .tmp behind — never a truncated real save.
template <typename Writer>
bool atomicSave(const std::string& path, Writer&& write) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        write(f);
        f.flush();
        if (!f) {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);
        return false;
    }
    return true;
}
