#ifndef __lib_gui_egradient_h
#define __lib_gui_egradient_h

#include <lib/gui/ewidget.h>

class eGradient: public eWidget
{
public:
	eGradient(eWidget *parent);

    enum {
        GRADIENT_COLOR_TO_TRANSPARENT,
        GRADIENT_ONECOLOR,
        GRADIENT_COLOR_TO_COLOR
    };

    enum {
        DARK_TO_LIGHT,
        LIGHT_TO_DARK,
        DARK_TO_LIGHT_TO_DARK,
        LIGHT_TO_DARK_TO_LIGHT
    };

    enum {
        LIGHT,
        NORMAL,
        EXTENDED
    };

    void setAlphatest(int alphatest);
    void setGradientOrientation(int orientation);
    void setGradientType(int type);
    void setGradientMode(int mode);
    void setGradientIntensity(int intensity);
    void setGradientColor(const gRGB &col);
    void setGradientSecondColor(const gRGB &col);
protected:
	int event(int event, void *data=0, void *data2=0);
private:
    int m_gradient_orientation , m_gradient_type, m_gradient_mode, m_gradient_intensity, m_alphatest;
    bool m_gradient_color_set, m_gradient_second_color_set;
    gRGB m_gradient_color, m_gradient_second_color;
    SWIG_VOID(int) drawGradient(ePtr<gPixmap> &SWIG_OUTPUT);
	ePtr<gPixmap> m_pixmap;
};

#endif
