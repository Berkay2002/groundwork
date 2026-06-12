#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "assets/AssetManifest.h"

struct ModelVertex {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
};

struct ModelMaterial {
    std::string name;
    int imageIndex = -1;
};

struct ModelPart {
    std::string name;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int materialIndex = -1;
};

struct ModelImage {
    std::string uri;
    std::filesystem::path sourcePath;
    int width = 0;
    int height = 0;
    int channels = 0;
    // glTF sampler wrap modes (GL enum values; the glTF default is repeat,
    // which Kenney models rely on — their UVs run outside [0,1]).
    int wrapS = 10497; // GL_REPEAT
    int wrapT = 10497;
    std::vector<unsigned char> pixels;
};

struct ModelAsset {
    std::string id;
    std::string name;
    float scale = 1.0f;
    float forwardYaw = 0.0f; // radians; manifest forwardYawDeg converted once
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<ModelPart> parts;
    std::vector<ModelMaterial> materials;
    std::vector<ModelImage> images;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

bool loadModelAsset(const std::filesystem::path& manifestDir,
                    const ModelManifestEntry& entry,
                    ModelAsset& out,
                    std::string* error = nullptr);
