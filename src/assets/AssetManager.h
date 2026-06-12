#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "assets/AssetManifest.h"
#include "assets/ModelAsset.h"

class AssetManager {
public:
    bool loadManifest(const std::filesystem::path& path,
                      std::string* error = nullptr);
    const ModelAsset* model(const std::string& id,
                            std::string* error = nullptr);

    const AssetManifest& manifest() const { return manifest_; }

private:
    AssetManifest manifest_;
    std::filesystem::path manifestDir_;
    std::unordered_map<std::string, std::unique_ptr<ModelAsset>> modelCache_;
};
