// The OpenGL functions the GPU renderer calls, loaded at run time (core profile 3.3, with
// clip control and texture barriers when the driver has them).
#pragma once
#include "gl/GL/glcorearb.h"

namespace glapi {

#define GLAPI_FUNCS(X) \
    X(PFNGLGETSTRINGPROC, GetString) X(PFNGLGETINTEGERVPROC, GetIntegerv) X(PFNGLGETERRORPROC, GetError) \
    X(PFNGLENABLEPROC, Enable) X(PFNGLDISABLEPROC, Disable) X(PFNGLVIEWPORTPROC, Viewport) X(PFNGLSCISSORPROC, Scissor) \
    X(PFNGLCLEARPROC, Clear) X(PFNGLCLEARCOLORPROC, ClearColor) X(PFNGLCLEARDEPTHPROC, ClearDepth) \
    X(PFNGLCOLORMASKPROC, ColorMask) X(PFNGLDEPTHMASKPROC, DepthMask) X(PFNGLDEPTHFUNCPROC, DepthFunc) \
    X(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate) X(PFNGLBLENDEQUATIONSEPARATEPROC, BlendEquationSeparate) \
    X(PFNGLBLENDCOLORPROC, BlendColor) X(PFNGLPIXELSTOREIPROC, PixelStorei) X(PFNGLREADPIXELSPROC, ReadPixels) \
    X(PFNGLFINISHPROC, Finish) X(PFNGLFLUSHPROC, Flush) X(PFNGLDRAWARRAYSPROC, DrawArrays) \
    X(PFNGLGENTEXTURESPROC, GenTextures) X(PFNGLDELETETEXTURESPROC, DeleteTextures) X(PFNGLBINDTEXTUREPROC, BindTexture) \
    X(PFNGLTEXIMAGE2DPROC, TexImage2D) X(PFNGLTEXSUBIMAGE2DPROC, TexSubImage2D) X(PFNGLTEXPARAMETERIPROC, TexParameteri) \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture) \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) X(PFNGLBLITFRAMEBUFFERPROC, BlitFramebuffer) \
    X(PFNGLDRAWBUFFERSPROC, DrawBuffers) X(PFNGLREADBUFFERPROC, ReadBuffer) \
    X(PFNGLGENBUFFERSPROC, GenBuffers) X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) X(PFNGLBINDBUFFERPROC, BindBuffer) \
    X(PFNGLBUFFERDATAPROC, BufferData) X(PFNGLBUFFERSUBDATAPROC, BufferSubData) \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) X(PFNGLVERTEXATTRIBIPOINTERPROC, VertexAttribIPointer) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) \
    X(PFNGLCREATESHADERPROC, CreateShader) X(PFNGLSHADERSOURCEPROC, ShaderSource) X(PFNGLCOMPILESHADERPROC, CompileShader) \
    X(PFNGLGETSHADERIVPROC, GetShaderiv) X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) X(PFNGLDELETESHADERPROC, DeleteShader) \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram) X(PFNGLATTACHSHADERPROC, AttachShader) X(PFNGLLINKPROGRAMPROC, LinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) X(PFNGLUSEPROGRAMPROC, UseProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) X(PFNGLUNIFORM1IPROC, Uniform1i) X(PFNGLUNIFORM1FPROC, Uniform1f) \
    X(PFNGLUNIFORM2FPROC, Uniform2f) X(PFNGLUNIFORM4FPROC, Uniform4f) X(PFNGLUNIFORM4IPROC, Uniform4i) X(PFNGLUNIFORM2IPROC, Uniform2i) \
    X(PFNGLPROVOKINGVERTEXPROC, ProvokingVertex)

#define GLAPI_OPTIONAL(X) \
    X(PFNGLCLIPCONTROLPROC, ClipControl) X(PFNGLTEXTUREBARRIERPROC, TextureBarrier)

struct Api {
#define GLAPI_DECL(type, name) type name = nullptr;
    GLAPI_FUNCS(GLAPI_DECL)
    GLAPI_OPTIONAL(GLAPI_DECL)
#undef GLAPI_DECL
    // every required function found; getProc(name) gives a function's address or null
    bool Load(void* (*getProc)(const char* name)) {
        bool ok = true;
#define GLAPI_LOAD(type, fn) name = "gl" #fn; fn = (type)getProc(name); if (!fn) { ok = false; missing = name; }
        const char* name = nullptr;
        GLAPI_FUNCS(GLAPI_LOAD)
#undef GLAPI_LOAD
#define GLAPI_LOADOPT(type, fn) fn = (type)getProc("gl" #fn);
        GLAPI_OPTIONAL(GLAPI_LOADOPT)
#undef GLAPI_LOADOPT
        return ok;
    }
    const char* missing = nullptr;
};

}  // namespace glapi
