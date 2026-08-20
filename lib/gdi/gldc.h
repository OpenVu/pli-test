#ifndef __lib_gdi_gldc_h
#define __lib_gdi_gldc_h

/*
 * gGLDC - OpenGL ES 2.0 device context for enigma2
 * ------------------------------------------------
 *
 *  البديل المسرّع لـ gFBDC. بدل الرسم بالمعالج داخل /dev/fb0 مباشرة،
 *  يترجم هذا الكلاس أوامر gOpcode القادمة من gPainter/gRC إلى نداءات GLES.
 *
 *  المرحلة 1 (المنفّذة هنا):
 *      clear / fill / fillRegion / rectangle  ->  GLES (شيدر مستطيل واحد
 *      يدعم اللون، التدرج بلونين أو ثلاثة، الحواف الدائرية لكل زاوية،
 *      والإطار) + القص عبر glScissor مشتقاً من gRegion.
 *
 *  باقي الأوامر (blit / renderText / renderPara / line / palette) ما زالت
 *  تُنفَّذ بالمسار القديم (CPU) لكن داخل "طبقة برمجية" في الذاكرة (RAM)،
 *  تُرفع كتكسشر وتُركَّب فوق مشهد GL عند كل flush. هكذا يبقى الجهاز صالحاً
 *  للاستعمال أثناء نقل باقي الأوامر إلى GLES في المرحلتين 2 و 3.
 */

#include "fb.h"
#include "gpixmap.h"
#include "gmaindc.h"
#include "grc.h"

/* معرّف مبهم: كل ما يخص EGL/GLES محبوس داخل gldc.cpp حتى لا تتسرب
   ترويسات GL إلى بقية السورس. */
struct gGLBackend;

class gGLDC : public gMainDC
{
	fbClass *fb;
	gGLBackend *m_gl;

	/* الطبقة البرمجية: سطح 32bpp في الذاكرة، هو m_pixmap الذي يراه gDC::exec */
	gUnmanagedSurface surface;
	unsigned char *m_soft_data;
	int m_soft_size;

	/* أصغر شريط أسطر يغطي ما تغيّر في الطبقة البرمجية منذ آخر رفع */
	int m_dirty_top, m_dirty_bottom;
	bool m_dirty_valid; /* هل الشريط أعلاه ذو معنى؟ */
	bool m_soft_active; /* هل رُسم أي شيء في الطبقة البرمجية أصلاً؟ */

	int m_xres, m_yres, m_bpp;
	bool m_gl_ready;
	bool m_gl_failed;

	int brightness, gamma, alpha;

	void exec(const gOpcode *opcode);

	/* --- GL --- */
	bool ensureGL();
	void shutdownGL();
	void present();
	void composeSoftLayer();
	void softBlitToFb(); /* مخرج احتياطي إذا فشلت تهيئة GL تماماً */

	void glClearRegion(const gRegion &region, const gRGB &color);
	void glDrawRect(const gRegion &clip, const eRect &area,
			const gRGB &background, const gRGB &borderColor, int borderWidth,
			const std::vector<gRGB> &gradientColors, uint8_t direction,
			int radius, uint8_t edges, bool alphablend, int gradientFullSize);

	/* --- الطبقة البرمجية --- */
	void softMarkDirty(const eRect &area);
	void softMarkDirtyRegion(const gRegion &region);
	void softErase(const gRegion &region);
	void softEraseRect(const eRect &area);

public:
	void setResolution(int xres, int yres, int bpp = 32);
	void reloadSettings();

	void setAlpha(int alpha);
	void setBrightness(int brightness);
	void setGamma(int gamma);

	int getAlpha() const { return alpha; }
	int getBrightness() const { return brightness; }
	int getGamma() const { return gamma; }

	int haveDoubleBuffering() const { return 1; } /* EGL يدير التبديل */

	void saveSettings();

	gGLDC();
	virtual ~gGLDC();

	int islocked() const { return fb ? fb->islocked() : 0; }
};

#endif
