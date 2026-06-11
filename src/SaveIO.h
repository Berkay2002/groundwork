#pragma once
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Crash-safe save: stream into "<path>.tmp", then atomically replace the real
// file. A crash mid-write leaves at worst a stale .tmp behind, never a
// truncated real save. Windows needs MoveFileEx because filesystem::rename
// cannot reliably overwrite an existing destination there.
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
#if defined(_WIN32)
    if (!MoveFileExA(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);
        return false;
    }
#endif
    return true;
}
