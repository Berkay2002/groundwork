#pragma once

#include "assets/ModelAsset.h"
#include "render/Shader.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ModelRenderer {
public:
    ModelRenderer();
    ~ModelRenderer();
    ModelRenderer(const ModelRenderer&) = delete;
    ModelRenderer& operator=(const ModelRenderer&) = delete;

    // `flash` (0..1) blends the lit color toward a hurt-red tint.
    void draw(const ModelAsset& model, const glm::mat4& viewProj,
              const glm::mat4& transform, float light, float flash = 0.0f);

private:
    struct UploadedModel;

    Shader shader_;
    int locMVP_ = -1;
    int locModel_ = -1;
    int locLight_ = -1;
    int locFlash_ = -1;
    std::unordered_map<std::string, std::unique_ptr<UploadedModel>> uploaded_;
    unsigned whiteTex_ = 0;

    const UploadedModel& upload(const ModelAsset& model);
};
