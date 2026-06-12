#include "assets/ModelAsset.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

void setError(std::string* error, const std::string& msg) {
    if (error) *error = msg;
}

std::string cgltfError(cgltf_result result) {
    switch (result) {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid json";
        case cgltf_result_invalid_gltf: return "invalid gltf";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "io error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "legacy gltf";
    }
    return "unknown error";
}

bool readAccessorVec2(const cgltf_accessor* accessor, cgltf_size index, glm::vec2& out) {
    cgltf_float values[2] = {};
    if (!accessor || accessor->type != cgltf_type_vec2) return false;
    if (!cgltf_accessor_read_float(accessor, index, values, 2)) return false;
    out = {values[0], values[1]};
    return true;
}

bool readAccessorVec3(const cgltf_accessor* accessor, cgltf_size index, glm::vec3& out) {
    cgltf_float values[3] = {};
    if (!accessor || accessor->type != cgltf_type_vec3) return false;
    if (!cgltf_accessor_read_float(accessor, index, values, 3)) return false;
    out = {values[0], values[1], values[2]};
    return true;
}

const cgltf_accessor* attributeAccessor(const cgltf_primitive& primitive,
                                        cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        if (primitive.attributes[i].type == type)
            return primitive.attributes[i].data;
    }
    return nullptr;
}

void includeBounds(ModelAsset& asset, const glm::vec3& p, bool& hasBounds) {
    if (!hasBounds) {
        asset.boundsMin = asset.boundsMax = p;
        hasBounds = true;
        return;
    }
    asset.boundsMin = glm::min(asset.boundsMin, p);
    asset.boundsMax = glm::max(asset.boundsMax, p);
}

bool loadImage(const std::filesystem::path& modelDir,
               const cgltf_image& image,
               ModelImage& out,
               std::string* error) {
    out.uri = image.uri ? image.uri : "";
    unsigned char* data = nullptr;
    int width = 0, height = 0, channels = 0;
    if (image.uri && std::strlen(image.uri) > 0) {
        out.sourcePath = modelDir / image.uri;
        data = stbi_load(out.sourcePath.string().c_str(), &width, &height, &channels, 4);
        out.channels = 4;
    } else if (image.buffer_view && image.buffer_view->buffer &&
               image.buffer_view->buffer->data) {
        const unsigned char* bytes =
            static_cast<const unsigned char*>(image.buffer_view->buffer->data) +
            image.buffer_view->offset;
        data = stbi_load_from_memory(bytes, int(image.buffer_view->size),
                                     &width, &height, &channels, 4);
        out.channels = 4;
    } else {
        setError(error, "image has no URI or embedded data");
        return false;
    }
    if (!data) {
        setError(error, std::string("failed to load image: ") + stbi_failure_reason());
        return false;
    }
    out.width = width;
    out.height = height;
    if (out.channels == 0) out.channels = channels;
    size_t bytes = size_t(width) * size_t(height) * 4u;
    out.pixels.assign(data, data + bytes);
    stbi_image_free(data);
    return true;
}

bool appendPrimitive(ModelAsset& asset,
                     const cgltf_data& data,
                     const cgltf_node& node,
                     const cgltf_mesh& mesh,
                     const cgltf_primitive& primitive,
                     bool& hasBounds,
                     std::string* error) {
    if (primitive.type != cgltf_primitive_type_triangles) {
        setError(error, "only triangle primitives are supported");
        return false;
    }
    const cgltf_accessor* positions = attributeAccessor(primitive, cgltf_attribute_type_position);
    if (!positions) {
        setError(error, "primitive missing POSITION attribute");
        return false;
    }
    const cgltf_accessor* normals = attributeAccessor(primitive, cgltf_attribute_type_normal);
    const cgltf_accessor* uvs = attributeAccessor(primitive, cgltf_attribute_type_texcoord);

    float nodeMatrix[16];
    cgltf_node_transform_world(&node, nodeMatrix);
    glm::mat4 transform = glm::make_mat4(nodeMatrix);
    glm::mat3 normalTransform = glm::inverseTranspose(glm::mat3(transform));

    uint32_t firstIndex = uint32_t(asset.indices.size());
    uint32_t vertexBase = uint32_t(asset.vertices.size());
    for (cgltf_size i = 0; i < positions->count; ++i) {
        glm::vec3 pos;
        if (!readAccessorVec3(positions, i, pos)) {
            setError(error, "failed to read POSITION accessor");
            return false;
        }
        ModelVertex v;
        v.pos = glm::vec3(transform * glm::vec4(pos, 1.0f)) * asset.scale;
        if (normals) {
            glm::vec3 n;
            if (readAccessorVec3(normals, i, n))
                v.normal = glm::normalize(normalTransform * n);
        }
        if (uvs) readAccessorVec2(uvs, i, v.uv);
        includeBounds(asset, v.pos, hasBounds);
        asset.vertices.push_back(v);
    }

    if (primitive.indices) {
        for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
            asset.indices.push_back(vertexBase +
                uint32_t(cgltf_accessor_read_index(primitive.indices, i)));
        }
    } else {
        for (cgltf_size i = 0; i < positions->count; ++i)
            asset.indices.push_back(vertexBase + uint32_t(i));
    }

    ModelPart part;
    part.firstIndex = firstIndex;
    part.indexCount = uint32_t(asset.indices.size()) - firstIndex;
    part.materialIndex = primitive.material ? int(primitive.material - data.materials) : -1;
    part.name = mesh.name ? mesh.name : "mesh";
    asset.parts.push_back(part);
    return true;
}

bool appendNode(ModelAsset& asset,
                const cgltf_data& data,
                const cgltf_node& node,
                bool& hasBounds,
                std::string* error) {
    if (node.mesh) {
        for (cgltf_size i = 0; i < node.mesh->primitives_count; ++i) {
            if (!appendPrimitive(asset, data, node, *node.mesh, node.mesh->primitives[i],
                                 hasBounds, error))
                return false;
        }
    }
    for (cgltf_size i = 0; i < node.children_count; ++i) {
        if (!appendNode(asset, data, *node.children[i], hasBounds, error))
            return false;
    }
    return true;
}

}

bool loadModelAsset(const std::filesystem::path& manifestDir,
                    const ModelManifestEntry& entry,
                    ModelAsset& out,
                    std::string* error) {
    std::filesystem::path modelPath = manifestDir / entry.path;
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, modelPath.string().c_str(), &data);
    if (result != cgltf_result_success) {
        setError(error, "failed to parse glTF/GLB: " + cgltfError(result));
        return false;
    }
    struct DataGuard {
        cgltf_data* data = nullptr;
        ~DataGuard() { if (data) cgltf_free(data); }
    } guard{data};

    result = cgltf_load_buffers(&options, data, modelPath.string().c_str());
    if (result != cgltf_result_success) {
        setError(error, "failed to load glTF buffers: " + cgltfError(result));
        return false;
    }

    ModelAsset asset;
    asset.id = entry.id;
    asset.name = entry.name.empty() ? entry.id : entry.name;
    asset.scale = entry.scale;

    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& material = data->materials[i];
        ModelMaterial outMat;
        outMat.name = material.name ? material.name : "";
        if (material.has_pbr_metallic_roughness) {
            const cgltf_texture_view& view =
                material.pbr_metallic_roughness.base_color_texture;
            if (view.texture && view.texture->image)
                outMat.imageIndex = int(view.texture->image - data->images);
        }
        asset.materials.push_back(outMat);
    }

    std::filesystem::path modelDir = modelPath.parent_path();
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        ModelImage image;
        if (!loadImage(modelDir, data->images[i], image, error)) return false;
        // Wrap modes live on the sampler of the texture referencing the
        // image; absent a sampler the glTF default is repeat.
        for (cgltf_size t = 0; t < data->textures_count; ++t) {
            const cgltf_texture& texture = data->textures[t];
            if (texture.image != &data->images[i]) continue;
            if (texture.sampler) {
                image.wrapS = texture.sampler->wrap_s;
                image.wrapT = texture.sampler->wrap_t;
            }
            break;
        }
        asset.images.push_back(std::move(image));
    }

    const cgltf_scene* scene = data->scene;
    if (!scene && data->scenes_count > 0) scene = &data->scenes[0];
    if (!scene) {
        setError(error, "model has no scene");
        return false;
    }

    bool hasBounds = false;
    for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
        if (!appendNode(asset, *data, *scene->nodes[i], hasBounds, error)) return false;
    }
    if (asset.vertices.empty() || asset.indices.empty() || asset.parts.empty()) {
        setError(error, "model has no renderable mesh data");
        return false;
    }

    out = std::move(asset);
    return true;
}
