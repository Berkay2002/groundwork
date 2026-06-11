#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#include <cstddef>

#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RGB8
#define GL_RGB8 0x8051
#endif
#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif

using PFNGLACTIVETEXTUREPROC = void (APIENTRYP)(GLenum texture);
using PFNGLATTACHSHADERPROC = void (APIENTRYP)(GLuint program, GLuint shader);
using PFNGLBINDBUFFERPROC = void (APIENTRYP)(GLenum target, GLuint buffer);
using PFNGLBINDVERTEXARRAYPROC = void (APIENTRYP)(GLuint array);
using PFNGLBUFFERDATAPROC = void (APIENTRYP)(GLenum target, GLsizeiptr size,
                                             const void* data, GLenum usage);
using PFNGLCOMPILESHADERPROC = void (APIENTRYP)(GLuint shader);
using PFNGLCREATEPROGRAMPROC = GLuint (APIENTRYP)();
using PFNGLCREATESHADERPROC = GLuint (APIENTRYP)(GLenum type);
using PFNGLDELETEBUFFERSPROC = void (APIENTRYP)(GLsizei n, const GLuint* buffers);
using PFNGLDELETEPROGRAMPROC = void (APIENTRYP)(GLuint program);
using PFNGLDELETESHADERPROC = void (APIENTRYP)(GLuint shader);
using PFNGLDELETEVERTEXARRAYSPROC = void (APIENTRYP)(GLsizei n, const GLuint* arrays);
using PFNGLENABLEVERTEXATTRIBARRAYPROC = void (APIENTRYP)(GLuint index);
using PFNGLGENBUFFERSPROC = void (APIENTRYP)(GLsizei n, GLuint* buffers);
using PFNGLGENVERTEXARRAYSPROC = void (APIENTRYP)(GLsizei n, GLuint* arrays);
using PFNGLGETPROGRAMINFOLOGPROC = void (APIENTRYP)(GLuint program, GLsizei bufSize,
                                                    GLsizei* length, GLchar* infoLog);
using PFNGLGETPROGRAMIVPROC = void (APIENTRYP)(GLuint program, GLenum pname, GLint* params);
using PFNGLGETSHADERINFOLOGPROC = void (APIENTRYP)(GLuint shader, GLsizei bufSize,
                                                   GLsizei* length, GLchar* infoLog);
using PFNGLGETSHADERIVPROC = void (APIENTRYP)(GLuint shader, GLenum pname, GLint* params);
using PFNGLGETUNIFORMLOCATIONPROC = GLint (APIENTRYP)(GLuint program, const GLchar* name);
using PFNGLLINKPROGRAMPROC = void (APIENTRYP)(GLuint program);
using PFNGLSHADERSOURCEPROC = void (APIENTRYP)(GLuint shader, GLsizei count,
                                               const GLchar* const* string,
                                               const GLint* length);
using PFNGLTEXIMAGE3DPROC = void (APIENTRYP)(GLenum target, GLint level,
                                             GLint internalformat, GLsizei width,
                                             GLsizei height, GLsizei depth,
                                             GLint border, GLenum format,
                                             GLenum type, const void* pixels);
using PFNGLUNIFORM1FPROC = void (APIENTRYP)(GLint location, GLfloat v0);
using PFNGLUNIFORM1FVPROC = void (APIENTRYP)(GLint location, GLsizei count,
                                             const GLfloat* value);
using PFNGLUNIFORM1IPROC = void (APIENTRYP)(GLint location, GLint v0);
using PFNGLUNIFORM2FPROC = void (APIENTRYP)(GLint location, GLfloat v0, GLfloat v1);
using PFNGLUNIFORM3FPROC = void (APIENTRYP)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
using PFNGLUNIFORMMATRIX4FVPROC = void (APIENTRYP)(GLint location, GLsizei count,
                                                   GLboolean transpose,
                                                   const GLfloat* value);
using PFNGLUSEPROGRAMPROC = void (APIENTRYP)(GLuint program);
using PFNGLVERTEXATTRIBIPOINTERPROC = void (APIENTRYP)(GLuint index, GLint size,
                                                       GLenum type, GLsizei stride,
                                                       const void* pointer);
using PFNGLVERTEXATTRIBPOINTERPROC = void (APIENTRYP)(GLuint index, GLint size,
                                                      GLenum type, GLboolean normalized,
                                                      GLsizei stride,
                                                      const void* pointer);
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

namespace glcompat {

// Load renderer-used OpenGL entry points after a context is current.
// Linux builds keep using Mesa's GL_GLEXT_PROTOTYPES path, so this is a no-op
// there. Windows needs explicit loading because opengl32 only exports OpenGL
// 1.1 symbols.
bool load();
const char* missingFunction();

} // namespace glcompat

#if defined(_WIN32)
extern PFNGLACTIVETEXTUREPROC glActiveTexture;
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLDELETESHADERPROC glDeleteShader;
extern PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
extern PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
extern PFNGLGETSHADERIVPROC glGetShaderiv;
extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLLINKPROGRAMPROC glLinkProgram;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLTEXIMAGE3DPROC glTexImage3D;
extern PFNGLUNIFORM1FPROC glUniform1f;
extern PFNGLUNIFORM1FVPROC glUniform1fv;
extern PFNGLUNIFORM1IPROC glUniform1i;
extern PFNGLUNIFORM2FPROC glUniform2f;
extern PFNGLUNIFORM3FPROC glUniform3f;
extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
extern PFNGLUSEPROGRAMPROC glUseProgram;
extern PFNGLVERTEXATTRIBIPOINTERPROC glVertexAttribIPointer;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
#endif
