/*
 * Copyright (C)      2022, Red Hat Inc, Dorinda Bassey <dbassey@redhat.com>
 * Copyright (C)      2022 Colin Ian King
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "stress-ng.h"

#if defined(HAVE_EGL_H)
#include <EGL/egl.h>
#endif
#if defined(HAVE_EGL_EXT_H)
#include <EGL/eglext.h>
#endif
#if defined(HAVE_GLES2_H)
#include <GLES2/gl2.h>
#endif

#if defined(HAVE_GBM_H)
#include <gbm.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"gpu N",		"start N GPU worker" },
	{ NULL,	"gpu-ops N",		"stop after N gpu render bogo operations" },
	{ NULL,	"gpu-frag N",		"specify shader core usage per pixel" },
	{ NULL,	"gpu-tex-size N",	"specify upload texture NxN" },
	{ NULL,	"gpu-upload N",		"specify upload texture N times per frame" },
	{ NULL,	"gpu-xsize X",		"specify framebuffer size x" },
	{ NULL,	"gpu-ysize Y",		"specify framebuffer size y" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_gpu(const char *opt, const char *name, const size_t max)
{
	int32_t gpu32;
	int64_t gpu64;

	gpu64 = stress_get_uint64(opt);
	stress_check_range(name, gpu64, 1, max);
	gpu32 = (uint32_t)gpu64;
	return stress_set_setting(name, TYPE_ID_INT32, &gpu32);
}

static int stress_set_gpu_frag(const char *opt)
{
	return stress_set_gpu(opt, "gpu-frag", INT_MAX);
}

static int stress_set_gpu_xsize(const char *opt)
{
	return stress_set_gpu(opt, "gpu-xsize", INT_MAX);
}

static int stress_set_gpu_ysize(const char *opt)
{
	return stress_set_gpu(opt, "gpu-ysize", INT_MAX);
}

static int stress_set_gpu_gl(const char *opt, const char *name, const size_t max)
{
	int gpu_val;

	gpu_val = stress_get_int32(opt);
	stress_check_range(name, gpu_val, 1, max);
	return stress_set_setting(name, TYPE_ID_INT32, &gpu_val);
}

static int stress_set_gpu_upload(const char *opt)
{
	return stress_set_gpu_gl(opt, "gpu-upload", INT_MAX);
}

static int stress_set_gpu_size(const char *opt)
{
	return stress_set_gpu_gl(opt, "gpu-tex-size", INT_MAX);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_gpu_frag,		stress_set_gpu_frag },
	{ OPT_gpu_uploads,	stress_set_gpu_upload },
	{ OPT_gpu_size,		stress_set_gpu_size },
	{ OPT_gpu_xsize,	stress_set_gpu_xsize },
	{ OPT_gpu_ysize,	stress_set_gpu_ysize },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_EGL) &&		\
	defined(HAVE_EGL_H) &&		\
	defined(HAVE_EGL_EXT_H) &&	\
	defined(HAVE_LIB_GLES2) &&	\
	defined(HAVE_GLES2_H) &&	\
	defined(HAVE_LIB_GBM) &&	\
	defined(HAVE_GBM_H)

static GLuint program;
static EGLDisplay display;
static EGLSurface surface;
static struct gbm_device *gbm;
static struct gbm_surface *gs;

static const char *devicenode = "/dev/dri/renderD128";
static GLubyte *teximage = NULL;

static const char vert_shader[] =
    "attribute vec4 pos;\n"
    "attribute vec4 color;\n"
    "varying vec4 v_color;\n"
    "void main()\n" "{\n" "v_color = color;\n" "gl_Position = pos;\n" "}\n";

static const char frag_shader[] =
    "precision mediump float;\n"
    "varying vec4 v_color;\n"
    "uniform int frag_n;\n"
    "void main()\n"
    "{\n"
    "int i;\n"
    "vec4 a = v_color;\n"
    "for (i = 0; i < frag_n; i++) {\n"
    "float f = float(i);\n"
    "a = a / clamp(sin(f) * exp(f), 0.1, 0.9);\n"
    "}\n"
    "a = clamp(a, -1.0, 1.0);\n"
    "gl_FragColor = v_color + 0.000001 * a;\n" "}\n";

static GLuint compile_shader(
	const stress_args_t *args,
	const char *text,
	const int size,
	const GLenum type)
{
	GLuint shader;
	GLint compiled;
	const GLchar *source[1] = { text };

	shader = glCreateShader(type);
	glShaderSource(shader, 1, source, &size);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled) {
		GLint infoLen = 0;

		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen > 1) {
			char *infoLog = malloc(infoLen);

			if (!infoLog) {
				pr_inf("%s: failed to allocate infoLog, skipping stressor\n", args->name);
				glDeleteShader(shader);
				return 0;
			}
			glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
			pr_inf("%s: failed to compile shader:\n%s\n", args->name, infoLog);
			free(infoLog);
		}
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

int load_shaders(const stress_args_t *args)
{
	GLint linked;
	GLuint vertexShader;
	GLuint fragmentShader;
	const char *name = args->name;

	vertexShader = compile_shader(args, vert_shader, sizeof(vert_shader),
			    GL_VERTEX_SHADER);
	if (vertexShader == 0) {
		pr_inf("%s: failed to compile vertex shader, skipping stressor\n", name);
		return EXIT_NO_RESOURCE;
	}

	fragmentShader = compile_shader(args, frag_shader, sizeof(frag_shader),
			    GL_FRAGMENT_SHADER);
	if (fragmentShader == 0) {
		pr_inf("%s: failed to compile fragment shader, skipping stressor\n", name);
		return EXIT_NO_RESOURCE;
	}

	program = glCreateProgram();
	if (program == 0) {
		pr_inf("%s: failed to create the shader program\n", name);
		return EXIT_NO_RESOURCE;
	}

	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		GLint infoLen = 0;

		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen > 1) {
			char *infoLog = malloc(infoLen);

			if (!infoLog) {
				pr_inf("%s: failed to allocate infoLog, skipping stressor\n", name);
				glDeleteProgram(program);
				return EXIT_NO_RESOURCE;
			}
			glGetProgramInfoLog(program, infoLen, NULL, infoLog);
			pr_fail("%s: failed to link shader program:\n%s\n", name, infoLog);
			free(infoLog);
		}
		glDeleteProgram(program);
		return EXIT_FAILURE;
	}

	glUseProgram(program);

	return EXIT_SUCCESS;
}

static const GLfloat vertex[] = {
	-1, -1, 0, 1,
	-1, 1, 0, 1,
	1, 1, 0, 1,

	-1, -1, 0, 1,
	1, 1, 0, 1,
	1, -1, 0, 1,
};

static const GLfloat color[] = {
	1, 0, 0, 1,
	0, 1, 0, 1,
	0, 0, 1, 1,

	1, 0, 0, 1,
	0, 0, 1, 1,
	1, 1, 0, 1,
};

int gles2_init(
	const stress_args_t *args,
	const uint32_t width,
	const uint32_t height,
	const int frag_n,
	const GLsizei texsize)
{
	int ret;
	GLint ufrag_n, apos, acolor;

	if (args->instance == 0) {
		pr_inf("GL_VENDOR: %s\n", (char *)glGetString(GL_VENDOR));
		pr_inf("GL_VERSION: %s\n", (char *)glGetString(GL_VERSION));
		pr_inf("GL_RENDERER: %s\n", (char *)glGetString(GL_RENDERER));
	}

	ret = load_shaders(args);
	if (ret != EXIT_SUCCESS)
		return ret;

	glClearColor(0, 0, 0, 0);
	glViewport(0, 0, width, height);

	ufrag_n = glGetUniformLocation(program, "frag_n");
	glUniform1i(ufrag_n, frag_n);
	if (glGetError() != GL_NO_ERROR) {
		pr_fail("%s: failed to get the storage location of %d\n",
		       args->name, frag_n);
		return EXIT_FAILURE;
	}

	apos = glGetAttribLocation(program, "pos");
	glEnableVertexAttribArray(apos);
	glVertexAttribPointer(apos, 4, GL_FLOAT, 0, 0, vertex);

	acolor = glGetAttribLocation(program, "color");
	glEnableVertexAttribArray(acolor);
	glVertexAttribPointer(acolor, 4, GL_FLOAT, 0, 0, color);

	if (texsize > 0) {
		GLint maxsize, bytesPerImage;
		GLuint texobj = 0;

		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxsize);
		if (texsize > maxsize) {
			pr_inf("%s: image size %u exceeds maximum texture size %u\n",
				args->name, (unsigned int)texsize, (unsigned int)maxsize);
			return EXIT_FAILURE;
		}

		texobj = 0;
		glGenTextures(1, &texobj);
		glBindTexture(GL_TEXTURE_2D, texobj);

		bytesPerImage = texsize * texsize * 4;
		teximage = malloc(bytesPerImage);
		if (!teximage) {
			pr_inf("%s: failed to allocate teximage, skipping stressor\n", args->name);
			return EXIT_NO_RESOURCE;
		}
	}
	return EXIT_SUCCESS;
}

static void stress_gpu_run(const GLsizei texsize, const GLsizei uploads)
{
	if (texsize > 0) {
		int i;

		for (i = 0; i < uploads; i++) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texsize,
				     texsize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
				     teximage);
		}
	}
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glFinish();
}

int get_config(const stress_args_t *args, EGLConfig *config)
{
	static const EGLint egl_config_attribs[] = {
		EGL_BUFFER_SIZE, 32,
		EGL_DEPTH_SIZE, EGL_DONT_CARE,
		EGL_STENCIL_SIZE, EGL_DONT_CARE,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE,
	};

	int i;
	EGLint num_configs;
	EGLConfig *configs;

	if (eglGetConfigs(display, NULL, 0, &num_configs) == EGL_FALSE) {
		pr_inf("%s: EGL: no EGL configs found, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	configs = malloc(num_configs * sizeof(EGLConfig));
	if ((eglChooseConfig(display, egl_config_attribs,
			     configs, num_configs,
			     &num_configs) == EGL_FALSE) || (num_configs == 0)) {
		pr_inf("%s: EGL: can't choose EGL config, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i < num_configs; ++i) {
		EGLint gbm_format;

		if (eglGetConfigAttrib(display, configs[i],
				       EGL_NATIVE_VISUAL_ID,
				       &gbm_format) == EGL_FALSE) {
			pr_inf("%s: EGL: eglGetConfigAttrib failed, skipping stressor\n", args->name);
			return EXIT_NO_RESOURCE;
		}

		if (gbm_format != GBM_FORMAT_ARGB8888)
			continue;

		*config = configs[i];
		free(configs);

		return EXIT_SUCCESS;
	}

	pr_inf("%s: EGL: cannot get configuration, skipping stressor\n", args->name);
	return EXIT_NO_RESOURCE;
}

static int egl_init(
	const stress_args_t *args,
	const uint32_t size_x,
	const uint32_t size_y)
{
	int ret, fd;
	EGLConfig config;
	EGLContext context;
	EGLint majorVersion;
	EGLint minorVersion;

	static const EGLint contextAttribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	fd = open(devicenode, O_RDWR);
	if (fd < 0) {
		pr_inf("%s: couldn't open %s, skipping stressor\n", args->name, devicenode);
		return EXIT_NO_RESOURCE;
	}

	gbm = gbm_create_device(fd);
	if (!gbm) {
		pr_inf("%s: couldn't create gbm device, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (display == EGL_NO_DISPLAY) {
		pr_inf("%s: EGL: eglGetPlatformDisplay failed with vendor, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	if (eglInitialize(display, &majorVersion, &minorVersion) == EGL_FALSE) {
		pr_inf("%s: EGL: failed to initialize EGL, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		pr_inf("%s: EGL: Failed to bind OpenGL ES\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	ret = get_config(args, &config);
	if (ret != EXIT_SUCCESS)
		return ret;

	gs = gbm_surface_create(gbm, size_x, size_y, GBM_BO_FORMAT_ARGB8888,
				GBM_BO_USE_LINEAR | GBM_BO_USE_SCANOUT |
				GBM_BO_USE_RENDERING);
	if (!gs) {
		pr_inf("%s: could not create gbm surface, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	surface = eglCreatePlatformWindowSurface(display, config, gs, NULL);
	if (surface == EGL_NO_SURFACE) {
		pr_inf("%s: EGL: Failed to allocate surface\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
	if (context == EGL_NO_CONTEXT) {
		pr_inf("%s: EGL: Failed to create context\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
		pr_inf("%s: EGL: Failed to make context current\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	return EXIT_SUCCESS;
}

static int stress_gpu(const stress_args_t *args)
{
	int frag_n = 0;
	int ret;
	uint32_t size_x = 256;
	uint32_t size_y = 256;
	GLsizei texsize = 4096;
	GLsizei uploads = 1;

	(void)stress_get_setting("gpu-frag", &frag_n);
	(void)stress_get_setting("gpu-xsize", &size_x);
	(void)stress_get_setting("gpu-ysize", &size_y);
	(void)stress_get_setting("gpu-tex-size", &texsize);
	(void)stress_get_setting("gpu-upload", &uploads);

	ret = egl_init(args, size_x, size_y);
	if (ret != EXIT_SUCCESS)
		goto deinit;

	ret = gles2_init(args, size_x, size_y, frag_n, texsize);
	if (ret != EXIT_SUCCESS)
		goto deinit;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_gpu_run(texsize, uploads);
		if (glGetError() != GL_NO_ERROR)
			return EXIT_NO_RESOURCE;
		inc_counter(args);
	} while (keep_stressing(args));

	ret = EXIT_SUCCESS;
deinit:
	if (teximage)
		free(teximage);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return ret;
}

stressor_info_t stress_gpu_info = {
	.stressor = stress_gpu,
	.class = CLASS_GPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_gpu_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_GPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif