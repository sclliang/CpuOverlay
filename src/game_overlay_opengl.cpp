#include "game_overlay_runtime.h"
#include <gl/GL.h>
#include <cstdio>
#include <unordered_map>

namespace GameOverlay {
namespace {
constexpr GLenum Texture0 = 0x84c0, ActiveTexture = 0x84e0, CurrentProgram = 0x8b8d;
constexpr GLenum VertexShader = 0x8b31, FragmentShader = 0x8b30, CompileStatus = 0x8b81, LinkStatus = 0x8b82;
constexpr GLenum VaoBinding = 0x85b5, DrawFramebuffer = 0x8ca9, DrawFramebufferBinding = 0x8ca6;
constexpr GLenum PixelUnpackBuffer = 0x88ec, PixelUnpackBufferBinding = 0x88ef, FramebufferSrgb = 0x8db9;
constexpr GLenum BlendSrcRgb = 0x80c9, BlendDstRgb = 0x80c8, BlendSrcAlpha = 0x80cb, BlendDstAlpha = 0x80ca;
constexpr GLenum BlendEquationRgb = 0x8009, BlendEquationAlpha = 0x883d, FuncAdd = 0x8006;
constexpr GLenum RasterizerDiscard = 0x8c89, SampleAlphaToCoverage = 0x809e, Bgra = 0x80e1, ClampToEdge = 0x812f;
constexpr GLenum SamplerBinding = 0x8919;
void* Proc(const char* name) {
    auto p = reinterpret_cast<void*>(wglGetProcAddress(name));
    if (!p || p == reinterpret_cast<void*>(1) || p == reinterpret_cast<void*>(2) ||
        p == reinterpret_cast<void*>(3) || p == reinterpret_cast<void*>(-1))
        p = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"opengl32.dll"), name));
    return p;
}
struct Functions {
#define GL_PROC(ret, name, arguments) using name##Type = ret (APIENTRY*) arguments; name##Type name = reinterpret_cast<name##Type>(Proc("gl" #name));
    GL_PROC(void, ActiveTexture, (GLenum))
    GL_PROC(void, BindFramebuffer, (GLenum, GLuint))
    GL_PROC(void, BindVertexArray, (GLuint))
    GL_PROC(void, GenVertexArrays, (GLsizei, GLuint*))
    GL_PROC(void, DeleteVertexArrays, (GLsizei, const GLuint*))
    GL_PROC(void, BindSampler, (GLuint, GLuint))
    GL_PROC(GLuint, CreateShader, (GLenum))
    GL_PROC(void, ShaderSource, (GLuint, GLsizei, const char* const*, const GLint*))
    GL_PROC(void, CompileShader, (GLuint))
    GL_PROC(void, GetShaderiv, (GLuint, GLenum, GLint*))
    GL_PROC(void, DeleteShader, (GLuint))
    GL_PROC(GLuint, CreateProgram, ())
    GL_PROC(void, AttachShader, (GLuint, GLuint))
    GL_PROC(void, LinkProgram, (GLuint))
    GL_PROC(void, GetProgramiv, (GLuint, GLenum, GLint*))
    GL_PROC(void, DeleteProgram, (GLuint))
    GL_PROC(GLint, GetUniformLocation, (GLuint, const char*))
    GL_PROC(void, Uniform4fv, (GLint, GLsizei, const GLfloat*))
    GL_PROC(void, Uniform1i, (GLint, GLint))
    GL_PROC(void, UseProgram, (GLuint))
    GL_PROC(void, BindBuffer, (GLenum, GLuint))
    GL_PROC(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))
    GL_PROC(void, BlendEquationSeparate, (GLenum, GLenum))
#undef GL_PROC
    bool Modern() const { return ActiveTexture && BindFramebuffer && BindVertexArray && GenVertexArrays && CreateShader && ShaderSource &&
        CompileShader && GetShaderiv && DeleteShader && CreateProgram && AttachShader && LinkProgram && GetProgramiv && DeleteProgram &&
        GetUniformLocation && Uniform4fv && Uniform1i && UseProgram && BindBuffer && BlendFuncSeparate && BlendEquationSeparate; }
};
void Enable(GLenum name, bool enabled) { if (enabled) glEnable(name); else glDisable(name); }
struct SavedState {
    Functions& g;
    bool modern;
    bool samplers, unpackBuffers;
    GLint program = 0, active = Texture0, texture = 0, vao = 0, framebuffer = 0, defaultDrawBuffer = GL_BACK, unpackBuffer = 0;
    GLint viewport[4]{}, polygon[2]{}, unpack[6]{}, blend[6]{}, sampler = 0;
    GLboolean mask[4]{};
    const GLenum capabilities[9]{GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_SCISSOR_TEST,
        GL_COLOR_LOGIC_OP, FramebufferSrgb, RasterizerDiscard, SampleAlphaToCoverage};
    GLboolean enabled[9]{};
    SavedState(Functions& functions, bool core, bool hasSamplers, bool hasUnpack) : g(functions), modern(core), samplers(hasSamplers), unpackBuffers(hasUnpack) {
        if (g.UseProgram) glGetIntegerv(CurrentProgram, &program);
        if (g.ActiveTexture) { glGetIntegerv(ActiveTexture, &active); g.ActiveTexture(Texture0); }
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        if (samplers) { glGetIntegerv(SamplerBinding, &sampler); g.BindSampler(0, 0); }
        glGetIntegerv(GL_VIEWPORT, viewport); glGetIntegerv(GL_POLYGON_MODE, polygon); glGetBooleanv(GL_COLOR_WRITEMASK, mask);
        const GLenum names[]{GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST};
        for (size_t i = 0; i < ARRAYSIZE(names); ++i) glGetIntegerv(names[i], &unpack[i]);
        for (size_t i = 0; i < (modern ? 9u : 6u); ++i) enabled[i] = glIsEnabled(capabilities[i]);
        if (g.BlendFuncSeparate && g.BlendEquationSeparate) {
            glGetIntegerv(BlendSrcRgb, &blend[0]); glGetIntegerv(BlendDstRgb, &blend[1]);
            glGetIntegerv(BlendSrcAlpha, &blend[2]); glGetIntegerv(BlendDstAlpha, &blend[3]);
            glGetIntegerv(BlendEquationRgb, &blend[4]); glGetIntegerv(BlendEquationAlpha, &blend[5]);
        } else { glGetIntegerv(GL_BLEND_SRC, &blend[0]); glGetIntegerv(GL_BLEND_DST, &blend[1]); }
        if (modern) {
            glGetIntegerv(VaoBinding, &vao); glGetIntegerv(DrawFramebufferBinding, &framebuffer);
            g.BindFramebuffer(DrawFramebuffer, 0);
        }
        if (unpackBuffers) { glGetIntegerv(PixelUnpackBufferBinding, &unpackBuffer); g.BindBuffer(PixelUnpackBuffer, 0); }
        glGetIntegerv(GL_DRAW_BUFFER, &defaultDrawBuffer);
    }
    ~SavedState() {
        glDrawBuffer(defaultDrawBuffer);
        if (modern) { g.BindVertexArray(vao); g.BindFramebuffer(DrawFramebuffer, framebuffer); }
        if (unpackBuffers) g.BindBuffer(PixelUnpackBuffer, unpackBuffer);
        if (samplers) g.BindSampler(0, sampler);
        if (g.UseProgram) g.UseProgram(program);
        glBindTexture(GL_TEXTURE_2D, texture); if (g.ActiveTexture) g.ActiveTexture(active);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]); glColorMask(mask[0], mask[1], mask[2], mask[3]);
        if (polygon[0] == polygon[1]) glPolygonMode(GL_FRONT_AND_BACK, polygon[0]);
        else { glPolygonMode(GL_FRONT, polygon[0]); glPolygonMode(GL_BACK, polygon[1]); }
        const GLenum names[]{GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST};
        for (size_t i = 0; i < ARRAYSIZE(names); ++i) glPixelStorei(names[i], unpack[i]);
        for (size_t i = 0; i < (modern ? 9u : 6u); ++i) Enable(capabilities[i], enabled[i] != 0);
        if (g.BlendFuncSeparate && g.BlendEquationSeparate) {
            g.BlendFuncSeparate(blend[0], blend[1], blend[2], blend[3]); g.BlendEquationSeparate(blend[4], blend[5]);
        } else glBlendFunc(blend[0], blend[1]);
    }
};
struct Renderer {
    Functions g;
    GLuint texture = 0, program = 0, vao = 0;
    GLint rectLocation = -1;
    uint32_t width = 0, height = 0, owner = 0;
    uint64_t generation = 0;
    bool modern = false, ready = false;
    bool Init() {
        const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION)); int major = 0;
        if (!version || sscanf_s(version, "%d", &major) != 1) return false;
        modern = major >= 3;
        if (modern) {
            if (!g.Modern()) return false;
            const char* vs = "#version 130\nuniform vec4 rect; out vec2 uv; void main(){uv=vec2(float(gl_VertexID&1),float((gl_VertexID>>1)&1));gl_Position=vec4(rect.xy+uv*rect.zw,0,1);}";
            const char* ps = "#version 130\nuniform sampler2D image; in vec2 uv; out vec4 color; void main(){color=texture(image,uv);}";
            auto shader = [this](GLenum type, const char* source) {
                const GLuint id = g.CreateShader(type); g.ShaderSource(id, 1, &source, nullptr); g.CompileShader(id);
                GLint ok = 0; g.GetShaderiv(id, CompileStatus, &ok); if (!ok) { g.DeleteShader(id); return GLuint(0); } return id;
            };
            const GLuint vertex = shader(VertexShader, vs), pixel = shader(FragmentShader, ps);
            if (!vertex || !pixel) { if (vertex) g.DeleteShader(vertex); if (pixel) g.DeleteShader(pixel); return false; }
            program = g.CreateProgram(); g.AttachShader(program, vertex); g.AttachShader(program, pixel); g.LinkProgram(program);
            g.DeleteShader(vertex); g.DeleteShader(pixel);
            GLint linked = 0; g.GetProgramiv(program, LinkStatus, &linked);
            if (!linked) { g.DeleteProgram(program); program = 0; return false; }
            rectLocation = g.GetUniformLocation(program, "rect"); g.GenVertexArrays(1, &vao);
        }
        glGenTextures(1, &texture); ready = texture != 0; return ready;
    }
    void Upload(const Frame& f) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SWAP_BYTES, FALSE); glPixelStorei(GL_UNPACK_LSB_FIRST, FALSE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, modern ? ClampToEdge : GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, modern ? ClampToEdge : GL_CLAMP);
        if (width != f.header.width || height != f.header.height) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, f.header.width, f.header.height, 0, Bgra, GL_UNSIGNED_BYTE, f.pixels.data());
            width = f.header.width; height = f.header.height;
        } else if (owner != f.header.ownerPid || generation != f.header.generation)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, Bgra, GL_UNSIGNED_BYTE, f.pixels.data());
        owner = f.header.ownerPid; generation = f.header.generation;
    }
};
std::unordered_map<HGLRC, Renderer> renderers;
}
bool DrawOpenGL(HDC dc) {
    const auto* f = ReadFrame(OpenGL); if (!f) return false;
    HGLRC context = wglGetCurrentContext(); HWND window = WindowFromDC(dc);
    if (!context || !window || wglGetCurrentDC() != dc) return false;
    RECT client{}; if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) return false;
    auto& r = renderers[context];
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION)); int major = 0, minor = 0;
    if (!version || sscanf_s(version, "%d.%d", &major, &minor) < 1) return false;
    if (major >= 3 && !r.g.Modern()) return false;
    const bool samplers = r.g.BindSampler && (major > 3 || (major == 3 && minor >= 3));
    const bool unpack = r.g.BindBuffer && (major > 2 || (major == 2 && minor >= 1));
    SavedState saved(r.g, major >= 3, samplers, unpack);
    if (!r.ready && !r.Init()) { ReportError(OpenGL, ERROR_NOT_SUPPORTED); return false; }
    if (!BeginDraw(*f, OpenGL)) return true;
    r.Upload(*f);
    const auto rect = WindowPlacement(*f, window, client.right, client.bottom);
    const GLfloat placement[]{rect.x * 2 / client.right - 1, 1 - rect.y * 2 / client.bottom,
        rect.width * 2 / client.right, -rect.height * 2 / client.bottom};
    glViewport(0, 0, client.right, client.bottom); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    GLboolean doubleBuffer = GL_FALSE; glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffer); glDrawBuffer(doubleBuffer ? GL_BACK : GL_FRONT);
    glEnable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_COLOR_LOGIC_OP);
    if (r.g.BlendFuncSeparate) r.g.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    else glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (r.g.BlendEquationSeparate) r.g.BlendEquationSeparate(FuncAdd, FuncAdd);
    if (r.modern) {
        glDisable(FramebufferSrgb); glDisable(RasterizerDiscard); glDisable(SampleAlphaToCoverage);
        r.g.UseProgram(r.program); r.g.Uniform4fv(r.rectLocation, 1, placement);
        r.g.Uniform1i(r.g.GetUniformLocation(r.program, "image"), 0); r.g.BindVertexArray(r.vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    } else {
        GLint mode = GL_MODELVIEW; glGetIntegerv(GL_MATRIX_MODE, &mode);
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        if (r.g.ActiveTexture && major >= 2) {
            GLint units = 1; glGetIntegerv(0x84e2, &units);
            for (GLint unit = 1; unit < std::min(units, 32); ++unit) {
                r.g.ActiveTexture(Texture0 + unit); glDisable(GL_TEXTURE_1D); glDisable(GL_TEXTURE_2D);
                glDisable(0x806f); glDisable(0x8513);
            }
            r.g.ActiveTexture(Texture0);
        }
        if (r.g.UseProgram) r.g.UseProgram(0);
        glDisable(GL_LIGHTING); glDisable(GL_FOG); glDisable(GL_ALPHA_TEST); glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(placement[0], placement[1]);
        glTexCoord2f(1, 0); glVertex2f(placement[0] + placement[2], placement[1]);
        glTexCoord2f(0, 1); glVertex2f(placement[0], placement[1] + placement[3]);
        glTexCoord2f(1, 1); glVertex2f(placement[0] + placement[2], placement[1] + placement[3]);
        glEnd();
        glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_TEXTURE); glPopMatrix(); glMatrixMode(mode); glPopAttrib();
    }
    ReportDraw(OpenGL, client.right, client.bottom); return true;
}
void ResetOpenGL(HGLRC context) {
    auto found = renderers.find(context); if (found == renderers.end()) return;
    auto& r = found->second;
    if (wglGetCurrentContext() == context) {
        if (r.texture) glDeleteTextures(1, &r.texture);
        if (r.program && r.g.DeleteProgram) r.g.DeleteProgram(r.program);
        if (r.vao && r.g.DeleteVertexArrays) r.g.DeleteVertexArrays(1, &r.vao);
    }
    renderers.erase(found);
}
}
