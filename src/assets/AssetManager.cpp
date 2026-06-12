#include "assets/AssetManager.h"

namespace {
void setError(std::string* error, const std::string& msg) {
    if (error) *error = msg;
}
}

bool AssetManager::loadManifest(const std::filesystem::path& path,
                                std::string* error) {
    AssetManifest loaded;
    if (!loadAssetManifest(path, loaded, error)) return false;
    manifest_ = std::move(loaded);
    manifestDir_ = path.parent_path();
    modelCache_.clear();
    return true;
}

const ModelAsset* AssetManager::model(const std::string& id,
                                      std::string* error) {
    auto cached = modelCache_.find(id);
    if (cached != modelCache_.end()) return cached->second.get();

    const ModelManifestEntry* entry = manifest_.modelById(id);
    if (!entry) {
        setError(error, "unknown model id: " + id);
        return nullptr;
    }
    auto loaded = std::make_unique<ModelAsset>();
    if (!loadModelAsset(manifestDir_, *entry, *loaded, error)) return nullptr;
    const ModelAsset* ptr = loaded.get();
    modelCache_[id] = std::move(loaded);
    return ptr;
}
