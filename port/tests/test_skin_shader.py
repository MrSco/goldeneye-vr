"""Compile the production mesh shaders and execute their skinning on Mesa GLES.

Run in WSL/Linux with g++, libEGL and Mesa. No headset or APK installation.
Mesa's single-view context replaces only OVR's view selector and declarations;
the skinning, lighting, fog and VR presentation expressions stay unchanged.
"""
from pathlib import Path
import ctypes as C
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
U, I, F, P = C.c_uint, C.c_int, C.c_float, C.c_void_p


def shaders(directory):
    source = (ROOT / "port/fast3d/gfx_opengl.cpp").read_text()
    code = source[source.index("static void append_str("):source.index("    const GLchar* sources[2]")]
    code = code.replace("static struct ShaderProgram* gfx_opengl_create_and_load_new_shader",
                        "static void generate")
    code += 'std::ofstream("mesh.vert") << vs_buf; std::ofstream("mesh.frag") << fs_buf; }\n'
    prelude = '''#include <cstdio>
#include <cstdint>
#include <fstream>
#include "gfx_cc.h"
#include "gfx_rendering_api.h"
struct GevrCpuSection { explicit GevrCpuSection(int) {} };
enum { GEVR_TIME_SHADER_COMPILE };
static bool use_multiview = true, gl_es = true, s_buildMesh = true, s_meshClipDistance;
static const char *gl_glsl_version_str = "300 es";
static int gl_glsl_version = 300;
static FilteringMode current_filter_mode = FILTER_THREE_POINT;
'''
    # Generate all combinations of alpha/fog/cutout/two-cycle, with both textures
    # and the maximum seven combiner inputs (vertex uniform pressure).
    main = '''int main(int argc, char **argv) {
    uint32_t options = std::stoul(argv[1]);
    generate(0x107a1788107a1788ULL, options);
}\n'''
    cpp = directory / "generate.cpp"
    cpp.write_text(prelude + code + main)
    exe = directory / "generate"
    subprocess.run([shutil.which("g++") or "c++", "-std=c++20", "-O2",
                    "-I" + str(ROOT / "port/fast3d"), "-I" + str(ROOT / "port/include"),
                    str(cpp), str(ROOT / "port/fast3d/gfx_cc.cpp"), "-o", str(exe)], check=True)
    for alpha in (0, 1):
        for fog in (0, 2):
            for edge in (0, 4):
                for cycle in (0, 16):
                    options = alpha | fog | edge | cycle
                    subprocess.run([str(exe), str(options)], cwd=directory, check=True)
                    vs = (directory / "mesh.vert").read_text()
                    vs = vs.replace("#extension GL_OVR_multiview2 : require", "")
                    vs = vs.replace("layout(num_views = 2) in;", "")
                    vs = vs.replace("gl_ViewID_OVR", "0u")
                    yield options, vs, (directory / "mesh.frag").read_text()


class GLES:
    def __init__(self):
        os.environ.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
        self.egl = C.CDLL("libEGL.so.1")
        self.egl.eglGetProcAddress.restype = P
        self.egl.eglGetProcAddress.argtypes = [C.c_char_p]
        get_display = self.proc("eglGetPlatformDisplayEXT", P, [U, P, P])
        self.display = get_display(0x31DD, None, None)  # EGL_PLATFORM_SURFACELESS_MESA
        initialize = self.proc("eglInitialize", U, [P, P, P])
        assert initialize(self.display, None, None), "EGL initialize failed"
        assert self.proc("eglBindAPI", U, [U])(0x30A0)
        attrs = (I * 9)(0x3033, 1, 0x3040, 0x40, 0x3024, 8, 0x3025, 8, 0x3038)
        config, count = P(), I()
        assert self.proc("eglChooseConfig", U, [P, P, P, I, P])(
            self.display, attrs, C.byref(config), 1, C.byref(count)) and count.value
        context = self.proc("eglCreateContext", P, [P, P, P, P])(
            self.display, config, None, (I * 3)(0x3098, 3, 0x3038))
        surface = self.proc("eglCreatePbufferSurface", P, [P, P, P])(
            self.display, config, (I * 5)(0x3057, 16, 0x3056, 16, 0x3038))
        assert self.proc("eglMakeCurrent", U, [P, P, P, P])(
            self.display, surface, surface, context), "GLES 3 context failed"
        self.calls = {}

    def proc(self, name, result, args):
        ptr = self.egl.eglGetProcAddress(name.encode())
        assert ptr, name
        return C.CFUNCTYPE(result, *args)(ptr)

    def call(self, name, result, args, *values):
        if name not in self.calls:
            self.calls[name] = self.proc(name, result, args)
        return self.calls[name](*values)

    def compile(self, kind, source):
        shader = self.call("glCreateShader", U, [U], kind)
        string = C.c_char_p(source.encode())
        self.call("glShaderSource", None, [U, I, P, P], shader, 1, C.byref(string), None)
        self.call("glCompileShader", None, [U], shader)
        status = I()
        self.call("glGetShaderiv", None, [U, U, P], shader, 0x8B81, C.byref(status))
        if not status.value:
            log = C.create_string_buffer(16384)
            self.call("glGetShaderInfoLog", None, [U, I, P, P], shader, len(log), None, log)
            raise AssertionError(log.value.decode())
        return shader

    def link(self, vs, fs, capture=False):
        program = self.call("glCreateProgram", U, [])
        for kind, source in ((0x8B31, vs), (0x8B30, fs)):
            shader = self.compile(kind, source)
            self.call("glAttachShader", None, [U, U], program, shader)
            self.call("glDeleteShader", None, [U], shader)
        names = ["aMeshPos", "aMeshST", "aMeshColor", "aSkinWeight", "aSkinBone"]
        names += [f"aSkinP{i}" for i in range(4)] + [f"aSkinN{i}" for i in range(4)]
        for i, name in enumerate(names):
            self.call("glBindAttribLocation", None, [U, U, C.c_char_p], program, i, name.encode())
        if capture:
            varyings = (C.c_char_p * 3)(b"gl_Position", b"vInput1", b"vFog")
            self.call("glTransformFeedbackVaryings", None, [U, I, P, U], program, 3, varyings, 0x8C8C)
        self.call("glLinkProgram", None, [U], program)
        status = I()
        self.call("glGetProgramiv", None, [U, U, P], program, 0x8B82, C.byref(status))
        if not status.value:
            log = C.create_string_buffer(16384)
            self.call("glGetProgramInfoLog", None, [U, I, P, P], program, len(log), None, log)
            raise AssertionError(log.value.decode())
        return program

    def uniform(self, program, name, kind, values):
        location = self.call("glGetUniformLocation", I, [U, C.c_char_p], program, name.encode())
        if location < 0:
            return
        if kind == "i":
            self.call("glUniform1i", None, [I, I], location, values)
        elif kind == "u":
            data = (U * 2)(*values)
            self.call("glUniform2uiv", None, [I, I, P], location, 1, data)
        elif kind == "m":
            data = (F * 16)(*values)
            self.call("glUniformMatrix4fv", None, [I, I, U, P], location, 1, 0, data)
        else:
            self.call(f"glUniform{len(values)}f", None, [I] + [F] * len(values), location, *values)

    def buffer(self, target, size, data=None, binding=None):
        buffer = U()
        self.call("glGenBuffers", None, [I, P], 1, C.byref(buffer))
        self.call("glBindBuffer", None, [U, U], target, buffer)
        self.call("glBufferData", None, [U, C.c_ssize_t, P, U], target, size, data, 0x88E0)
        if binding is not None:
            self.call("glBindBufferBase", None, [U, U, U], target, binding, buffer)
        return buffer

    def attribute(self, i, values):
        self.call(f"glVertexAttrib{len(values)}f", None, [U] + [F] * len(values), i, *values)

    def capture(self):
        self.call("glBeginTransformFeedback", None, [U], 0)
        self.call("glDrawArrays", None, [U, I, I], 0, 0, 1)
        self.call("glEndTransformFeedback", None, [])
        assert self.call("glGetError", U, []) == 0
        ptr = self.call("glMapBufferRange", P, [U, C.c_ssize_t, C.c_ssize_t, U], 0x8C8E, 0, 48, 1)
        assert ptr
        result = list(C.cast(ptr, C.POINTER(F * 12)).contents)
        self.call("glUnmapBuffer", U, [U], 0x8C8E)
        return result


def execution(gl, program):
    gl.call("glUseProgram", None, [U], program)
    gl.call("glEnable", None, [U], 0x8C89)  # rasterizer discard
    vao = U()
    gl.call("glGenVertexArrays", None, [I, P], 1, C.byref(vao))
    gl.call("glBindVertexArray", None, [U], vao)
    rows = [0.0] * (64 * 12)
    rows[:12] = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]
    rows[12:24] = [0, -1, 0, 20, 1, 0, 0, 0, 0, 0, 1, 0]
    data = (F * len(rows))(*rows)
    ubo = gl.buffer(0x8A11, C.sizeof(data), data, 0)
    block = gl.call("glGetUniformBlockIndex", U, [U, C.c_char_p], program, b"SkinPalette")
    gl.call("glUniformBlockBinding", None, [U, U, U], program, block, 0)
    gl.buffer(0x8C8E, 48, binding=0)
    identity = [1 if c == r else 0 for c in range(4) for r in range(4)]
    for name in ("uMeshMP", "uMeshInSel[0]"):
        gl.uniform(program, name, "m", identity)
    gl.uniform(program, "uVrFlat", "i", 1)
    gl.uniform(program, "uSkinValid", "u", [3, 0])
    gl.uniform(program, "uMeshLit", "i", 1)
    gl.uniform(program, "uMeshNumLights", "i", 1)
    gl.uniform(program, "uMeshAmbient", "f", [10, 20, 30])
    gl.uniform(program, "uMeshLightDir[0]", "f", [0, 0, 1])
    gl.uniform(program, "uMeshLightColor[0]", "f", [100, 110, 120])
    gl.uniform(program, "uMeshFog", "f", [.1, .2, .3, .4])
    gl.uniform(program, "uMeshFogParams", "f", [100, 50, 1])
    gl.attribute(0, [7, 8, 9])
    gl.attribute(2, [0, 0, 127/255, .7])
    gl.attribute(3, [128, 127, 0, 0])
    gl.attribute(4, [0, 1, 0, 0])
    gl.attribute(5, [0, 0, .5])
    gl.attribute(6, [10, 0, .5])
    gl.attribute(9, [0, 0, 1])
    gl.attribute(10, [0, 0, 1])
    first = gl.capture()
    expected = [10, 5, .3, 1, 110/255, 130/255, 150/255, .7, .1, .2, .3, 150/255]
    assert all(abs(a-b) < 2e-5 for a, b in zip(first, expected)), (first, expected)
    # Invalid bone drops out, leaving bone 1's entire weight (not /255).
    gl.uniform(program, "uSkinValid", "u", [2, 0])
    out = gl.capture()
    assert out[:4] == [20, 10, .3, 1] or all(abs(a-b) < 2e-5 for a,b in zip(out[:4], [20,10,.3,1])), out
    gl.uniform(program, "uSkinValid", "u", [0, 0])
    out = gl.capture()
    assert all(abs(a-b) < 2e-5 for a,b in zip(out[:4], [7,8,2.7,1])), out
    # Reupload another instance, then replay the first saved palette.
    gl.uniform(program, "uSkinValid", "u", [3, 0])
    rows[15] = 100
    changed = (F * len(rows))(*rows)
    gl.call("glBindBuffer", None, [U,U], 0x8A11, ubo)
    gl.call("glBufferData", None, [U,C.c_ssize_t,P,U], 0x8A11, C.sizeof(changed), changed, 0x88E0)
    assert gl.capture()[0] == 50
    gl.call("glBufferData", None, [U,C.c_ssize_t,P,U], 0x8A11, C.sizeof(data), data, 0x88E0)
    assert gl.capture() == first
    rows[-12:] = [1,0,0,-20, 0,1,0,0, 0,0,1,0]
    high = (F * len(rows))(*rows)
    gl.call("glBufferData", None, [U,C.c_ssize_t,P,U], 0x8A11, C.sizeof(high), high, 0x88E0)
    gl.attribute(4, [63,1,0,0])
    gl.uniform(program, "uSkinValid", "u", [0,1 << 31])
    assert all(abs(a-b) < 2e-5 for a,b in zip(gl.capture()[:4], [-20,0,.3,1]))
    gl.attribute(4, [0,1,0,0])
    gl.call("glBufferData", None, [U,C.c_ssize_t,P,U], 0x8A11, C.sizeof(data), data, 0x88E0)
    # Vertex quantization, negative normals, and zero-length normals.
    gl.uniform(program, "uSkinValid", "u", [1, 0])
    gl.attribute(5, [-.5, -.49, 100000])
    gl.attribute(9, [0, 0, -1])
    out = gl.capture()
    assert all(abs(a-b) < 2e-3 for a,b in zip(out[:4], [-1,0,9830.1,1])), out
    assert all(abs(a-b) < 2e-5 for a,b in zip(out[4:7], [10/255,20/255,30/255])), out
    gl.attribute(9, [0, 0, 0])
    assert gl.capture()[4:7] == out[4:7]
    # Fog behind the camera and without G_FOG, retained on redraw.
    behind = identity[:]
    behind[15] = -1
    gl.uniform(program, "uMeshMP", "m", behind)
    gl.attribute(5, [0,0,1])
    assert abs(gl.capture()[11] - 1) < 2e-5
    gl.uniform(program, "uMeshFogParams", "f", [100,50,0])
    assert abs(gl.capture()[11] - .4) < 2e-5
    gl.uniform(program, "uMeshMP", "m", identity)
    gl.uniform(program, "uSkinValid", "u", [0,0])
    # The existing room layout has no skin attributes. It must remain unposed
    # even with GL's default generic vec4 attribute (0,0,0,1).
    gl.attribute(3, [0,0,0,1])
    assert all(abs(a-b) < 2e-5 for a,b in zip(gl.capture()[:4], [7,8,2.7,1]))
    # The mesh's head clip position must be reconstructed through the scope
    # camera exactly as the CPU capture is, including inverted eye Y.
    head = [2,0,0,0, 0,-3,0,0, 0,0,1,-1, 0,0,0,0]
    scope = [4,0,0,0, 0,4,0,0, 0,0,-1.1,-1, .25,0,-1,0]
    gl.uniform(program, "uMeshMP", "m", head)
    gl.uniform(program, "uScopeVP", "m", scope)
    gl.uniform(program, "uHeadP", "f", [2,-3])
    gl.uniform(program, "uScope", "i", 1)
    gl.attribute(0, [1,2,-10])
    assert all(abs(a-b) < 2e-5 for a,b in zip(gl.capture()[:4], [4.25,8,10,10]))
    gl.attribute(3, [255,0,0,0])
    gl.attribute(5, [1,2,-10])
    gl.uniform(program, "uSkinValid", "u", [1,0])
    assert all(abs(a-b) < 2e-5 for a,b in zip(gl.capture()[:4], [4.25,8,10,10]))
    gl.uniform(program, "uScope", "i", 0)
    gl.uniform(program, "uSkinValid", "u", [0,0])
    gl.uniform(program, "uMeshMP", "m", identity)
    print("GLES scope: rigid and skinned positions match the scope camera with inverted eye Y")
    culling(gl, program, identity)
    print("GLES skinning: weighted pose, quantization, lighting, fog, invalid bones and palette replay passed")


def culling(gl, program, identity):
    gl.call("glDisable", None, [U], 0x8C89)
    gl.call("glViewport", None, [I,I,I,I], 0,0,16,16)
    positions = (F * 9)(-.5,-.5,0, .5,-.5,0, 0,.5,0)
    gl.buffer(0x8892, C.sizeof(positions), positions)
    gl.call("glEnableVertexAttribArray", None, [U], 0)
    gl.call("glVertexAttribPointer", None, [U,I,U,U,I,P], 0,3,0x1406,0,12,None)
    query = U()
    gl.call("glGenQueries", None, [I,P], 1,C.byref(query))
    gl.call("glEnable", None, [U], 0x0B44)
    def visible(front, side, mp):
        gl.uniform(program, "uMeshMP", "m", mp)
        gl.call("glFrontFace", None, [U], front)
        gl.call("glCullFace", None, [U], side)
        gl.call("glBeginQuery", None, [U,U], 0x8C2F,query)
        gl.call("glDrawArrays", None, [U,I,I], 4,0,3)
        gl.call("glEndQuery", None, [U], 0x8C2F)
        result = U()
        gl.call("glGetQueryObjectuiv", None, [U,U,P], query,0x8866,C.byref(result))
        assert gl.call("glGetError", U, []) == 0
        return bool(result.value)
    assert visible(0x0901, 0x0405, identity)  # CCW / back
    assert not visible(0x0901, 0x0404, identity)  # front
    inverted = identity[:]
    inverted[5] = -1
    assert visible(0x0900, 0x0405, inverted)  # CW after target y inversion
    assert not visible(0x0901, 0x0405, inverted)
    gl.call("glDisable", None, [U], 0x0B44)
    print("GLES culling: back/front and inverted-target winding passed")


if __name__ == "__main__":
    if os.name == "nt":
        print("SKIP: GLES shader execution requires Mesa under WSL/Linux")
        raise SystemExit(0)
    gl = GLES()
    with tempfile.TemporaryDirectory(prefix="gevr-skin-shader-") as tmp:
        count = 0
        for options, vs, fs in shaders(Path(tmp)):
            program = gl.link(vs, fs, capture=options == 3)
            count += 1
            if options == 3:
                execution(gl, program)
            gl.call("glDeleteProgram", None, [U], program)
        print(f"{count} production mesh shader variants compiled and linked")
