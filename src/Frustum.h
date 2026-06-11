#pragma once
#include <glm/glm.hpp>

// View frustum planes extracted from a view-projection matrix
// (Gribb-Hartmann method), for AABB visibility tests.
struct Frustum {
    glm::vec4 planes[6];

    static Frustum fromMatrix(const glm::mat4& m) {
        Frustum f;
        glm::mat4 t = glm::transpose(m);
        f.planes[0] = t[3] + t[0]; // left
        f.planes[1] = t[3] - t[0]; // right
        f.planes[2] = t[3] + t[1]; // bottom
        f.planes[3] = t[3] - t[1]; // top
        f.planes[4] = t[3] + t[2]; // near
        f.planes[5] = t[3] - t[2]; // far
        return f;
    }

    bool intersectsAABB(const glm::vec3& mn, const glm::vec3& mx) const {
        for (const auto& p : planes) {
            // Most positive vertex along the plane normal.
            glm::vec3 v(p.x > 0 ? mx.x : mn.x,
                        p.y > 0 ? mx.y : mn.y,
                        p.z > 0 ? mx.z : mn.z);
            if (glm::dot(glm::vec3(p), v) + p.w < 0) return false;
        }
        return true;
    }
};
