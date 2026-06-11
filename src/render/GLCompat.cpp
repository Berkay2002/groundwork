#include "render/GLCompat.h"

#if defined(_WIN32)
#include <cstdint>

PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLTEXIMAGE3DPROC glTexImage3D = nullptr;
PFNGLUNIFORM1FPROC glUniform1f = nullptr;
PFNGLUNIFORM1FVPROC glUniform1fv = nullptr;
PFNGLUNIFORM1IPROC glUniform1i = nullptr;
PFNGLUNIFORM2FPROC glUniform2f = nullptr;
PFNGLUNIFORM3FPROC glUniform3f = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLVERTEXATTRIBIPOINTERPROC glVertexAttribIPointer = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;

namespace {
const char* missing = nullptr;

void* loadProc(const char* name) {
    PROC p = wglGetProcAddress(name);
    auto raw = reinterpret_cast<std::uintptr_t>(p);
    if (raw > 3 && raw != UINTPTR_MAX) return reinterpret_cast<void*>(p);
    static HMODULE opengl = LoadLibraryA("opengl32.dll");
    return opengl ? reinterpret_cast<void*>(GetProcAddress(opengl, name)) : nullptr;
}

#define LOAD_GL_PROC(name)                                      \
    do {                                                        \
        name = reinterpret_cast<decltype(name)>(loadProc(#name)); \
        if (!name) { missing = #name; return false; }           \
    } while (false)
} // namespace

namespace glcompat {

bool load() {
    missing = nullptr;
    LOAD_GL_PROC(glActiveTexture);
    LOAD_GL_PROC(glAttachShader);
    LOAD_GL_PROC(glBindBuffer);
    LOAD_GL_PROC(glBindVertexArray);
    LOAD_GL_PROC(glBufferData);
    LOAD_GL_PROC(glCompileShader);
    LOAD_GL_PROC(glCreateProgram);
    LOAD_GL_PROC(glCreateShader);
    LOAD_GL_PROC(glDeleteBuffers);
    LOAD_GL_PROC(glDeleteProgram);
    LOAD_GL_PROC(glDeleteShader);
    LOAD_GL_PROC(glDeleteVertexArrays);
    LOAD_GL_PROC(glEnableVertexAttribArray);
    LOAD_GL_PROC(glGenBuffers);
    LOAD_GL_PROC(glGenVertexArrays);
    LOAD_GL_PROC(glGetProgramInfoLog);
    LOAD_GL_PROC(glGetProgramiv);
    LOAD_GL_PROC(glGetShaderInfoLog);
    LOAD_GL_PROC(glGetShaderiv);
    LOAD_GL_PROC(glGetUniformLocation);
    LOAD_GL_PROC(glLinkProgram);
    LOAD_GL_PROC(glShaderSource);
    LOAD_GL_PROC(glTexImage3D);
    LOAD_GL_PROC(glUniform1f);
    LOAD_GL_PROC(glUniform1fv);
    LOAD_GL_PROC(glUniform1i);
    LOAD_GL_PROC(glUniform2f);
    LOAD_GL_PROC(glUniform3f);
    LOAD_GL_PROC(glUniformMatrix4fv);
    LOAD_GL_PROC(glUseProgram);
    LOAD_GL_PROC(glVertexAttribIPointer);
    LOAD_GL_PROC(glVertexAttribPointer);
    return true;
}

const char* missingFunction() { return missing; }

} // namespace glcompat

#else

namespace glcompat {

bool load() { return true; }
const char* missingFunction() { return nullptr; }

} // namespace glcompat

#endif
