#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct ModelManifestEntry {
    std::string id;
    std::string path;
    std::string name;
    float scale = 1.0f;
};

struct AssetManifest {
    int version = 0;
    std::vector<ModelManifestEntry> models;

    const ModelManifestEntry* modelById(const std::string& id) const;
};

bool loadAssetManifest(const std::filesystem::path& path,
                       AssetManifest& out,
                       std::string* error = nullptr);
