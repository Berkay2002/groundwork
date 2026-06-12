#include "render/ModelRenderer.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

namespace {

const char* MODEL_VS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec2 vUV;
out float vShade;
void main() {
    vec3 n = normalize(mat3(uModel) * aNormal);
    vUV = aUV;
    vShade = clamp(0.62 + n.y * 0.28 + abs(n.x) * 0.06 + abs(n.z) * 0.04, 0.45, 1.0);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* MODEL_FS = R"(
#version 330 core
in vec2 vUV;
in float vShade;
uniform sampler2D uTex;
uniform float uLight;
out vec4 FragColor;
void main() {
    vec4 c = texture(uTex, vUV);
    if (c.a < 0.1) discard;
    FragColor = vec4(c.rgb * vShade * uLight, c.a);
}
)";

struct UploadVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

// glTF wrap values are GL enums already; reject anything unexpected.
GLint sanitizeWrap(int wrap) {
    switch (wrap) {
        case GL_REPEAT:
        case GL_CLAMP_TO_EDGE:
        case GL_MIRRORED_REPEAT:
            return wrap;
    }
    return GL_REPEAT;
}

unsigned uploadTexture(const ModelImage& image) {
    unsigned tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sanitizeWrap(image.wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sanitizeWrap(image.wrapT));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
    return tex;
}

} // namespace

struct ModelRenderer::UploadedModel {
    unsigned vao = 0;
    unsigned vbo = 0;
    unsigned ebo = 0;
    std::vector<ModelPart> parts;
    std::vector<ModelMaterial> materials;
    std::vector<unsigned> textures;
    uint32_t indexCount = 0;

    ~UploadedModel() {
        if (!textures.empty()) glDeleteTextures(GLsizei(textures.size()), textures.data());
        glDeleteBuffers(1, &ebo);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }
};

ModelRenderer::ModelRenderer() : shader_(MODEL_VS, MODEL_FS) {
    locMVP_ = shader_.loc("uMVP");
    locModel_ = shader_.loc("uModel");
    locLight_ = shader_.loc("uLight");
    shader_.use();
    shader_.setInt("uTex", 0);

    const unsigned char white[] = {255, 255, 255, 255};
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
}

ModelRenderer::~ModelRenderer() {
    uploaded_.clear();
    glDeleteTextures(1, &whiteTex_);
}

const ModelRenderer::UploadedModel& ModelRenderer::upload(const ModelAsset& model) {
    auto it = uploaded_.find(model.id);
    if (it != uploaded_.end()) return *it->second;

    auto uploaded = std::make_unique<UploadedModel>();
    uploaded->parts = model.parts;
    uploaded->materials = model.materials;
    uploaded->indexCount = uint32_t(model.indices.size());

    std::vector<UploadVertex> vertices;
    vertices.reserve(model.vertices.size());
    for (const ModelVertex& v : model.vertices)
        vertices.push_back({v.pos, v.normal, v.uv});

    glGenVertexArrays(1, &uploaded->vao);
    glGenBuffers(1, &uploaded->vbo);
    glGenBuffers(1, &uploaded->ebo);
    glBindVertexArray(uploaded->vao);
    glBindBuffer(GL_ARRAY_BUFFER, uploaded->vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(UploadVertex),
                 vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, uploaded->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, model.indices.size() * sizeof(uint32_t),
                 model.indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = sizeof(UploadVertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(UploadVertex, pos));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(UploadVertex, normal));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(UploadVertex, uv));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    uploaded->textures.reserve(model.images.size());
    for (const ModelImage& image : model.images)
        uploaded->textures.push_back(uploadTexture(image));

    const UploadedModel* ptr = uploaded.get();
    uploaded_[model.id] = std::move(uploaded);
    return *ptr;
}

void ModelRenderer::draw(const ModelAsset& model, const glm::mat4& viewProj,
                         const glm::mat4& transform, float light) {
    const UploadedModel& uploaded = upload(model);
    if (uploaded.indexCount == 0) return;

    shader_.use();
    glm::mat4 mvp = viewProj * transform;
    glUniformMatrix4fv(locMVP_, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(locModel_, 1, GL_FALSE, glm::value_ptr(transform));
    glUniform1f(locLight_, std::max(0.0f, light));
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(uploaded.vao);
    glDisable(GL_CULL_FACE);

    for (const ModelPart& part : uploaded.parts) {
        unsigned tex = whiteTex_;
        if (part.materialIndex >= 0 &&
            part.materialIndex < int(uploaded.materials.size())) {
            int image = uploaded.materials[size_t(part.materialIndex)].imageIndex;
            if (image >= 0 && image < int(uploaded.textures.size()))
                tex = uploaded.textures[size_t(image)];
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glDrawElements(GL_TRIANGLES, GLsizei(part.indexCount), GL_UNSIGNED_INT,
                       (void*)(size_t(part.firstIndex) * sizeof(uint32_t)));
    }

    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}
