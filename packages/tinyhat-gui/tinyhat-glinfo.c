#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <string.h>

#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02

typedef const unsigned char *(*getstring_t)(unsigned int);

static int probe(EGLDisplay dpy, EGLenum api, EGLint renderable, const EGLint *ctxattr, int *major, int *minor, char *renderer, size_t size)
{
	EGLint cfgattr[] = {EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, renderable, EGL_NONE};
	EGLConfig cfg;
	EGLint n = 0;
	if (!eglBindAPI(api) || !eglChooseConfig(dpy, cfgattr, &cfg, 1, &n) || n < 1)
		return 0;
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
	if (ctx == EGL_NO_CONTEXT)
		return 0;
	int ok = 0;
	if (eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		getstring_t getstring = (getstring_t)eglGetProcAddress("glGetString");
		const char *version = getstring ? (const char *)getstring(GL_VERSION) : NULL;
		const char *name = getstring ? (const char *)getstring(GL_RENDERER) : NULL;
		if (version) {
			const char *p = version;
			if (strncmp(p, "OpenGL ES ", 10) == 0)
				p += 10;
			ok = sscanf(p, "%d.%d", major, minor) == 2;
		}
		if (ok && name)
			snprintf(renderer, size, "%s", name);
		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	}
	eglDestroyContext(dpy, ctx);
	return ok;
}

int main(void)
{
	PFNEGLGETPLATFORMDISPLAYEXTPROC getdisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay dpy = getdisplay ? getdisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
	int gl_major = 0, gl_minor = 0, core_major = 0, core_minor = 0, es_major = 0, es_minor = 0;
	const EGLint es_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
	const EGLint core_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
	char renderer[256] = "";
	if (dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL)) {
		probe(dpy, EGL_OPENGL_ES_API, EGL_OPENGL_ES2_BIT, es_attr, &es_major, &es_minor, renderer, sizeof renderer);
		probe(dpy, EGL_OPENGL_API, EGL_OPENGL_BIT, NULL, &gl_major, &gl_minor, renderer, sizeof renderer);
		if (probe(dpy, EGL_OPENGL_API, EGL_OPENGL_BIT, core_attr, &core_major, &core_minor, renderer, sizeof renderer) &&
		    core_major * 100 + core_minor > gl_major * 100 + gl_minor) {
			gl_major = core_major;
			gl_minor = core_minor;
		}
		eglTerminate(dpy);
	}
	for (char *c = renderer; *c; c++)
		if (*c == '\'' || *c == '\n')
			*c = ' ';
	int software = strstr(renderer, "llvmpipe") || strstr(renderer, "softpipe") || strstr(renderer, "Software") || !*renderer;
	printf("TINYHAT_GL_VERSION=%d.%d\n", gl_major, gl_minor);
	printf("TINYHAT_GLES_VERSION=%d.%d\n", es_major, es_minor);
	printf("TINYHAT_GL_RENDERER='%s'\n", renderer);
	printf("TINYHAT_GL_SOFTWARE=%d\n", software);
	return 0;
}
