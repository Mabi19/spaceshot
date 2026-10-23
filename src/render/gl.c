#include "image.h"
#include "log.h"
#include "render/command.h"
#include "render/common.h"
#include "render/renderer.h"
#include "render/texture.h"
#include "wayland/globals.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
// clang-format off
// gl2ext.h depends on GLES3/gl3.h being included
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
// clang-format on
#include <assert.h>
#include <cairo.h>
#include <drm_fourcc.h>
#include <math.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-egl.h>
#include <xxh3.h>

// This file uses #embed, which ccache doesn't support. Therefore:
// TODO: remove once ccache 4.14 is available
// ccache:disable

typedef struct {
    EGLSurface surface;
    struct wl_egl_window *window;
    uint32_t device_width;
    uint32_t device_height;
} GLCanvas;

// Texture metadata: dimensions and CPU readback information.
// Transformed textures (which can appear with dmabufs) additionally store the
// transform applied to their contents; both drawing and downloading apply it.
typedef struct {
    GLuint gl;
    /** EGLImage for dmabuf-backed textures, EGL_NO_IMAGE_KHR otherwise. */
    EGLImageKHR image;
    // dmabuf-backed textures own their buffers
    DmabufBuffer *dmabuf;
    uint32_t width;
    uint32_t height;
    ImageFormat download_format;
    ImageTransform transform;
} GLTexture;

// Data for a single instance of a rectangle.
typedef struct {
    GLfloat position[2];
    GLfloat size[2];
    GLfloat color[4];
    // order: tl, tr, bl, br
    GLfloat border_radius[4];
    // order: left, right, top, bottom
    GLfloat border_width[4];
    GLfloat border_color[4];
    // UVs are stored here with the instances instead of per-vertex,
    // because the first instance's UVs would loop
    // instead of using the data corresponding to each instance.
    GLfloat uv_tl[2];
    GLfloat uv_tr[2];
    GLfloat uv_bl[2];
    GLfloat uv_br[2];
} GLRectData;

// A helper macro that calls RECT_FIELD with every field of GLRectData
// and their indices. Used to define the vertex array in init and
// flush_quad_batch.
#define RECT_DATA_FIELDS                                                       \
    RECT_FIELD(1, position);                                                   \
    RECT_FIELD(2, size);                                                       \
    RECT_FIELD(3, color);                                                      \
    RECT_FIELD(4, border_radius);                                              \
    RECT_FIELD(5, border_width);                                               \
    RECT_FIELD(6, border_color);                                               \
    RECT_FIELD(7, uv_tl);                                                      \
    RECT_FIELD(8, uv_tr);                                                      \
    RECT_FIELD(9, uv_bl);                                                      \
    RECT_FIELD(10, uv_br)

typedef struct GLTextCacheEntry {
    char *content;
    size_t length;
    RenderTextStyle style;
    XXH64_hash_t key_hash;
    uint32_t last_used_frame;

    PangoRectangle logical_rect;
    PangoRectangle ink_rect;
    /**
     * May be NULL if a texture wasn't generated
     * (e.g. this text was only measured)
     */
    GLTexture *texture;
    struct GLTextCacheEntry *next;
} GLTextCacheEntry;

static const char VERTEX_SHADER_SOURCE[] = {
#embed "rect-vert.glsl"
    , '\0'
};

static const char FRAGMENT_SHADER_SOURCE[] = {
#embed "rect-frag.glsl"
    , '\0'
};

static const char PASS_VERTEX_SHADER_SOURCE[] = {
#embed "pass-vert.glsl"
    , '\0'
};

static const char PASS_IDENTITY_SHADER_SOURCE[] = {
#embed "pass-identity.glsl"
    , '\0'
};

static const char PASS_SMART_BORDER_H_SHADER_SOURCE[] = {
#embed "pass-smarth.glsl"
    , '\0'
};

static const char PASS_SMART_BORDER_V_SHADER_SOURCE[] = {
#embed "pass-smartv.glsl"
    , '\0'
};

// internal state: EGL & GL objects + Pango objects necessary for text
static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;

// EGL/GL entry points and capabilities for dmabuf import
static bool gl_has_dmabuf_import = false;
static PFNEGLCREATEIMAGEKHRPROC gl_eglCreateImageKHR = NULL;
static PFNEGLDESTROYIMAGEKHRPROC gl_eglDestroyImageKHR = NULL;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC gl_eglImageTargetTexture2DOES = NULL;
static PFNEGLQUERYDMABUFMODIFIERSEXTPROC gl_eglQueryDmaBufModifiersEXT = NULL;

static GLuint gl_texture_none = 0;
static GLuint gl_vao = 0;
static GLuint gl_vbo_rect_mesh = 0;
static GLuint gl_vbo_rect_data = 0;
static GLuint gl_shader_program = 0;
static GLuint gl_uniform_screen_size = 0;
static GLuint gl_uniform_tex = 0;

// Offscreen programs share this vertex shader and fullscreen quad. Keep the
// shader alive so future image-processing programs can link against it too.
static GLuint gl_pass_vertex_shader = 0;
static GLuint gl_pass_vao = 0;
static GLuint gl_pass_vbo = 0;
// SHADER_PASS_IDENTITY
static GLuint gl_pass_identity_program = 0;
static GLint gl_pass_identity_tex = 0;
// SHADER_PASS_SMART_BORDER_H
static GLuint gl_pass_smart_border_h_program = 0;
static GLint gl_pass_smart_border_h_tex = 0;
static GLint gl_pass_smart_border_h_scale = 0;
// SHADER_PASS_SMART_BORDER_V
static GLuint gl_pass_smart_border_v_program = 0;
static GLint gl_pass_smart_border_v_tex = 0;
static GLint gl_pass_smart_border_v_scale = 0;

static GLRectData *rect_instance_buffer = NULL;
static size_t rect_instance_capacity = 0;

static PangoFontMap *pango_fontmap = NULL;
static PangoContext *pango_context = NULL;
static PangoLayout *pango_layout = NULL;
static PangoFontDescription *pango_font_description = NULL;

static XXH3_state_t *text_cache_hash_state = NULL;
static GLTextCacheEntry **text_cache = NULL;
static size_t text_cache_item_count = 0;
static size_t text_cache_size = 0;
// Used by the text cache to know when to evict items.
static uint32_t frame_no = 0;

static void renderer_gl_cleanup();

static GLuint
compile_shader(const char *name, GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLchar message[1024];
        glGetShaderInfoLog(shader, sizeof(message), NULL, message);
        log_debug(
            "GL shader '%s' compilation failed (%x):\n%s\n", name, type, message
        );
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// The caller retains ownership of both shaders, including on failure.
static GLuint
link_program(const char *name, GLuint vertex_shader, GLuint fragment_shader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLchar message[1024];
        glGetProgramInfoLog(program, sizeof(message), NULL, message);
        log_debug("GL shader '%s' link failed:\n%s\n", name, message);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

static GLuint
create_pass_program(const char *name, const char *fragment_source) {
    GLuint fragment_shader =
        compile_shader(name, GL_FRAGMENT_SHADER, fragment_source);
    if (!fragment_shader) {
        return 0;
    }
    GLuint program = link_program(name, gl_pass_vertex_shader, fragment_shader);
    glDeleteShader(fragment_shader);
    return program;
}

/** Whether the current context supports a GL extension. */
static bool gl_has_extension(const char *name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; i++) {
        const char *extension = (const char *)glGetStringi(GL_EXTENSIONS, i);
        if (extension && strcmp(extension, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool renderer_gl_init() {
    egl_display = eglGetPlatformDisplay(
        EGL_PLATFORM_WAYLAND_KHR, wayland_globals.display, NULL
    );
    if (egl_display == EGL_NO_DISPLAY) {
        log_debug("eglGetPlatformDisplay failed\n");
        return false;
    }
    if (!eglInitialize(egl_display, NULL, NULL)) {
        log_debug("eglInitialize failed\n");
        renderer_gl_cleanup();
        return false;
    }

    // check for necessary extensions
    const char *exts = eglQueryString(egl_display, EGL_EXTENSIONS);
    if (exts == NULL) {
        log_debug("Couldn't get EGL extensions\n");
        renderer_gl_cleanup();
        return false;
    }
    bool has_no_config_context = false;
    bool has_surfaceless_context = false;
    bool has_image_dma_buf_import = false;
    bool has_image_base = false;
    bool has_image_dma_buf_import_modifiers = false;
    const char *ext_start = exts;
    int length = 0;
    while (true) {
        char c = ext_start[length];
        if (c == ' ' || c == '\0') {
#define CHECK_EXT(name)                                                        \
    length == sizeof(name) - 1 && memcmp(ext_start, name, length) == 0
            if (CHECK_EXT("EGL_KHR_no_config_context")) {
                has_no_config_context = true;
            }
            if (CHECK_EXT("EGL_KHR_surfaceless_context")) {
                has_surfaceless_context = true;
            }
            if (CHECK_EXT("EGL_KHR_image_base")) {
                has_image_base = true;
            }
            if (CHECK_EXT("EGL_EXT_image_dma_buf_import")) {
                has_image_dma_buf_import = true;
            }
            if (CHECK_EXT("EGL_EXT_image_dma_buf_import_modifiers")) {
                has_image_dma_buf_import_modifiers = true;
            }
#undef CHECK_EXT

            if (c == '\0') {
                break;
            }
            ext_start += length + 1;
            length = 0;
        }
        length++;
    }
    if (!has_no_config_context) {
        log_debug("Missing required extension EGL_KHR_no_config_context\n");
        renderer_gl_cleanup();
        return false;
    }
    if (!has_surfaceless_context) {
        log_debug("Missing required extension EGL_KHR_surfaceless_context\n");
        renderer_gl_cleanup();
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        log_debug("eglBindAPI failed\n");
        renderer_gl_cleanup();
        return false;
    }

    constexpr EGLint CONTEXT_ATTRIBS[] = {
        EGL_CONTEXT_MAJOR_VERSION,
        3,
        EGL_CONTEXT_MINOR_VERSION,
        0,
        EGL_NONE,
    };
    egl_context = eglCreateContext(
        egl_display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, CONTEXT_ATTRIBS
    );
    if (egl_context == EGL_NO_CONTEXT) {
        log_debug("eglCreateContext failed\n");
        renderer_gl_cleanup();
        return false;
    }
    if (!eglMakeCurrent(
            egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context
        )) {
        log_debug("eglMakeCurrent failed\n");
        renderer_gl_cleanup();
        return false;
    }

    // dmabuf import is optional; without it dmabuf capture is disabled
    // (GL_OES_EGL_image is what provides glEGLImageTargetTexture2DOES, whose
    // GLeglImageOES argument is the handle of an EGLImage created with
    // eglCreateImageKHR)
    if (has_image_base && has_image_dma_buf_import &&
        has_image_dma_buf_import_modifiers &&
        gl_has_extension("GL_OES_EGL_image")) {
        gl_eglCreateImageKHR =
            (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
        gl_eglDestroyImageKHR =
            (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
        gl_eglImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
            eglGetProcAddress("glEGLImageTargetTexture2DOES");
        gl_eglQueryDmaBufModifiersEXT = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)
            eglGetProcAddress("eglQueryDmaBufModifiersEXT");
        gl_has_dmabuf_import = gl_eglCreateImageKHR != NULL &&
                               gl_eglDestroyImageKHR != NULL &&
                               gl_eglImageTargetTexture2DOES != NULL &&
                               gl_eglQueryDmaBufModifiersEXT != NULL;
    }
    if (!gl_has_dmabuf_import) {
        log_debug("dmabuf texture import is unavailable\n");
    }

    // Set up common GL state and data
    log_debug(
        "GL renderer: %s / %s\n",
        glGetString(GL_RENDERER),
        glGetString(GL_VERSION)
    );
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);

    // 1x1 white texture for use when RenderCommandRect.texture is NULL
    glGenTextures(1, &gl_texture_none);
    glBindTexture(GL_TEXTURE_2D, gl_texture_none);
    constexpr GLubyte NONE_TEXTURE_DATA[] = {255, 255, 255};
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB,
        1,
        1,
        0,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        NONE_TEXTURE_DATA
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenVertexArrays(1, &gl_vao);
    glBindVertexArray(gl_vao);
    // A VBO for the base rectangle mesh
    glGenBuffers(1, &gl_vbo_rect_mesh);
    glBindBuffer(GL_ARRAY_BUFFER, gl_vbo_rect_mesh);
    // clang-format off
    constexpr GLfloat RECTANGLE_MESH[] = {
        0.f, 0.f, // tl
        1.f, 0.f, // tr
        0.f, 1.f, // bl
        1.f, 1.f, // br
    };
    // clang-format on
    glBufferData(
        GL_ARRAY_BUFFER, sizeof(RECTANGLE_MESH), RECTANGLE_MESH, GL_STATIC_DRAW
    );
    // vertex positions
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    // rectangle parameters: this VBO will store one GLRectData per instance
    glGenBuffers(1, &gl_vbo_rect_data);
    glBindBuffer(GL_ARRAY_BUFFER, gl_vbo_rect_data);
    // The pointers are set on-demand. See flush_quad_batch for more details.
#define RECT_FIELD(index, field)                                               \
    glEnableVertexAttribArray(index);                                          \
    glVertexAttribDivisor(index, 1);

    RECT_DATA_FIELDS;
#undef RECT_FIELD

    // Shader for regular drawing
    TIMING_START(gl_shader_init);
    GLuint vertex_shader =
        compile_shader("rect-vert", GL_VERTEX_SHADER, VERTEX_SHADER_SOURCE);
    if (!vertex_shader) {
        renderer_gl_cleanup();
        return false;
    }
    GLuint fragment_shader =
        compile_shader("rect-frag", GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SOURCE);
    if (!fragment_shader) {
        glDeleteShader(vertex_shader);
        renderer_gl_cleanup();
        return false;
    }
    gl_shader_program = link_program("rect", vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    if (!gl_shader_program) {
        renderer_gl_cleanup();
        return false;
    }
    // screen_size is in the vertex shader, tex is in the fragment shader
    gl_uniform_screen_size =
        glGetUniformLocation(gl_shader_program, "screen_size");
    gl_uniform_tex = glGetUniformLocation(gl_shader_program, "tex");
    // 0 is technically the default, but set it to be explicit
    glUseProgram(gl_shader_program);
    glUniform1i(gl_uniform_tex, 0);

    // Shaders and other data for texture_run_pass
    gl_pass_vertex_shader = compile_shader(
        "pass-vertex", GL_VERTEX_SHADER, PASS_VERTEX_SHADER_SOURCE
    );
    if (!gl_pass_vertex_shader) {
        renderer_gl_cleanup();
        return false;
    }

    gl_pass_identity_program =
        create_pass_program("pass-identity", PASS_IDENTITY_SHADER_SOURCE);
    if (!gl_pass_identity_program) {
        renderer_gl_cleanup();
        return false;
    }
    gl_pass_identity_tex =
        glGetUniformLocation(gl_pass_identity_program, "tex");

    gl_pass_smart_border_h_program = create_pass_program(
        "pass-smart-border-h", PASS_SMART_BORDER_H_SHADER_SOURCE
    );
    if (!gl_pass_smart_border_h_program) {
        renderer_gl_cleanup();
        return false;
    }
    gl_pass_smart_border_h_tex =
        glGetUniformLocation(gl_pass_smart_border_h_program, "tex");
    gl_pass_smart_border_h_scale =
        glGetUniformLocation(gl_pass_smart_border_h_program, "scale");

    gl_pass_smart_border_v_program = create_pass_program(
        "pass-smart-border-v", PASS_SMART_BORDER_V_SHADER_SOURCE
    );
    if (!gl_pass_smart_border_v_program) {
        renderer_gl_cleanup();
        return false;
    }
    gl_pass_smart_border_v_tex =
        glGetUniformLocation(gl_pass_smart_border_v_program, "tex");
    gl_pass_smart_border_v_scale =
        glGetUniformLocation(gl_pass_smart_border_v_program, "scale");

    TIMING_END(gl_shader_init);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_debug("GL error during init A: %d\n", error);
        renderer_gl_cleanup();
        return false;
    }

    glGenVertexArrays(1, &gl_pass_vao);
    glBindVertexArray(gl_pass_vao);
    glGenBuffers(1, &gl_pass_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, gl_pass_vbo);
    constexpr GLfloat PASS_QUAD[] = {
        -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(PASS_QUAD), PASS_QUAD, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    error = glGetError();
    if (error != GL_NO_ERROR) {
        log_debug("GL error during init: %d\n", error);
        renderer_gl_cleanup();
        return false;
    }

    pango_fontmap = pango_cairo_font_map_new();
    pango_context = pango_font_map_create_context(pango_fontmap);
    pango_layout = pango_layout_new(pango_context);
    renderer_init_pango_layout(pango_layout);
    pango_font_description = pango_font_description_new();
    text_cache_hash_state = XXH3_createState();
    text_cache_item_count = 0;
    text_cache_size = 64;
    text_cache = calloc(64, sizeof(GLTextCacheEntry *));

    frame_no = 0;

    return true;
}

static void text_cache_free_entry(GLTextCacheEntry *entry);

static void renderer_gl_cleanup() {
    if (gl_pass_identity_program != 0) {
        glDeleteProgram(gl_pass_identity_program);
    }
    if (gl_pass_smart_border_h_program != 0) {
        glDeleteProgram(gl_pass_smart_border_h_program);
    }
    if (gl_pass_smart_border_v_program != 0) {
        glDeleteProgram(gl_pass_smart_border_v_program);
    }
    if (gl_pass_vertex_shader != 0) {
        glDeleteShader(gl_pass_vertex_shader);
    }
    if (gl_pass_vao != 0) {
        glDeleteVertexArrays(1, &gl_pass_vao);
    }
    if (gl_pass_vbo != 0) {
        glDeleteBuffers(1, &gl_pass_vbo);
    }
    if (gl_shader_program != 0) {
        glDeleteProgram(gl_shader_program);
    }
    if (gl_vao != 0) {
        glDeleteVertexArrays(1, &gl_vao);
    }
    if (gl_vbo_rect_mesh != 0) {
        glDeleteBuffers(1, &gl_vbo_rect_mesh);
    }
    if (gl_vbo_rect_data != 0) {
        glDeleteBuffers(1, &gl_vbo_rect_data);
    }
    if (gl_texture_none != 0) {
        glDeleteTextures(1, &gl_texture_none);
    }

    if (text_cache) {
        for (size_t i = 0; i < text_cache_size; i++) {
            GLTextCacheEntry *entry = text_cache[i];
            while (entry != NULL) {
                GLTextCacheEntry *next = entry->next;
                text_cache_free_entry(entry);
                entry = next;
            }
        }
        free(text_cache);
    }

    if (egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(egl_display, egl_context);
    }
    if (egl_display != EGL_NO_DISPLAY) {
        eglTerminate(egl_display);
    }

    free(rect_instance_buffer);

    gl_has_dmabuf_import = false;
    gl_eglCreateImageKHR = NULL;
    gl_eglDestroyImageKHR = NULL;
    gl_eglImageTargetTexture2DOES = NULL;
    gl_eglQueryDmaBufModifiersEXT = NULL;

    if (pango_fontmap) {
        g_object_unref(pango_fontmap);
    }
    if (pango_context) {
        g_object_unref(pango_context);
    }
    if (pango_layout) {
        g_object_unref(pango_layout);
    }
    if (pango_font_description) {
        pango_font_description_free(pango_font_description);
    }
    if (text_cache_hash_state) {
        XXH3_freeState(text_cache_hash_state);
    }
}

static RenderCanvas *renderer_gl_canvas_new(
    struct wl_surface *wl_surface,
    uint32_t device_width,
    uint32_t device_height,
    ImageFormat pixel_format
) {
    GLCanvas *result = calloc(1, sizeof(GLCanvas));
    result->device_width = device_width;
    result->device_height = device_height;
    result->window =
        wl_egl_window_create(wl_surface, device_width, device_height);

    // The only thing we care about here is 10-bitness:
    // if pixel_format is 10-bit, that means we want to draw 10-bit textures.
    bool is_10bit = pixel_format == IMAGE_FORMAT_XRGB2101010 ||
                    pixel_format == IMAGE_FORMAT_XBGR2101010;
    // In the 10bit case we will always draw a 10bit image as a background.
    const EGLint attrib_list_10bit[] = {
        EGL_RED_SIZE,
        10,
        EGL_GREEN_SIZE,
        10,
        EGL_BLUE_SIZE,
        10,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT,
        EGL_NONE,
    };
    // In the non-10bit case we may need alpha, so get it.
    const EGLint attrib_list[] = {
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_ALPHA_SIZE,
        8,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT,
        EGL_NONE,
    };

    EGLConfig config;
    EGLint num_found = 0;
    if (is_10bit) {
        eglChooseConfig(egl_display, attrib_list_10bit, &config, 1, &num_found);
        if (num_found == 0) {
            log_debug("acquiring 10-bit surface config failed, falling back\n");
        } else {
            log_debug("got 10-bit config\n");
        }
    }
    if (num_found == 0) {
        // Either getting a 10-bit config failed, or we want an 8-bit config.
        if (!eglChooseConfig(
                egl_display, attrib_list, &config, 1, &num_found
            ) ||
            num_found == 0) {
            report_error_fatal(
                "Couldn't choose an appropriate EGL surface config"
            );
        }
        log_debug("got 8-bit config\n");
    }

    result->surface = eglCreatePlatformWindowSurface(
        egl_display, config, result->window, NULL
    );
    if (result->surface == EGL_NO_SURFACE) {
        report_error_fatal("Couldn't create EGL window surface");
    }
    if (!eglMakeCurrent(
            egl_display, result->surface, result->surface, egl_context
        )) {
        report_error_fatal("eglMakeCurrent failed during canvas new");
    };
    eglSwapInterval(egl_display, 0);

    return (RenderCanvas *)result;
}

static void renderer_gl_canvas_resize(
    RenderCanvas *render_canvas, uint32_t device_width, uint32_t device_height
) {
    GLCanvas *canvas = (GLCanvas *)render_canvas;
    wl_egl_window_resize(canvas->window, device_width, device_height, 0, 0);
    canvas->device_width = device_width;
    canvas->device_height = device_height;
}

static void renderer_gl_canvas_destroy(RenderCanvas *render_canvas) {
    GLCanvas *canvas = (GLCanvas *)render_canvas;
    eglDestroySurface(egl_display, canvas->surface);
    wl_egl_window_destroy(canvas->window);
    free(canvas);
    // in case this was the active surface, revert to a no-surface configuration
    // (probably for deleting textures and whatnot)
    if (!eglMakeCurrent(
            egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context
        )) {
        report_error_fatal("eglMakeCurrent failed during canvas destroy");
    };
}

static RenderTexture *renderer_gl_texture_new_from_image(const Image *image) {
    GLTexture *texture = calloc(1, sizeof(GLTexture));
    texture->width = image->width;
    texture->height = image->height;
    // Uploading and reading back with the same GL formats is byte-preserving,
    // so readback can produce the original format.
    texture->download_format = image->format;
    glGenTextures(1, &texture->gl);
    glBindTexture(GL_TEXTURE_2D, texture->gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Rect quads have a 1px antialiasing fringe that samples slightly
    // outside the given UV range.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    uint32_t bpp = image_format_bytes_per_pixel(image->format);
    // GL can only handle strides which are an integer number of pixels.
    assert(image->stride % bpp == 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, image->stride / bpp);

#define TEX_IMAGE(                                                             \
    internalformat, format, type, swizzle_r, swizzle_g, swizzle_b, swizzle_a   \
)                                                                              \
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, swizzle_r);           \
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, swizzle_g);           \
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, swizzle_b);           \
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, swizzle_a);           \
    glTexImage2D(                                                              \
        GL_TEXTURE_2D,                                                         \
        0,                                                                     \
        internalformat,                                                        \
        image->width,                                                          \
        image->height,                                                         \
        0,                                                                     \
        format,                                                                \
        type,                                                                  \
        image->data                                                            \
    )

    switch (image->format) {
    case IMAGE_FORMAT_XRGB8888:
        TEX_IMAGE(
            GL_RGBA8,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            GL_BLUE,
            GL_GREEN,
            GL_RED,
            GL_ONE
        );
        break;
    case IMAGE_FORMAT_XBGR8888:
        TEX_IMAGE(
            GL_RGBA8,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            GL_RED,
            GL_GREEN,
            GL_BLUE,
            GL_ONE
        );
        break;
    case IMAGE_FORMAT_ARGB8888:
        TEX_IMAGE(
            GL_RGBA8,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            GL_BLUE,
            GL_GREEN,
            GL_RED,
            GL_ALPHA
        );
        break;
    case IMAGE_FORMAT_ABGR8888:
        TEX_IMAGE(
            GL_RGBA8,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            GL_RED,
            GL_GREEN,
            GL_BLUE,
            GL_ALPHA
        );
        break;
    case IMAGE_FORMAT_XRGB2101010:
        TEX_IMAGE(
            GL_RGB10_A2,
            GL_RGBA,
            GL_UNSIGNED_INT_2_10_10_10_REV,
            GL_BLUE,
            GL_GREEN,
            GL_RED,
            GL_ONE
        );
        break;
    case IMAGE_FORMAT_XBGR2101010:
        TEX_IMAGE(
            GL_RGB10_A2,
            GL_RGBA,
            GL_UNSIGNED_INT_2_10_10_10_REV,
            GL_RED,
            GL_GREEN,
            GL_BLUE,
            GL_ONE
        );
        break;
    default:
        REPORT_UNHANDLED("image format", "%d", image->format);
    }
#undef TEX_IMAGE

    return (RenderTexture *)texture;
}

static void renderer_gl_texture_destroy(RenderTexture *render_texture) {
    GLTexture *texture = (GLTexture *)render_texture;
    glDeleteTextures(1, &texture->gl);
    if (texture->image != EGL_NO_IMAGE_KHR) {
        gl_eglDestroyImageKHR(egl_display, texture->image);
        dmabuf_buffer_destroy(texture->dmabuf);
    }
    free(texture);
}

/**
 * Run a same-size image-processing shader pass into a new, caller-owned
 * texture. Returns NULL on allocation/rendering failure.
 */
static RenderTexture *renderer_gl_texture_run_pass(
    const RenderTexture *render_source, ShaderPassType type, ...
) {
    GLTexture *source = (GLTexture *)render_source;

    TIMING_START(texture_pass);

    ImageFormat output_format;
    va_list args;
    va_start(args);
    switch (type) {
    case SHADER_PASS_IDENTITY:
        glUseProgram(gl_pass_identity_program);
        glUniform1i(gl_pass_identity_tex, 0);
        output_format = source->download_format;
        break;
    case SHADER_PASS_SMART_BORDER_H: {
        glUseProgram(gl_pass_smart_border_h_program);
        glUniform1i(gl_pass_smart_border_h_tex, 0);
        uint32_t scale = va_arg(args, uint32_t);
        glUniform1ui(gl_pass_smart_border_h_scale, scale);
        output_format = IMAGE_FORMAT_GRAY8;
        break;
    }
    case SHADER_PASS_SMART_BORDER_V: {
        glUseProgram(gl_pass_smart_border_v_program);
        glUniform1i(gl_pass_smart_border_v_tex, 0);
        uint32_t scale = va_arg(args, uint32_t);
        glUniform1ui(gl_pass_smart_border_v_scale, scale);
        output_format = IMAGE_FORMAT_GRAY8;
        break;
    }
    default:
        REPORT_UNHANDLED("shader pass type", "%d", type);
    }
    va_end(args);

    GLenum internal_format;
    switch (output_format) {
    case IMAGE_FORMAT_GRAY8:
        internal_format = GL_R8;
        break;
    case IMAGE_FORMAT_ARGB8888:
    case IMAGE_FORMAT_ABGR8888:
    case IMAGE_FORMAT_XRGB8888:
    case IMAGE_FORMAT_XBGR8888:
        internal_format = GL_RGBA8;
        output_format = IMAGE_FORMAT_ABGR8888;
        break;
    case IMAGE_FORMAT_XRGB2101010:
    case IMAGE_FORMAT_XBGR2101010:
        internal_format = GL_RGB10_A2;
        output_format = IMAGE_FORMAT_XBGR2101010;
        break;
    default:
        REPORT_UNHANDLED("pass output format", "%d", output_format);
    }

    GLTexture *result = calloc(1, sizeof(GLTexture));
    if (!result) {
        return NULL;
    }

    result->width = source->width;
    result->height = source->height;
    result->download_format = output_format;
    result->transform = source->transform;
    glGenTextures(1, &result->gl);
    glBindTexture(GL_TEXTURE_2D, result->gl);
    glTexStorage2D(
        GL_TEXTURE_2D, 1, internal_format, result->width, result->height
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (output_format == IMAGE_FORMAT_GRAY8) {
        // Grayscale pass results can also be drawn by the normal UI shader.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
    }

    GLuint target;
    glGenFramebuffers(1, &target);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
    glFramebufferTexture2D(
        GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, result->gl, 0
    );
    GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DITHER);
        glViewport(0, 0, result->width, result->height);
        glBindVertexArray(gl_pass_vao);
        glBindTexture(GL_TEXTURE_2D, source->gl);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    } else {
        log_debug("texture pass framebuffer is incomplete (%u)\n", status);
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &target);
    glEnable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_DITHER);

#ifdef SPACESHOT_TIMING
    glFinish();
#endif
    TIMING_END(texture_pass);

    GLenum error = glGetError();
    if (status != GL_FRAMEBUFFER_COMPLETE || error != GL_NO_ERROR) {
        log_debug("texture pass failed (GL error %u)\n", error);
        renderer_gl_texture_destroy((RenderTexture *)result);
        return NULL;
    }
    return (RenderTexture *)result;
}

static bool renderer_gl_supports_dmabuf() { return gl_has_dmabuf_import; }

static RenderTexture *
renderer_gl_texture_new_from_dmabuf(DmabufBuffer *buffer) {
    if (!gl_has_dmabuf_import) {
        return NULL;
    }

    EGLint attribs[64];
    int c = 0;
    attribs[c++] = EGL_WIDTH;
    attribs[c++] = buffer->width;
    attribs[c++] = EGL_HEIGHT;
    attribs[c++] = buffer->height;
    attribs[c++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[c++] = buffer->format;
    for (uint32_t i = 0; i < buffer->plane_count; i++) {
#define PLANE_ATTRIBS(no)                                                      \
    attribs[c++] = EGL_DMA_BUF_PLANE##no##_FD_EXT;                             \
    attribs[c++] = buffer->planes[no].fd;                                      \
    attribs[c++] = EGL_DMA_BUF_PLANE##no##_OFFSET_EXT;                         \
    attribs[c++] = buffer->planes[no].offset;                                  \
    attribs[c++] = EGL_DMA_BUF_PLANE##no##_PITCH_EXT;                          \
    attribs[c++] = buffer->planes[no].pitch;                                   \
    attribs[c++] = EGL_DMA_BUF_PLANE##no##_MODIFIER_LO_EXT;                    \
    attribs[c++] = (EGLint)(buffer->modifier & 0xffffffffu);                   \
    attribs[c++] = EGL_DMA_BUF_PLANE##no##_MODIFIER_HI_EXT;                    \
    attribs[c++] = (EGLint)(buffer->modifier >> 32)
        switch (i) {
        case 0:
            PLANE_ATTRIBS(0);
            break;
        case 1:
            PLANE_ATTRIBS(1);
            break;
        case 2:
            PLANE_ATTRIBS(2);
            break;
        case 3:
            PLANE_ATTRIBS(3);
            break;
        }
    }
#undef PLANE_ATTRIBS
    attribs[c++] = EGL_NONE;

    EGLImageKHR image = gl_eglCreateImageKHR(
        egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs
    );
    if (image == EGL_NO_IMAGE_KHR) {
        log_debug("eglCreateImageKHR failed: %x\n", eglGetError());
        return NULL;
    }

    GLTexture *texture = calloc(1, sizeof(GLTexture));
    glGenTextures(1, &texture->gl);
    glBindTexture(GL_TEXTURE_2D, texture->gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // The buffer's DRM format already determines the color components
    // correctly, so (unlike with glTexImage2D uploads) no swizzling of them
    // is necessary.
    while (glGetError() != GL_NO_ERROR) {
        // clear errors from unrelated state changes
    }
    // GLeglImageOES (GL_OES_EGL_image) is the GL-side name of an EGLImage
    // handle created with eglCreateImageKHR
    gl_eglImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)image);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_debug("glEGLImageTargetTexture2DOES failed: %x\n", error);
        glDeleteTextures(1, &texture->gl);
        gl_eglDestroyImageKHR(egl_display, image);
        free(texture);
        return NULL;
    }

    texture->image = image;
    texture->width = buffer->width;
    texture->height = buffer->height;
    texture->dmabuf = buffer;
    // glReadPixels yields logical RGBA component order, which in memory is
    // the little-endian XBGR layout.
    switch (buffer->format) {
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_XBGR2101010:
        texture->download_format = IMAGE_FORMAT_XBGR2101010;
        break;
    case DRM_FORMAT_ARGB8888:
        texture->download_format = IMAGE_FORMAT_ABGR8888;
        break;
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_XBGR8888:
        texture->download_format = IMAGE_FORMAT_XBGR8888;
        break;
    default:
        REPORT_UNHANDLED("dmabuf texture format", "%u", buffer->format);
    }

    log_debug("created texture %u from dmabuf\n", texture->gl);
    return (RenderTexture *)texture;
}

static void renderer_gl_texture_dmabuf_set_transform(
    RenderTexture *render_texture, ImageTransform transform
) {
    GLTexture *texture = (GLTexture *)render_texture;
    texture->transform = transform;
}

static size_t renderer_gl_intersect_dmabuf_modifiers(
    uint32_t format, uint64_t *modifiers, size_t modifier_count
) {
    if (!gl_has_dmabuf_import || modifier_count == 0) {
        return 0;
    }

    constexpr EGLint MAX_MODIFIERS = 256;
    EGLuint64KHR supported[MAX_MODIFIERS];
    EGLBoolean external_only[MAX_MODIFIERS];
    EGLint count = 0;
    if (!gl_eglQueryDmaBufModifiersEXT(
            egl_display, format, MAX_MODIFIERS, supported, external_only, &count
        ) ||
        count == 0) {
        // can't enumerate; assume the modifiers are not usable
        return 0;
    }

    size_t kept = 0;
    for (size_t i = 0; i < modifier_count; i++) {
        for (EGLint j = 0; j < count; j++) {
            // external-only modifiers cannot be bound to GL_TEXTURE_2D
            if (supported[j] == modifiers[i] && external_only[j] == EGL_FALSE) {
                modifiers[kept++] = modifiers[i];
                break;
            }
        }
    }
    return kept;
}

static Image *
renderer_gl_texture_download(const RenderTexture *render_texture) {
    const GLTexture *texture = (const GLTexture *)render_texture;

    TIMING_START(texture_download);

    // This function is intended to work only for dmabuf-based textures.
    // If a regular texture which is already on the CPU calls this, something's
    // probably wrong. Also it depends on the texture not being swizzled, and
    // image textures can be.
    assert(texture->dmabuf);

    Image *raw =
        image_new(texture->width, texture->height, texture->download_format);
    if (!raw) {
        return NULL;
    }

    // Imported modifiers need only be sampleable, not framebuffer-compatible.
    // Running an identity shader pass essentially copies into a texture made by
    // us, which should always work for creating the read framebuffer. However,
    // linear modifiers typically do work for read framebuffer directly.
    // TODO: If performance turns out to not be a big concern, always do the
    // identity pass to make sure.
    GLTexture *staging = NULL;
    GLuint texture_id;
    if (texture->dmabuf->modifier == DRM_FORMAT_MOD_LINEAR) {
        texture_id = texture->gl;
    } else {
        staging = (GLTexture *)renderer_gl_texture_run_pass(
            render_texture, SHADER_PASS_IDENTITY
        );
        if (!staging) {
            image_destroy(raw);
            return NULL;
        }
        texture_id = staging->gl;
    }

    GLuint framebuffer;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(
        GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id, 0
    );
    GLenum status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        log_debug("texture readback framebuffer is incomplete (%u)\n", status);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &framebuffer);
        if (staging) {
            renderer_gl_texture_destroy((RenderTexture *)staging);
        }
        image_destroy(raw);
        return NULL;
    }

    uint32_t bytes_per_pixel = image_format_bytes_per_pixel(raw->format);
    // Only glReadPixels uses these two parameters, so just setting them here is
    // fine
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, raw->stride / bytes_per_pixel);
    switch (texture->download_format) {
    case IMAGE_FORMAT_ABGR8888:
    case IMAGE_FORMAT_XBGR8888:
        glReadPixels(
            0, 0, raw->width, raw->height, GL_RGBA, GL_UNSIGNED_BYTE, raw->data
        );
        break;
    case IMAGE_FORMAT_XBGR2101010:
        glReadPixels(
            0,
            0,
            raw->width,
            raw->height,
            GL_RGBA,
            GL_UNSIGNED_INT_2_10_10_10_REV,
            raw->data
        );
        break;
    default:
        REPORT_UNHANDLED("download image format", "%d", raw->format);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    if (staging) {
        renderer_gl_texture_destroy((RenderTexture *)staging);
    }

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        log_debug("glReadPixels produced an error: %u\n", error);
        image_destroy(raw);
        return NULL;
    }

    if (texture->transform == IMAGE_TRANSFORM_NORMAL) {
        TIMING_END(texture_download);
        return raw;
    }
    // image_transform places pixels at transformed positions, so undoing an
    // applied transform means placing them with its inverse.
    // (Drawing shows the same image by sampling; there the inverse cancels.)
    Image *result =
        image_transform(raw, image_transform_invert(texture->transform));
    image_destroy(raw);

    TIMING_END(texture_download);

    return result;
}

/** Double the size of the text cache's hash table. */
static void text_cache_grow() {
    size_t new_size = text_cache_size * 2;
    log_debug("growing text cache to %zu buckets\n", new_size);
    GLTextCacheEntry **new_cache = calloc(new_size, sizeof(GLTextCacheEntry *));

    for (size_t i = 0; i < text_cache_size; i++) {
        GLTextCacheEntry *cache_entry = text_cache[i];
        while (cache_entry != NULL) {
            GLTextCacheEntry *next = cache_entry->next;
            size_t new_slot = cache_entry->key_hash & (new_size - 1);
            cache_entry->next = new_cache[new_slot];
            new_cache[new_slot] = cache_entry;
            cache_entry = next;
        }
    }
    free(text_cache);
    text_cache = new_cache;
    text_cache_size = new_size;
}

static GLTextCacheEntry *text_cache_ensure_entry(
    const char *content, int length, RenderTextStyle style
) {
    XXH3_64bits_reset(text_cache_hash_state);
    if (length == -1) {
        length = strlen(content);
    }
    XXH3_64bits_update(text_cache_hash_state, &length, sizeof(length));
    XXH3_64bits_update(text_cache_hash_state, content, length);
    size_t font_family_len = strlen(style.font_family);
    // include the null terminator to separate between fields
    XXH3_64bits_update(
        text_cache_hash_state, style.font_family, font_family_len + 1
    );
    XXH3_64bits_update(
        text_cache_hash_state, &style.font_size, sizeof(style.font_size)
    );
    XXH3_64bits_update(
        text_cache_hash_state, &style.weight, sizeof(style.weight)
    );
    XXH3_64bits_update(
        text_cache_hash_state, &style.italic, sizeof(style.italic)
    );
    XXH3_64bits_update(
        text_cache_hash_state, &style.max_width, sizeof(style.max_width)
    );
    XXH64_hash_t hash = XXH3_64bits_digest(text_cache_hash_state);
    size_t slot = hash & (text_cache_size - 1);
    GLTextCacheEntry *entry = text_cache[slot];
    while (entry != NULL) {
        if (entry->key_hash == hash && entry->length == (size_t)length &&
            memcmp(entry->content, content, length) == 0 &&
            strcmp(entry->style.font_family, style.font_family) == 0 &&
            entry->style.font_size == style.font_size &&
            entry->style.weight == style.weight &&
            entry->style.italic == style.italic &&
            entry->style.max_width == style.max_width) {
            entry->last_used_frame = frame_no;
            return entry;
        }
        entry = entry->next;
    }

    text_cache_item_count++;
    if (text_cache_item_count >= text_cache_size) {
        text_cache_grow();
        slot = hash & (text_cache_size - 1);
    }

    log_debug(
        "creating new cache entry for string %.*s\n", (int)length, content
    );
    GLTextCacheEntry *new_entry = calloc(1, sizeof(GLTextCacheEntry));
    new_entry->content = malloc(length);
    memcpy(new_entry->content, content, length);
    new_entry->length = length;
    new_entry->style = style;
    new_entry->key_hash = hash;
    new_entry->last_used_frame = frame_no;
    // compute extents: this is always needed, so we may as well do it here
    renderer_update_pango(pango_layout, pango_font_description, &style);
    pango_layout_set_text(pango_layout, content, length);
    pango_layout_set_font_description(pango_layout, pango_font_description);
    pango_layout_get_pixel_extents(
        pango_layout, &new_entry->ink_rect, &new_entry->logical_rect
    );
    // link it in
    new_entry->next = text_cache[slot];
    text_cache[slot] = new_entry;

    return new_entry;
}

/**
 * Get the GLTextCacheEntry for a TEXT command's parameters.
 * This also saves it inside the command struct, so it should be preferred over
 * manually calling text_cache_ensure_entry.
 */
static GLTextCacheEntry *
text_cache_ensure_command_entry(RenderCommandText *text) {
    if (!text->renderer_data) {
        text->renderer_data =
            text_cache_ensure_entry(text->content, text->length, text->style);
    }
    return text->renderer_data;
}

static void text_cache_ensure_rendered(GLTextCacheEntry *entry) {
    if (entry->texture != NULL) {
        return;
    }
    log_debug(
        "rendering string %.*s to cache\n", (int)entry->length, entry->content
    );

    Image *image = image_new(
        entry->ink_rect.width, entry->ink_rect.height, IMAGE_FORMAT_ARGB8888
    );
    // Images do not clear their contents by default
    memset(image->data, 0, image->stride * image->height);
    cairo_surface_t *surface = image_make_cairo_surface(image);
    cairo_t *cr = cairo_create(surface);

    renderer_update_pango(pango_layout, pango_font_description, &entry->style);
    pango_layout_set_text(pango_layout, entry->content, entry->length);
    pango_layout_set_font_description(pango_layout, pango_font_description);
    // Color is applied on the GPU.
    // This means that emoji will be colored as well;
    // this is an acceptable tradeoff for not having to also key by color,
    // since I don't see the need to have non-white labels anyway.
    cairo_move_to(cr, -entry->ink_rect.x, -entry->ink_rect.y);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    pango_cairo_update_context(cr, pango_context);
    pango_layout_context_changed(pango_layout);
    pango_cairo_show_layout(cr, pango_layout);

    cairo_surface_flush(surface);
    entry->texture = (GLTexture *)renderer_gl_texture_new_from_image(image);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    image_destroy(image);
}

/** Only frees the entry, doesn't manipulate the links. */
static void text_cache_free_entry(GLTextCacheEntry *entry) {
    free(entry->content);
    if (entry->texture) {
        renderer_gl_texture_destroy((RenderTexture *)entry->texture);
    }
    free(entry);
}

/** Returns whether the specified command should result in a drawn quad. */
static inline bool command_needs_quad(RenderCommand *command) {
    if (command->type == RENDER_COMMAND_RECT) {
        // Degenerate rects don't draw anything (and would cause a division by
        // zero in the vertex shader).
        RenderCommandRect *rect = (RenderCommandRect *)command;
        return rect->bounds.width > 0 && rect->bounds.height > 0;
    } else if (command->type == RENDER_COMMAND_TEXT) {
        // Similarly, guard against 0-size texts.
        RenderCommandText *text = (RenderCommandText *)command;
        GLTextCacheEntry *cache_entry = text_cache_ensure_command_entry(text);
        return cache_entry->ink_rect.width > 0 &&
               cache_entry->ink_rect.height > 0;
    }
    return false;
}

static void flush_quad_batch(size_t *batch_start, size_t *batch_length) {
    if (*batch_length == 0) {
        return;
    }
    uintptr_t base = *batch_start * sizeof(GLRectData);

    // To make up for the lack of glDrawArraysInstancedBaseInstance,
    // we instead shift the vertex array's attribute pointers.
#define RECT_FIELD(index, field)                                               \
    glVertexAttribPointer(                                                     \
        index,                                                                 \
        sizeof((GLRectData){}.field) / sizeof(GLfloat),                        \
        GL_FLOAT,                                                              \
        GL_FALSE,                                                              \
        sizeof(GLRectData),                                                    \
        (const void *)(base + offsetof(GLRectData, field))                     \
    )

    RECT_DATA_FIELDS;
#undef RECT_FIELD

    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, *batch_length);
    *batch_start = *batch_start + *batch_length;
    *batch_length = 0;
}

static void
renderer_gl_draw(RenderCanvas *render_canvas, const RenderDisplayList dl) {
    GLCanvas *canvas = (GLCanvas *)render_canvas;
    int canvas_width = canvas->device_width;
    int canvas_height = canvas->device_height;
    frame_no++;

    TIMING_START(gl_frame);

    if (!eglMakeCurrent(
            egl_display, canvas->surface, canvas->surface, egl_context
        )) {
        report_error_fatal("eglMakeCurrent failed during draw");
    }
    glViewport(0, 0, canvas_width, canvas_height);
    glScissor(0, 0, canvas_width, canvas_height);
    glUseProgram(gl_shader_program);
    glUniform2f(gl_uniform_screen_size, canvas_width, canvas_height);

    // Count the number of quads to emit this frame,
    // so we know how big of a buffer we need.
    size_t quad_count = 0;
    RenderCommand *cmd = dl.first;
    while (cmd != NULL) {
        if (command_needs_quad(cmd)) {
            quad_count++;
        }
        cmd = cmd->next;
    }
    if (quad_count > rect_instance_capacity) {
        rect_instance_buffer =
            realloc(rect_instance_buffer, sizeof(GLRectData) * quad_count);
        rect_instance_capacity = quad_count;
    }

    // Build the quad instances for this frame
    size_t i = 0;
    cmd = dl.first;
    while (cmd != NULL) {
        switch (cmd->type) {
        case RENDER_COMMAND_RECT: {
            if (!command_needs_quad(cmd)) {
                break;
            }
            RenderCommandRect *rect = (RenderCommandRect *)cmd;
            renderer_sanitize_rect(rect);
            if (rect->texture) {
                assert(rect->uv.u1 > rect->uv.u0 && rect->uv.v1 > rect->uv.v0);
            }

            // dmabuf-based textures may come transformed (the stored transform
            // is what the compositor applied to their contents); un-apply it
            // here by picking the right UVs.
            //
            // The image to show is image_transform(contents, invert(T)), whose
            // pixel at some position comes from the contents at the position
            // image_transform_coords(invert(T)) maps it *from*. Sampling is the
            // inverse of placing pixels, so that's just image_transform_uv(T) -
            // the two inversions cancel out. (texture_download places pixels
            // instead, so there the invert is explicit.)
            const double u[4] = {
                rect->uv.u0, rect->uv.u1, rect->uv.u0, rect->uv.u1
            };
            const double v[4] = {
                rect->uv.v0, rect->uv.v0, rect->uv.v1, rect->uv.v1
            };
            GLfloat uv_corners[4][2];
            ImageTransform transform =
                rect->texture ? ((const GLTexture *)rect->texture)->transform
                              : IMAGE_TRANSFORM_NORMAL;
            for (int j = 0; j < 4; j++) {
                double source_u, source_v;
                image_transform_uv(transform, u[j], v[j], &source_u, &source_v);
                uv_corners[j][0] = source_u;
                uv_corners[j][1] = source_v;
            }

            rect_instance_buffer[i] = (GLRectData){
                .position = {rect->bounds.x, rect->bounds.y},
                .size = {rect->bounds.width, rect->bounds.height},
                .color =
                    {rect->color.r,
                     rect->color.g,
                     rect->color.b,
                     rect->color.a},
                .border_radius =
                    {rect->border_radius.tl,
                     rect->border_radius.tr,
                     rect->border_radius.bl,
                     rect->border_radius.br},
                .border_width =
                    {rect->border_width.left,
                     rect->border_width.right,
                     rect->border_width.top,
                     rect->border_width.bottom},
                .border_color =
                    {rect->border_color.r,
                     rect->border_color.g,
                     rect->border_color.b,
                     rect->border_color.a},
                .uv_tl = {uv_corners[0][0], uv_corners[0][1]},
                .uv_tr = {uv_corners[1][0], uv_corners[1][1]},
                .uv_bl = {uv_corners[2][0], uv_corners[2][1]},
                .uv_br = {uv_corners[3][0], uv_corners[3][1]},
            };
            i++;
            break;
        }
        case RENDER_COMMAND_TEXT: {
            RenderCommandText *text = (RenderCommandText *)cmd;
            GLTextCacheEntry *cache_entry =
                text_cache_ensure_command_entry(text);
            if (cache_entry->ink_rect.width <= 0 ||
                cache_entry->ink_rect.height <= 0) {
                // The renderer can't cope with degenerate rectangles.
                break;
            }
            // We know this texture will be used now, and since this function
            // changes the GL state, calling it in the draw pass would be
            // tricky.
            text_cache_ensure_rendered(cache_entry);

            rect_instance_buffer[i] = (GLRectData){
                // Round the text position to make hinting valid.
                .position =
                    {round(text->x + cache_entry->ink_rect.x),
                     round(text->y + cache_entry->ink_rect.y)},
                .size =
                    {cache_entry->ink_rect.width, cache_entry->ink_rect.height},
                .color =
                    {text->color.r,
                     text->color.g,
                     text->color.b,
                     text->color.a},
                .uv_tl = {0, 0},
                .uv_tr = {1, 0},
                .uv_bl = {0, 1},
                .uv_br = {1, 1}
            };

            i++;
            break;
        }
        default:
            assert(!command_needs_quad(cmd));
            break;
        }

        cmd = cmd->next;
    }
    glBindVertexArray(gl_vao);
    glBindBuffer(GL_ARRAY_BUFFER, gl_vbo_rect_data);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(GLRectData) * quad_count,
        rect_instance_buffer,
        GL_STREAM_DRAW
    );

    // Draw everything, batching by texture and scissor rectangle.
    GLuint last_texture = 0;
    size_t batch_start = 0;
    size_t batch_length = 0;
    constexpr size_t MAX_CLIP_DEPTH = 16;
    RenderClipRect clip_rects[MAX_CLIP_DEPTH];
    clip_rects[0] = (RenderClipRect){0, 0, canvas_width, canvas_height};
    size_t clip_depth = 0;
    cmd = dl.first;
    while (cmd != NULL) {
        switch (cmd->type) {
        case RENDER_COMMAND_CLEAR: {
            RenderCommandClear *clear = (RenderCommandClear *)cmd;
            // I don't know why you'd clear after any geometry, but flush the
            // batch properly anyway
            flush_quad_batch(&batch_start, &batch_length);
            glClearColor(
                clear->color.r, clear->color.g, clear->color.b, clear->color.a
            );
            glClear(GL_COLOR_BUFFER_BIT);
            break;
        }
        case RENDER_COMMAND_RECT: {
            if (!command_needs_quad(cmd)) {
                break;
            }
            RenderCommandRect *rect = (RenderCommandRect *)cmd;
            GLuint texture = rect->texture == NULL
                                 ? gl_texture_none
                                 : ((const GLTexture *)rect->texture)->gl;
            if (last_texture == 0) {
                last_texture = texture;
                glBindTexture(GL_TEXTURE_2D, texture);
            } else if (last_texture != texture) {
                flush_quad_batch(&batch_start, &batch_length);
                glBindTexture(GL_TEXTURE_2D, texture);
                last_texture = texture;
            }
            batch_length++;
            break;
        }
        case RENDER_COMMAND_TEXT: {
            RenderCommandText *text = (RenderCommandText *)cmd;
            GLTextCacheEntry *cache_entry =
                text_cache_ensure_command_entry(text);
            if (cache_entry->ink_rect.width <= 0 ||
                cache_entry->ink_rect.height <= 0) {
                break;
            }

            GLuint texture = cache_entry->texture->gl;
            if (last_texture == 0) {
                last_texture = texture;
                glBindTexture(GL_TEXTURE_2D, texture);
            } else if (last_texture != texture) {
                flush_quad_batch(&batch_start, &batch_length);
                glBindTexture(GL_TEXTURE_2D, texture);
                last_texture = texture;
            }
            batch_length++;
            break;
        }
        case RENDER_COMMAND_PUSH_CLIP: {
            RenderCommandPushClip *push_clip = (RenderCommandPushClip *)cmd;
            flush_quad_batch(&batch_start, &batch_length);
            RenderClipRect new = {
                push_clip->bounds.x,
                // glScissor uses Y up
                canvas_height - push_clip->bounds.y - push_clip->bounds.height,
                push_clip->bounds.width,
                push_clip->bounds.height
            };
            RenderClipRect top = clip_rects[clip_depth];
            int x1 = new.x > top.x ? new.x : top.x;
            int y1 = new.y > top.y ? new.y : top.y;
            int x2 = new.x + new.width < top.x + top.width ? new.x + new.width
                                                           : top.x + top.width;
            int y2 = new.y + new.height < top.y + top.height
                         ? new.y + new.height
                         : top.y + top.height;
            RenderClipRect combined = {
                x1, y1, x2 <= x1 ? 0 : x2 - x1, y2 <= y1 ? 0 : y2 - y1
            };
            clip_depth++;
            assert(clip_depth < MAX_CLIP_DEPTH);
            clip_rects[clip_depth] = combined;
            glScissor(combined.x, combined.y, combined.width, combined.height);
            break;
        }
        case RENDER_COMMAND_POP_CLIP: {
            flush_quad_batch(&batch_start, &batch_length);
            assert(clip_depth > 0);
            clip_depth--;
            RenderClipRect rect = clip_rects[clip_depth];
            glScissor(rect.x, rect.y, rect.width, rect.height);
            break;
        }
        default:
            REPORT_UNHANDLED("render command type", "%d", cmd->type);
        }
        cmd = cmd->next;
    }

    flush_quad_batch(&batch_start, &batch_length);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        report_error_fatal("GL error during draw: %d", error);
    }

#ifdef SPACESHOT_TIMING
    // To properly time frames, we need glFinish(). Otherwise the GPU will
    // execute the commands whenever
    glFinish();
#else
    glFlush();
#endif
    TIMING_END(gl_frame);

    eglSwapBuffers(egl_display, canvas->surface);

    // GC stale strings from the text cache
    for (size_t i = 0; i < text_cache_size; i++) {
        // pointer to the previous entry's next pointer to the cache entry
        GLTextCacheEntry **p_entry = &text_cache[i];
        while (*p_entry != NULL) {
            if (frame_no - (*p_entry)->last_used_frame > 16) {
                GLTextCacheEntry *stale = *p_entry;
                *p_entry = (*p_entry)->next;
                log_debug(
                    "text cache GC freed string %.*s\n",
                    (int)stale->length,
                    stale->content
                );
                text_cache_free_entry(stale);
                text_cache_item_count--;
            } else {
                p_entry = &(*p_entry)->next;
            }
        }
    }
}

static RenderTextMetrics renderer_gl_measure_text(
    const char *content, int length, RenderTextStyle style
) {
    // Creating a cache entry automatically computes extents
    GLTextCacheEntry *cache_entry =
        text_cache_ensure_entry(content, length, style);

    return (RenderTextMetrics){
        .width = cache_entry->logical_rect.width,
        .height = cache_entry->logical_rect.height,
    };
}

const Renderer renderer_gl = {
    .init = renderer_gl_init,
    .cleanup = renderer_gl_cleanup,
    .canvas_new = renderer_gl_canvas_new,
    .canvas_resize = renderer_gl_canvas_resize,
    .canvas_destroy = renderer_gl_canvas_destroy,
    .draw = renderer_gl_draw,
    .measure_text = renderer_gl_measure_text,
    .texture_new_from_image = renderer_gl_texture_new_from_image,
    .texture_new_from_dmabuf = renderer_gl_texture_new_from_dmabuf,
    .texture_dmabuf_set_transform = renderer_gl_texture_dmabuf_set_transform,
    .texture_destroy = renderer_gl_texture_destroy,
    .texture_run_pass = renderer_gl_texture_run_pass,
    .texture_download = renderer_gl_texture_download,
    .supports_dmabuf = renderer_gl_supports_dmabuf,
    .intersect_dmabuf_modifiers = renderer_gl_intersect_dmabuf_modifiers,
};
