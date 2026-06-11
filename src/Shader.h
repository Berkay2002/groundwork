#pragma once
#include <GL/gl.h>
#include <GL/glext.h>
#include <cstdio>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Shader {
public:
    Shader(const char* vsSrc, const char* fsSrc) {
        GLuint vs = compile(GL_VERTEX_SHADER, vsSrc);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc);
        prog_ = glCreateProgram();
        glAttachShader(prog_, vs);
        glAttachShader(prog_, fs);
        glLinkProgram(prog_);
        GLint ok;
        glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetProgramInfoLog(prog_, sizeof(log), nullptr, log);
            std::fprintf(stderr, "shader link error: %s\n", log);
            std::exit(1);
        }
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    ~Shader() { if (prog_) glDeleteProgram(prog_); }
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void use() const { glUseProgram(prog_); }
    void setMat4(const char* name, const glm::mat4& m) const {
        glUniformMatrix4fv(glGetUniformLocation(prog_, name), 1, GL_FALSE, glm::value_ptr(m));
    }
    void setVec2(const char* name, float x, float y) const {
        glUniform2f(glGetUniformLocation(prog_, name), x, y);
    }
    void setVec3(const char* name, const glm::vec3& v) const {
        glUniform3f(glGetUniformLocation(prog_, name), v.x, v.y, v.z);
    }
    void setFloat(const char* name, float v) const {
        glUniform1f(glGetUniformLocation(prog_, name), v);
    }
    void setInt(const char* name, int v) const {
        glUniform1i(glGetUniformLocation(prog_, name), v);
    }
    // For uniforms set in hot per-draw loops (e.g. the per-chunk origin).
    int loc(const char* name) const { return glGetUniformLocation(prog_, name); }

private:
    GLuint prog_ = 0;
    static GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::fprintf(stderr, "shader compile error: %s\n", log);
            std::exit(1);
        }
        return s;
    }
};
