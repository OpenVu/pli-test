/*
 * gGLDC - OpenGL ES 2.0 device context for enigma2
 * =================================================
 *
 * كل ما يخص EGL / GLES محبوس في هذا الملف.
 *
 * خريطة الأوامر (المرحلة 1):
 *
 *   clear        -> مستطيل GL بلون الخلفية على كامل منطقة القص
 *   fill         -> مستطيل GL بلون المقدمة
 *   fillRegion   -> نفس الشيء لكل مستطيل في المنطقة
 *   rectangle    -> مستطيل GL مع تدرج + حواف دائرية + إطار (شيدر واحد)
 *   setClip / addClip / popClip / setOffset / setForegroundColor
 *   setBackgroundColor / setGradient / setRadius / setBorder
 *                -> إدارة حالة في gDC (نستعملها نحن)
 *   flush/waitVSync -> تركيب الطبقة البرمجية + eglSwapBuffers أو gles_flush
 *
 *   blit / renderText / renderPara / line / palette
 *                -> ما زالت CPU داخل الطبقة البرمجية (RAM) ثم تُركَّب فوق GL.
 *                   (المرحلة 2: blit عبر كاش تكسشرات، المرحلة 3: أطلس حروف)
 */

#ifndef EGL_NO_X11
#define EGL_NO_X11
#endif
#ifndef MESA_EGL_NO_X11_HEADERS
#define MESA_EGL_NO_X11_HEADERS
#endif

#include <lib/gdi/gldc.h>

#include <lib/base/init.h>
#include <lib/base/init_num.h>
#include <lib/base/eerror.h>

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

/* ============================================================================
 * الشيدرات
 * ==========================================================================*/

static const char *kVertexShader =
	"attribute vec2 aPos;\n"
	"attribute vec2 aTex;\n"
	"uniform mat4 uProj;\n"
	"varying vec2 vTex;\n"
	"varying vec2 vPix;\n"
	"void main()\n"
	"{\n"
	"    vTex = aTex;\n"
	"    vPix = aPos;\n"
	"    gl_Position = uProj * vec4(aPos, 0.0, 1.0);\n"
	"}\n";

/*
 * شيدر المستطيل: يغطي fill / clear / rectangle كلها.
 *  - uRadius: نصف القطر لكل زاوية (TL, TR, BL, BR) بالبكسل
 *  - uBorderW: عرض الإطار بالبكسل (0 = بلا إطار)
 *  - uGradMode: 0 بلا تدرج، 1 عمودي، 2 أفقي
 *  - uGradSize: مقام التدرج بالبكسل (gradientFullSize أو بُعد المستطيل)
 *  - uAA: 1 لتفعيل تنعيم الحواف (فقط مع الحواف الدائرية)
 */
static const char *kRectShader =
	"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
	"precision highp float;\n"
	"#else\n"
	"precision mediump float;\n"
	"#endif\n"
	"varying vec2 vPix;\n"
	"uniform vec2 uRectPos;\n"
	"uniform vec2 uRectSize;\n"
	"uniform vec4 uCol0;\n"
	"uniform vec4 uCol1;\n"
	"uniform vec4 uCol2;\n"
	"uniform float uGradMode;\n"
	"uniform float uGradSize;\n"
	"uniform float uGradOffset;\n"
	"uniform vec4 uRadius;\n"
	"uniform float uBorderW;\n"
	"uniform vec4 uBorderCol0;\n"
	"uniform vec4 uBorderCol1;\n"
	"uniform float uBorderGradMode;\n"
	"uniform float uAA;\n"
	"void main()\n"
	"{\n"
	"    vec2 p  = vPix - uRectPos;\n"
	"    vec2 hs = uRectSize * 0.5;\n"
	"    vec2 c  = p - hs;\n"
	"    float r;\n"
	"    if (c.x < 0.0) r = (c.y < 0.0) ? uRadius.x : uRadius.z;\n"
	"    else           r = (c.y < 0.0) ? uRadius.y : uRadius.w;\n"
	"    vec2 q = abs(c) - (hs - vec2(r));\n"
	"    float d = min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;\n"
	"    float outer = (uAA > 0.5) ? clamp(0.5 - d, 0.0, 1.0) : 1.0;\n"
	"    float inner = (uBorderW > 0.0) ? clamp(0.5 - (d + uBorderW), 0.0, 1.0) : outer;\n"
	"    float ring  = clamp(outer - inner, 0.0, 1.0);\n"
	"    float gpos  = (uGradMode < 1.5) ? p.y : p.x;\n"
	"    float t     = clamp((gpos + uGradOffset) / max(uGradSize, 1.0), 0.0, 1.0);\n"
	"    vec4 fillCol;\n"
	"    if (uGradMode < 0.5)      fillCol = uCol0;\n"
	"    else if (t < 0.5)         fillCol = mix(uCol0, uCol1, t * 2.0);\n"
	"    else                      fillCol = mix(uCol1, uCol2, (t - 0.5) * 2.0);\n"
	"    vec4 bCol = uBorderCol0;\n"
	"    if (uBorderGradMode > 0.5) {\n"
	"        float bpos = (uBorderGradMode < 1.5) ? p.y : p.x;\n"
	"        bCol = mix(uBorderCol0, uBorderCol1, clamp(bpos / max(uGradSize, 1.0), 0.0, 1.0));\n"
	"    }\n"
	"    vec4 col = mix(fillCol, bCol, ring);\n"
	"    col.a *= outer;\n"
	"    gl_FragColor = col;\n"
	"}\n";

/*
 * شيدر التكسشر: يستعمل حالياً لتركيب الطبقة البرمجية.
 * الذاكرة عندنا BGRA (ترتيب gRGB على little-endian) فنقلبها في الشيدر
 * بدل الاعتماد على امتداد GL_BGRA_EXT غير المضمون.
 */
static const char *kTexShader =
	"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
	"precision highp float;\n"
	"#else\n"
	"precision mediump float;\n"
	"#endif\n"
	"varying vec2 vTex;\n"
	"uniform sampler2D uTex;\n"
	"uniform float uSwizzleBGRA;\n"
	"void main()\n"
	"{\n"
	"    vec4 t = texture2D(uTex, vTex);\n"
	"    gl_FragColor = (uSwizzleBGRA > 0.5) ? t.bgra : t;\n"
	"}\n";

/* ============================================================================
 * حالة الواجهة الخلفية
 * ==========================================================================*/

enum
{
	BLEND_UNKNOWN = -1,
	BLEND_OFF = 0,
	BLEND_STRAIGHT = 1,	  /* src.a, 1-src.a  : ألوان غير مضروبة مسبقاً */
	BLEND_PREMULTIPLIED = 2 /* 1, 1-src.a     : الطبقة البرمجية */
};

struct gGLBackend
{
	EGLDisplay display;
	EGLSurface surface;
	EGLContext context;

	void *vugles_lib;
	int (*gles_open)(void);
	void (*gles_close)(void);
	void (*gles_flush)(void);
	void (*gles_state_open)(void);
	bool using_vugles;

	GLuint rectProg;
	GLint r_aPos, r_aTex;
	GLint r_uProj, r_uRectPos, r_uRectSize;
	GLint r_uCol0, r_uCol1, r_uCol2;
	GLint r_uGradMode, r_uGradSize, r_uGradOffset;
	GLint r_uRadius, r_uBorderW, r_uBorderCol0, r_uBorderCol1, r_uBorderGradMode;
	GLint r_uAA;

	GLuint texProg;
	GLint t_aPos, t_aTex;
	GLint t_uProj, t_uTex, t_uSwizzle;

	GLuint softTex;
	int softTexW, softTexH;

	float proj[16];
	int vpW, vpH;

	GLuint curProg;
	int blendMode;
	bool scissorOn;

	gGLBackend()
		: display(EGL_NO_DISPLAY), surface(EGL_NO_SURFACE), context(EGL_NO_CONTEXT),
		  vugles_lib(0), gles_open(0), gles_close(0), gles_flush(0), gles_state_open(0),
		  using_vugles(false),
		  rectProg(0), texProg(0), softTex(0), softTexW(0), softTexH(0),
		  vpW(0), vpH(0), curProg(0), blendMode(BLEND_UNKNOWN), scissorOn(false)
	{
		memset(proj, 0, sizeof(proj));
	}
};

/* ============================================================================
 * أدوات صغيرة
 * ==========================================================================*/

static GLuint glCompile(GLenum type, const char *src, const char *name)
{
	GLuint sh = glCreateShader(type);
	if (!sh)
	{
		eDebug("[gGLDC] glCreateShader failed for %s", name);
		return 0;
	}
	glShaderSource(sh, 1, &src, 0);
	glCompileShader(sh);

	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		GLint len = 0;
		glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
		if (len > 1)
		{
			char *log = (char *)malloc(len);
			if (log)
			{
				glGetShaderInfoLog(sh, len, 0, log);
				eDebug("[gGLDC] shader '%s' compile error: %s", name, log);
				free(log);
			}
		}
		else
			eDebug("[gGLDC] shader '%s' compile error (no log)", name);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

static GLuint glLink(const char *vs, const char *fs, const char *name)
{
	GLuint v = glCompile(GL_VERTEX_SHADER, vs, name);
	if (!v)
		return 0;
	GLuint f = glCompile(GL_FRAGMENT_SHADER, fs, name);
	if (!f)
	{
		glDeleteShader(v);
		return 0;
	}

	GLuint prog = glCreateProgram();
	glAttachShader(prog, v);
	glAttachShader(prog, f);
	glLinkProgram(prog);

	GLint ok = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		GLint len = 0;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
		if (len > 1)
		{
			char *log = (char *)malloc(len);
			if (log)
			{
				glGetProgramInfoLog(prog, len, 0, log);
				eDebug("[gGLDC] program '%s' link error: %s", name, log);
				free(log);
			}
		}
		glDeleteProgram(prog);
		prog = 0;
	}

	glDeleteShader(v);
	glDeleteShader(f);
	return prog;
}

/* gRGB.a في enigma2 هي "الشفافية": 0 = معتم تماماً، 255 = شفاف تماماً.
   (انظر gPixmap::fill حيث يُعمل col ^= 0xFF000000 قبل الكتابة) */
static inline void colorToGL(const gRGB &c, float *out)
{
	out[0] = c.r / 255.0f;
	out[1] = c.g / 255.0f;
	out[2] = c.b / 255.0f;
	out[3] = 1.0f - (c.a / 255.0f);
}

static inline void makeOrtho(float *m, int w, int h)
{
	/* إحداثيات بكسل، الأصل أعلى-يسار، y إلى الأسفل */
	memset(m, 0, sizeof(float) * 16);
	m[0] = 2.0f / (w ? w : 1);
	m[5] = -2.0f / (h ? h : 1);
	m[10] = -1.0f;
	m[12] = -1.0f;
	m[13] = 1.0f;
	m[15] = 1.0f;
}

/* ============================================================================
 * gGLDC
 * ==========================================================================*/

gGLDC::gGLDC()
	: fb(0), m_gl(0), m_soft_data(0), m_soft_size(0),
	  m_dirty_top(0), m_dirty_bottom(0), m_dirty_valid(false), m_soft_active(false),
	  m_xres(0), m_yres(0), m_bpp(32),
	  m_gl_ready(false), m_gl_failed(false),
	  brightness(128), gamma(128), alpha(255)
{
	m_gl = new gGLBackend();

	fb = new fbClass;
	if (!fb->Available())
	{
		eDebug("[gGLDC] Warning: no framebuffer device; relying on EGL only.");
	}

	int xres = 1920, yres = 1080, bpp = 32;
	if (fb->Available())
	{
		fb->getMode(xres, yres, bpp);
		if (!((xres == 720 && yres == 576) || (xres == 1280 && yres == 720) || (xres == 1920 && yres == 1080)))
		{
			xres = 1280;
			yres = 720;
		}
	}

	surface.clut.data = 0;
	setResolution(xres, yres, 32);

	reloadSettings();

	eDebug("[gGLDC] created, %dx%d (GLES backend, lazy EGL init on render thread)", m_xres, m_yres);
}

gGLDC::~gGLDC()
{
	shutdownGL();
	delete m_gl;
	m_gl = 0;

	m_pixmap = 0;

	if (m_soft_data)
	{
		free(m_soft_data);
		m_soft_data = 0;
	}
	delete[] surface.clut.data;
	surface.clut.data = 0;

	delete fb;
	fb = 0;
}

void gGLDC::setResolution(int xres, int yres, int bpp)
{
	(void)bpp; /* الطبقة البرمجية دائماً 32bpp */

	if (m_pixmap && m_xres == xres && m_yres == yres)
		return;

	if (fb && fb->Available())
		fb->SetMode(xres, yres, 32);

	m_xres = xres;
	m_yres = yres;
	m_bpp = 32;

	surface.x = xres;
	surface.y = yres;
	surface.bpp = 32;
	surface.bypp = 4;
	surface.stride = xres * 4;
	surface.data_phys = 0;
	surface.transparent = true;

	int need = surface.stride * surface.y;
	if (need != m_soft_size)
	{
		if (m_soft_data)
			free(m_soft_data);
		m_soft_data = (unsigned char *)malloc(need);
		m_soft_size = need;
	}
	if (!m_soft_data)
		eFatal("[gGLDC] out of memory allocating %d bytes for the software layer", need);

	memset(m_soft_data, 0, need);
	surface.data = m_soft_data;

	m_dirty_top = 0;
	m_dirty_bottom = 0;
	m_dirty_valid = false;
	m_soft_active = false;

	if (!surface.clut.data)
	{
		surface.clut.colors = 256;
		surface.clut.data = new gRGB[surface.clut.colors];
		memset(static_cast<void *>(surface.clut.data), 0, sizeof(*surface.clut.data) * surface.clut.colors);
	}

	m_pixmap = new gPixmap(&surface);

	/* أعِد ضبط الـ viewport في الإطار التالي */
	if (m_gl)
	{
		m_gl->vpW = 0;
		m_gl->vpH = 0;
		if (m_gl->softTex)
		{
			glDeleteTextures(1, &m_gl->softTex);
			m_gl->softTex = 0;
			m_gl->softTexW = 0;
			m_gl->softTexH = 0;
		}
	}

	eDebug("[gGLDC] resolution set to %dx%dx32", m_xres, m_yres);
}

/* ---------------------------------------------------------------------------
 * تهيئة EGL — تُستدعى من خيط gRC عند أول أمر رسم
 * -------------------------------------------------------------------------*/

bool gGLDC::ensureGL()
{
	if (m_gl_ready)
		return true;
	if (m_gl_failed)
		return false;

	gGLBackend &g = *m_gl;

	eDebug("[gGLDC] ===== initialising GLES backend =====");

	/* 1) المسار المغلق (Vu+ / بعض الأجهزة المبنية على Broadcom): libvugles2.so */
	if (!g.vugles_lib)
	{
		g.vugles_lib = dlopen("libvugles2.so", RTLD_NOW | RTLD_GLOBAL);
		if (g.vugles_lib)
		{
			g.gles_open = (int (*)(void))dlsym(g.vugles_lib, "_Z9gles_openv");
			g.gles_close = (void (*)(void))dlsym(g.vugles_lib, "_Z10gles_closev");
			g.gles_flush = (void (*)(void))dlsym(g.vugles_lib, "_Z10gles_flushv");
			g.gles_state_open = (void (*)(void))dlsym(g.vugles_lib, "_Z15gles_state_openv");
			eDebug("[gGLDC] libvugles2.so loaded (open=%p flush=%p state_open=%p)",
				   (void *)g.gles_open, (void *)g.gles_flush, (void *)g.gles_state_open);
		}
		else
		{
			const char *err = dlerror();
			eDebug("[gGLDC] libvugles2.so not available (%s) - using standard EGL", err ? err : "no error");
		}
	}

	if (g.gles_open)
	{
		int rc = g.gles_open();
		eDebug("[gGLDC] gles_open() returned %d", rc);
		if (rc == 1)
		{
			g.using_vugles = true;
			if (g.gles_state_open)
				g.gles_state_open();
		}
		else
			eDebug("[gGLDC] gles_open() failed, falling back to standard EGL");
	}

	/* 2) المسار القياسي */
	if (!g.using_vugles)
	{
		g.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		if (g.display == EGL_NO_DISPLAY)
		{
			eDebug("[gGLDC] FATAL: eglGetDisplay failed");
			m_gl_failed = true;
			return false;
		}

		EGLint major = 0, minor = 0;
		if (!eglInitialize(g.display, &major, &minor))
		{
			eDebug("[gGLDC] FATAL: eglInitialize failed (0x%04x)", eglGetError());
			g.display = EGL_NO_DISPLAY;
			m_gl_failed = true;
			return false;
		}
		eDebug("[gGLDC] EGL %d.%d - vendor: %s", major, minor,
			   eglQueryString(g.display, EGL_VENDOR) ? eglQueryString(g.display, EGL_VENDOR) : "?");

		eglBindAPI(EGL_OPENGL_ES_API);

		EGLConfig config = 0;
		EGLint numConfig = 0;
		EGLint attribs[] = {
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_NONE};
		eglChooseConfig(g.display, attribs, &config, 1, &numConfig);

		if (!numConfig)
		{
			EGLint fb2[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE};
			eglChooseConfig(g.display, fb2, &config, 1, &numConfig);
		}
		if (!numConfig)
		{
			EGLint fb3[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
			eglChooseConfig(g.display, fb3, &config, 1, &numConfig);
		}
		if (!numConfig)
		{
			eDebug("[gGLDC] FATAL: no usable EGL config");
			m_gl_failed = true;
			return false;
		}

		EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
		g.context = eglCreateContext(g.display, config, EGL_NO_CONTEXT, ctxAttribs);
		if (g.context == EGL_NO_CONTEXT)
		{
			eDebug("[gGLDC] FATAL: eglCreateContext failed (0x%04x)", eglGetError());
			m_gl_failed = true;
			return false;
		}

		g.surface = eglCreateWindowSurface(g.display, config, (EGLNativeWindowType)0, NULL);
		if (g.surface == EGL_NO_SURFACE)
		{
			eDebug("[gGLDC] eglCreateWindowSurface failed (0x%04x), trying pbuffer", eglGetError());
			EGLint pb[] = {EGL_WIDTH, m_xres, EGL_HEIGHT, m_yres, EGL_NONE};
			g.surface = eglCreatePbufferSurface(g.display, config, pb);
		}
		if (g.surface == EGL_NO_SURFACE)
		{
			eDebug("[gGLDC] FATAL: no EGL surface (0x%04x)", eglGetError());
			m_gl_failed = true;
			return false;
		}

		if (!eglMakeCurrent(g.display, g.surface, g.surface, g.context))
		{
			eDebug("[gGLDC] FATAL: eglMakeCurrent failed (0x%04x)", eglGetError());
			m_gl_failed = true;
			return false;
		}
		eglSwapInterval(g.display, 1);
	}

	const char *ver = (const char *)glGetString(GL_VERSION);
	const char *ren = (const char *)glGetString(GL_RENDERER);
	eDebug("[gGLDC] GL_VERSION=%s GL_RENDERER=%s", ver ? ver : "NULL", ren ? ren : "NULL");
	if (!ver)
	{
		eDebug("[gGLDC] FATAL: no GL context is current");
		m_gl_failed = true;
		return false;
	}

	/* 3) الشيدرات */
	g.rectProg = glLink(kVertexShader, kRectShader, "rect");
	g.texProg = glLink(kVertexShader, kTexShader, "tex");
	if (!g.rectProg || !g.texProg)
	{
		eDebug("[gGLDC] FATAL: shader setup failed");
		m_gl_failed = true;
		return false;
	}

	g.r_aPos = glGetAttribLocation(g.rectProg, "aPos");
	g.r_aTex = glGetAttribLocation(g.rectProg, "aTex");
	g.r_uProj = glGetUniformLocation(g.rectProg, "uProj");
	g.r_uRectPos = glGetUniformLocation(g.rectProg, "uRectPos");
	g.r_uRectSize = glGetUniformLocation(g.rectProg, "uRectSize");
	g.r_uCol0 = glGetUniformLocation(g.rectProg, "uCol0");
	g.r_uCol1 = glGetUniformLocation(g.rectProg, "uCol1");
	g.r_uCol2 = glGetUniformLocation(g.rectProg, "uCol2");
	g.r_uGradMode = glGetUniformLocation(g.rectProg, "uGradMode");
	g.r_uGradSize = glGetUniformLocation(g.rectProg, "uGradSize");
	g.r_uGradOffset = glGetUniformLocation(g.rectProg, "uGradOffset");
	g.r_uRadius = glGetUniformLocation(g.rectProg, "uRadius");
	g.r_uBorderW = glGetUniformLocation(g.rectProg, "uBorderW");
	g.r_uBorderCol0 = glGetUniformLocation(g.rectProg, "uBorderCol0");
	g.r_uBorderCol1 = glGetUniformLocation(g.rectProg, "uBorderCol1");
	g.r_uBorderGradMode = glGetUniformLocation(g.rectProg, "uBorderGradMode");
	g.r_uAA = glGetUniformLocation(g.rectProg, "uAA");

	g.t_aPos = glGetAttribLocation(g.texProg, "aPos");
	g.t_aTex = glGetAttribLocation(g.texProg, "aTex");
	g.t_uProj = glGetUniformLocation(g.texProg, "uProj");
	g.t_uTex = glGetUniformLocation(g.texProg, "uTex");
	g.t_uSwizzle = glGetUniformLocation(g.texProg, "uSwizzleBGRA");

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_DITHER);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	g.blendMode = BLEND_OFF;
	g.scissorOn = false;
	g.curProg = 0;

	glViewport(0, 0, m_xres, m_yres);
	makeOrtho(g.proj, m_xres, m_yres);
	g.vpW = m_xres;
	g.vpH = m_yres;

	/* شاشة شفافة تماماً: الفيديو خلف طبقة الـ OSD يجب أن يظهر */
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	m_gl_ready = true;
	eDebug("[gGLDC] ===== GLES backend ready (%s) =====",
		   g.using_vugles ? "libvugles2" : "standard EGL");
	return true;
}

void gGLDC::shutdownGL()
{
	if (!m_gl)
		return;
	gGLBackend &g = *m_gl;

	/* ملاحظة: لا نستدعي glDelete* هنا. الاستدعاء يأتي من الخيط الرئيسي بينما
	   سياق GL مربوط بخيط gRC الذي انتهى؛ حذف الكائنات بلا سياق حالٍ يعني
	   سلوكاً غير معرَّف. إنهاء EGL يحرر كل شيء على أي حال. */
	g.softTex = 0;
	g.rectProg = 0;
	g.texProg = 0;

	if (g.using_vugles)
	{
		if (g.gles_close)
			g.gles_close();
		g.using_vugles = false;
	}
	else if (g.display != EGL_NO_DISPLAY)
	{
		eglMakeCurrent(g.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (g.context != EGL_NO_CONTEXT)
			eglDestroyContext(g.display, g.context);
		if (g.surface != EGL_NO_SURFACE)
			eglDestroySurface(g.display, g.surface);
		eglTerminate(g.display);
	}
	g.display = EGL_NO_DISPLAY;
	g.context = EGL_NO_CONTEXT;
	g.surface = EGL_NO_SURFACE;

	m_gl_ready = false;
}

void gGLDC::present()
{
	gGLBackend &g = *m_gl;
	if (g.using_vugles)
	{
		if (g.gles_flush)
			g.gles_flush();
		/* المكتبة المغلقة قد تغيّر حالة GL من تحتنا */
		g.curProg = 0;
		g.blendMode = BLEND_UNKNOWN;
		g.scissorOn = false;
	}
	else if (g.display != EGL_NO_DISPLAY && g.surface != EGL_NO_SURFACE)
		eglSwapBuffers(g.display, g.surface);
}

/* ---------------------------------------------------------------------------
 * ضبط الحالة
 * -------------------------------------------------------------------------*/

static inline void useProgram(gGLBackend &g, GLuint p)
{
	if (g.curProg != p)
	{
		glUseProgram(p);
		g.curProg = p;
	}
}

static inline void setBlend(gGLBackend &g, int mode)
{
	if (g.blendMode == mode)
		return;
	g.blendMode = mode;
	if (mode == BLEND_OFF)
	{
		glDisable(GL_BLEND);
		return;
	}
	glEnable(GL_BLEND);
	if (mode == BLEND_PREMULTIPLIED)
		glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	else
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

static inline void setScissor(gGLBackend &g, const eRect &r, int screenH)
{
	if (!g.scissorOn)
	{
		glEnable(GL_SCISSOR_TEST);
		g.scissorOn = true;
	}
	int y = screenH - r.bottom();
	if (y < 0)
		y = 0;
	int w = r.width();
	int h = r.height();
	if (w < 0)
		w = 0;
	if (h < 0)
		h = 0;
	glScissor(r.left(), y, w, h);
}

static inline void clearScissor(gGLBackend &g)
{
	if (g.scissorOn)
	{
		glDisable(GL_SCISSOR_TEST);
		g.scissorOn = false;
	}
}

/* رسم رباعي بإحداثيات بكسل (مصفوفات من ذاكرة العميل - مسموح في GLES2) */
static void drawQuad(gGLBackend &g, GLint aPos, GLint aTex,
					 float x, float y, float w, float h,
					 float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f)
{
	const float pos[8] = {
		x, y,
		x + w, y,
		x, y + h,
		x + w, y + h};
	const float tex[8] = {
		u0, v0,
		u1, v0,
		u0, v1,
		u1, v1};

	glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 0, pos);
	glEnableVertexAttribArray(aPos);
	if (aTex >= 0)
	{
		glVertexAttribPointer(aTex, 2, GL_FLOAT, GL_FALSE, 0, tex);
		glEnableVertexAttribArray(aTex);
	}
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	(void)g;
}

/* ---------------------------------------------------------------------------
 * الطبقة البرمجية
 * -------------------------------------------------------------------------*/

void gGLDC::softMarkDirty(const eRect &area)
{
	if (!area.valid() || area.empty())
		return;
	int top = area.top();
	int bottom = area.bottom();
	if (top < 0)
		top = 0;
	if (bottom > m_yres)
		bottom = m_yres;
	if (bottom <= top)
		return;

	m_soft_active = true;

	if (!m_dirty_valid)
	{
		m_dirty_top = top;
		m_dirty_bottom = bottom;
		m_dirty_valid = true;
	}
	else
	{
		if (top < m_dirty_top)
			m_dirty_top = top;
		if (bottom > m_dirty_bottom)
			m_dirty_bottom = bottom;
	}
}

void gGLDC::softMarkDirtyRegion(const gRegion &region)
{
	for (unsigned int i = 0; i < region.rects.size(); ++i)
		softMarkDirty(region.rects[i]);
}

void gGLDC::softEraseRect(const eRect &area)
{
	if (!m_soft_active)
		return; /* لا شيء في الطبقة أصلاً */
	if (!area.valid() || area.empty())
		return;

	int left = area.left() < 0 ? 0 : area.left();
	int top = area.top() < 0 ? 0 : area.top();
	int right = area.right() > m_xres ? m_xres : area.right();
	int bottom = area.bottom() > m_yres ? m_yres : area.bottom();
	if (right <= left || bottom <= top)
		return;

	const int bytes = (right - left) * 4;
	for (int y = top; y < bottom; ++y)
		memset(m_soft_data + y * surface.stride + left * 4, 0, bytes);

	softMarkDirty(eRect(left, top, right - left, bottom - top));
}

void gGLDC::softErase(const gRegion &region)
{
	for (unsigned int i = 0; i < region.rects.size(); ++i)
		softEraseRect(region.rects[i]);
}

/*
 * صمام أمان: إن فشلت تهيئة GLES تماماً على هذا الجهاز، ننسخ الطبقة البرمجية
 * إلى /dev/fb0 عند كل flush. عندها يتصرف gGLDC تماماً كـ gFBDC القديم
 * (رسم بالمعالج) بدل أن يعطينا شاشة سوداء.
 */
void gGLDC::softBlitToFb()
{
	if (!fb || !fb->Available() || !fb->lfb || !m_soft_data)
		return;

	const int fbStride = (int)fb->Stride();
	const int rowBytes = (m_xres * 4 < fbStride) ? m_xres * 4 : fbStride;

	int top = m_dirty_valid ? m_dirty_top : 0;
	int bottom = m_dirty_valid ? m_dirty_bottom : m_yres;
	if (bottom > m_yres)
		bottom = m_yres;
	if (top < 0)
		top = 0;

	for (int y = top; y < bottom; ++y)
		memcpy(fb->lfb + (size_t)y * fbStride,
			   m_soft_data + (size_t)y * surface.stride,
			   rowBytes);

	m_dirty_valid = false;
	fb->blit();
}

void gGLDC::composeSoftLayer()
{
	if (!m_soft_active)
		return;

	gGLBackend &g = *m_gl;

	if (!g.softTex)
	{
		glGenTextures(1, &g.softTex);
		glBindTexture(GL_TEXTURE_2D, g.softTex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_xres, m_yres, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_soft_data);
		g.softTexW = m_xres;
		g.softTexH = m_yres;
	}
	else if (m_dirty_valid && m_dirty_bottom > m_dirty_top)
	{
		glBindTexture(GL_TEXTURE_2D, g.softTex);
		/* stride == width*4، إذن الأسطر متلاصقة ويمكن رفعها مباشرة */
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexSubImage2D(GL_TEXTURE_2D, 0,
						0, m_dirty_top, m_xres, m_dirty_bottom - m_dirty_top,
						GL_RGBA, GL_UNSIGNED_BYTE,
						m_soft_data + (size_t)m_dirty_top * surface.stride);
	}
	m_dirty_valid = false;

	clearScissor(g);
	useProgram(g, g.texProg);
	/* محتوى الطبقة مضروب مسبقاً في ألفا (انظر eTextPara::blit) */
	setBlend(g, BLEND_PREMULTIPLIED);

	glUniformMatrix4fv(g.t_uProj, 1, GL_FALSE, g.proj);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g.softTex);
	glUniform1i(g.t_uTex, 0);
	glUniform1f(g.t_uSwizzle, 1.0f); /* الذاكرة BGRA */

	drawQuad(g, g.t_aPos, g.t_aTex, 0.0f, 0.0f, (float)m_xres, (float)m_yres);
}

/* ---------------------------------------------------------------------------
 * الرسم بـ GL
 * -------------------------------------------------------------------------*/

void gGLDC::glDrawRect(const gRegion &clip, const eRect &area,
					   const gRGB &background, const gRGB &borderColor, int borderWidth,
					   const std::vector<gRGB> &gradientColors, uint8_t direction,
					   int radius, uint8_t edges, bool alphablend, int gradientFullSize)
{
	if (!area.valid() || area.empty())
		return;
	if (clip.rects.empty())
		return;

	gGLBackend &g = *m_gl;

	/* حدّ نصف القطر كما يفعل CornerData */
	int minDim = (area.width() < area.height()) ? area.width() : area.height();
	if (radius > minDim / 2)
		radius = minDim / 2;
	if (radius < 0)
		radius = 0;

	float rTL = (edges & gPixmap::RADIUS_TOP_LEFT) ? (float)radius : 0.0f;
	float rTR = (edges & gPixmap::RADIUS_TOP_RIGHT) ? (float)radius : 0.0f;
	float rBL = (edges & gPixmap::RADIUS_BOTTOM_LEFT) ? (float)radius : 0.0f;
	float rBR = (edges & gPixmap::RADIUS_BOTTOM_RIGHT) ? (float)radius : 0.0f;
	bool rounded = (rTL > 0.0f || rTR > 0.0f || rBL > 0.0f || rBR > 0.0f);

	float c0[4], c1[4], c2[4], b0[4], b1[4];
	colorToGL(background, c0);
	c1[0] = c0[0]; c1[1] = c0[1]; c1[2] = c0[2]; c1[3] = c0[3];
	c2[0] = c0[0]; c2[1] = c0[1]; c2[2] = c0[2]; c2[3] = c0[3];

	float gradMode = 0.0f;
	if (direction && gradientColors.size() >= 3)
	{
		gradMode = (float)direction;
		colorToGL(gradientColors[0], c0);
		colorToGL(gradientColors[1], c1);
		colorToGL(gradientColors[2], c2);
	}
	else if (direction && gradientColors.size() == 2)
	{
		gradMode = (float)direction;
		colorToGL(gradientColors[0], c0);
		colorToGL(gradientColors[1], c1);
		c2[0] = c1[0]; c2[1] = c1[1]; c2[2] = c1[2]; c2[3] = c1[3];
	}

	colorToGL(borderColor, b0);
	b1[0] = b0[0]; b1[1] = b0[1]; b1[2] = b0[2]; b1[3] = b0[3];

	float gradSize;
	if (gradientFullSize > 0)
		gradSize = (float)gradientFullSize;
	else
		gradSize = (gradMode < 1.5f) ? (float)area.height() : (float)area.width();

	useProgram(g, g.rectProg);

	/*
	 * الدلالة الأصلية للـ fill في enigma2 هي كتابة خام (بلا مزج).
	 * نحافظ عليها إلا إذا كانت هناك حواف دائرية (نحتاج تنعيماً)
	 * أو طُلب المزج صراحة.
	 */
	setBlend(g, (rounded || alphablend) ? BLEND_STRAIGHT : BLEND_OFF);

	glUniformMatrix4fv(g.r_uProj, 1, GL_FALSE, g.proj);
	glUniform2f(g.r_uRectPos, (float)area.left(), (float)area.top());
	glUniform2f(g.r_uRectSize, (float)area.width(), (float)area.height());
	glUniform4fv(g.r_uCol0, 1, c0);
	glUniform4fv(g.r_uCol1, 1, c1);
	glUniform4fv(g.r_uCol2, 1, c2);
	glUniform1f(g.r_uGradMode, gradMode);
	glUniform1f(g.r_uGradSize, gradSize);
	glUniform1f(g.r_uGradOffset, 0.0f);
	glUniform4f(g.r_uRadius, rTL, rTR, rBL, rBR);
	glUniform1f(g.r_uBorderW, (float)(borderWidth > 0 ? borderWidth : 0));
	glUniform4fv(g.r_uBorderCol0, 1, b0);
	glUniform4fv(g.r_uBorderCol1, 1, b1);
	glUniform1f(g.r_uBorderGradMode, 0.0f);
	glUniform1f(g.r_uAA, rounded ? 1.0f : 0.0f);

	for (unsigned int i = 0; i < clip.rects.size(); ++i)
	{
		eRect r = clip.rects[i] & area;
		if (r.empty() || !r.valid())
			continue;

		setScissor(g, r, m_yres);
		drawQuad(g, g.r_aPos, g.r_aTex,
				 (float)area.left(), (float)area.top(),
				 (float)area.width(), (float)area.height());

		/* المحتوى القديم من الطبقة البرمجية تحت هذه المنطقة لم يعد صالحاً */
		softEraseRect(r);
	}
	clearScissor(g);
}

void gGLDC::glClearRegion(const gRegion &region, const gRGB &color)
{
	if (region.rects.empty())
		return;

	gGLBackend &g = *m_gl;
	float c[4];
	colorToGL(color, c);

	useProgram(g, g.rectProg);
	setBlend(g, BLEND_OFF);

	glUniformMatrix4fv(g.r_uProj, 1, GL_FALSE, g.proj);
	glUniform4fv(g.r_uCol0, 1, c);
	glUniform4fv(g.r_uCol1, 1, c);
	glUniform4fv(g.r_uCol2, 1, c);
	glUniform1f(g.r_uGradMode, 0.0f);
	glUniform1f(g.r_uGradSize, 1.0f);
	glUniform1f(g.r_uGradOffset, 0.0f);
	glUniform4f(g.r_uRadius, 0.0f, 0.0f, 0.0f, 0.0f);
	glUniform1f(g.r_uBorderW, 0.0f);
	glUniform4fv(g.r_uBorderCol0, 1, c);
	glUniform4fv(g.r_uBorderCol1, 1, c);
	glUniform1f(g.r_uBorderGradMode, 0.0f);
	glUniform1f(g.r_uAA, 0.0f);

	clearScissor(g);

	for (unsigned int i = 0; i < region.rects.size(); ++i)
	{
		const eRect &r = region.rects[i];
		if (r.empty() || !r.valid())
			continue;

		glUniform2f(g.r_uRectPos, (float)r.left(), (float)r.top());
		glUniform2f(g.r_uRectSize, (float)r.width(), (float)r.height());
		drawQuad(g, g.r_aPos, g.r_aTex,
				 (float)r.left(), (float)r.top(), (float)r.width(), (float)r.height());

		softEraseRect(r);
	}
}

/* ---------------------------------------------------------------------------
 * exec
 * -------------------------------------------------------------------------*/

void gGLDC::exec(const gOpcode *o)
{
	/* أوامر لا تحتاج GL أبداً */
	switch (o->opcode)
	{
	case gOpcode::setBackgroundColor:
	case gOpcode::setForegroundColor:
	case gOpcode::setBackgroundColorRGB:
	case gOpcode::setForegroundColorRGB:
	case gOpcode::setFont:
	case gOpcode::setGradient:
	case gOpcode::setRadius:
	case gOpcode::setBorder:
	case gOpcode::setOffset:
	case gOpcode::setClip:
	case gOpcode::addClip:
	case gOpcode::popClip:
	case gOpcode::setPalette:
	case gOpcode::mergePalette:
		gDC::exec(o);
		return;
	default:
		break;
	}

	if (!ensureGL())
	{
		/* لا GL: لا نُسقط الأوامر، ننفذها بالمسار البرمجي حتى لا يتجمد الجهاز */
		switch (o->opcode)
		{
		case gOpcode::flush:
		case gOpcode::waitVSync:
			softBlitToFb();
			return;
		case gOpcode::flip:
		case gOpcode::sendShow:
		case gOpcode::sendHide:
			return;
		default:
			gDC::exec(o);
			softMarkDirty(m_current_clip.extends);
			return;
		}
	}

	switch (o->opcode)
	{
	case gOpcode::clear:
	{
		glClearRegion(m_current_clip, m_background_color_rgb);
		delete o->parm.fill;
		break;
	}
	case gOpcode::fill:
	{
		eRect area = o->parm.fill->area;
		area.moveBy(m_current_offset);
		gRegion clip = m_current_clip & area;
		glClearRegion(clip, m_foreground_color_rgb);
		delete o->parm.fill;
		break;
	}
	case gOpcode::fillRegion:
	{
		o->parm.fillRegion->region.moveBy(m_current_offset);
		gRegion clip = m_current_clip & o->parm.fillRegion->region;
		glClearRegion(clip, m_foreground_color_rgb);
		delete o->parm.fillRegion;
		break;
	}
	case gOpcode::rectangle:
	{
		o->parm.rectangle->area.moveBy(m_current_offset);
		gRegion clip = m_current_clip & o->parm.rectangle->area;
		glDrawRect(clip, o->parm.rectangle->area,
				   m_background_color_rgb, m_border_color, m_border_width,
				   m_gradient_colors, m_gradient_orientation,
				   m_radius, m_radius_edges, m_gradient_alphablend, m_gradient_fullSize);

		m_border_width = 0;
		m_radius = 0;
		m_radius_edges = 0;
		m_gradient_orientation = 0;
		m_gradient_fullSize = 0;
		m_gradient_alphablend = false;
		delete o->parm.rectangle;
		break;
	}

	/* ---- ما زال على المعالج: نحسب المنطقة المتسخة ثم نفوّض ---- */
	case gOpcode::blit:
	{
		eRect dirty = o->parm.blit->position;
		dirty.moveBy(m_current_offset);
		if (dirty.width() <= 0 || dirty.height() <= 0)
			dirty = eRect(dirty.topLeft(), o->parm.blit->pixmap->size());
		if (o->parm.blit->clip.valid())
		{
			eRect c = o->parm.blit->clip;
			c.moveBy(m_current_offset);
			dirty &= c;
		}
		dirty &= m_current_clip.extends;
		gDC::exec(o);
		softMarkDirty(dirty);
		break;
	}
	case gOpcode::renderText:
	{
		eRect dirty = o->parm.renderText->area;
		dirty.moveBy(m_current_offset);
		/* هامش سخي: eTextPara قد يخرج قليلاً عن المستطيل (ascender/border) */
		dirty = eRect(dirty.left() - 8, dirty.top() - 8, dirty.width() + 16, dirty.height() + 32);
		dirty &= m_current_clip.extends;
		gDC::exec(o);
		softMarkDirty(dirty);
		break;
	}
	case gOpcode::renderPara:
	{
		eRect dirty = m_current_clip.extends;
		gDC::exec(o);
		softMarkDirty(dirty);
		break;
	}
	case gOpcode::line:
	{
		eRect dirty = m_current_clip.extends;
		gDC::exec(o);
		softMarkDirty(dirty);
		break;
	}

	/* ---- العرض ---- */
	case gOpcode::flush:
	case gOpcode::waitVSync:
	{
		composeSoftLayer();
		present();
		break;
	}
	case gOpcode::flip:
		break;

	case gOpcode::sendShow:
	case gOpcode::sendHide:
		break;

	case gOpcode::enableSpinner:
		enableSpinner();
		softMarkDirty(m_spinner_pos);
		break;
	case gOpcode::disableSpinner:
		disableSpinner();
		softMarkDirty(m_spinner_pos);
		break;
	case gOpcode::incrementSpinner:
		incrementSpinner();
		softMarkDirty(m_spinner_pos);
		break;

	default:
		gDC::exec(o);
		break;
	}
}

/* ---------------------------------------------------------------------------
 * إعدادات (متوافقة مع واجهة gFBDC)
 * -------------------------------------------------------------------------*/

void gGLDC::setAlpha(int a) { alpha = a; }
void gGLDC::setBrightness(int b) { brightness = b; }
void gGLDC::setGamma(int g) { gamma = g; }
void gGLDC::saveSettings() {}

void gGLDC::reloadSettings()
{
	alpha = 255;
	gamma = 128;
	brightness = 128;
}

eAutoInitPtr<gGLDC> init_gGLDC(eAutoInitNumbers::graphic - 1, "GGLDC");
