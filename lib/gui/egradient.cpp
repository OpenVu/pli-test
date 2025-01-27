#include <lib/gdi/color.h>
#include <lib/gui/egradient.h>

eGradient::eGradient(eWidget *parent): eWidget(parent)
{
	m_gradient_orientation = gPainter::GRADIENT_VERTICAL;
	m_gradient_type = GRADIENT_ONECOLOR;
	m_gradient_mode = DARK_TO_LIGHT;
	m_gradient_intensity = LIGHT;
	m_alphatest = 0;
	m_gradient_color_set = false;
	m_gradient_second_color_set = false;
	setTransparent(1);
}

int eGradient::event(int event, void *data, void *data2)
{
	switch (event)
	{
	case evtPaint:
	{
		ePtr<eWindowStyle> style;

		getStyle(style);

		eWidget::event(event, data, data2);

		gPainter &painter = *(gPainter*)data2;

		int flags = 0;
		if (m_alphatest == 1)
			flags = gPainter::BT_ALPHATEST;
		else if (m_alphatest == 2)
			flags = gPainter::BT_ALPHABLEND;

		if (!m_pixmap)
			drawGradient(m_pixmap);
		if (m_pixmap)
			painter.blit(m_pixmap, eRect(ePoint(0, 0), size()), eRect(), flags);
	
		return 0;
	}
	case evtChangedSize:
		m_pixmap = nullptr;
		invalidate();
		[[fallthrough]];
	default:
		return eWidget::event(event, data, data2);
	}
}

void eGradient::setAlphatest(int alphatest)
{
	m_alphatest = alphatest;
}

void eGradient::setGradientOrientation(int orientation)
{
	if (orientation >= 0 && orientation <= 1 && m_gradient_orientation != orientation)
	{
		m_gradient_orientation = orientation;
		m_pixmap = nullptr;
		invalidate();
	}
}

void eGradient::setGradientType(int type)
{
	if (type >= 0 && type <= 2 && m_gradient_type != type)
	{
		m_gradient_type = type;
		m_pixmap = nullptr;
		invalidate();
	}
}

void eGradient::setGradientMode(int mode)
{
	if (mode >= 0 && mode <= 3 && m_gradient_mode != mode)
	{
		m_gradient_mode = mode;
		m_pixmap = nullptr;
		invalidate();
	}
}

void eGradient::setGradientIntensity(int intensity)
{
	if (intensity >= 0 && intensity <= 2 && m_gradient_intensity != intensity)
	{
		m_gradient_intensity = intensity;
		m_pixmap = nullptr;
		invalidate();
	}
}

void eGradient::setGradientColor(const gRGB &col)
{
	if ((!m_gradient_color_set) || (m_gradient_color != col))
	{
		m_gradient_color = col;
		m_gradient_color_set = true;
		m_pixmap = nullptr;
		invalidate();
	}
}

void eGradient::setGradientSecondColor(const gRGB &col)
{
	if ((!m_gradient_second_color_set) || (m_gradient_second_color != col))
	{
		m_gradient_second_color = col;
		m_gradient_second_color_set = true;
		m_pixmap = nullptr;
		invalidate();
	}
}

int eGradient::drawGradient(ePtr<gPixmap> &result)
{
	if (!m_gradient_color_set || ((m_gradient_type == GRADIENT_COLOR2COLOR) && !m_gradient_color_set && !m_gradient_second_color_set))
		return 0;
	
	result = nullptr;
	result = new gPixmap(size().width(), size().height(), 32, NULL, (m_alphatest == gPainter::BT_ALPHABLEND && m_gradient_orientation == gPainter::GRADIENT_VERTICAL) ? gPixmap::accelAlways : gPixmap::accelNever);
	uint32_t* gradientBuf = NULL;
	uint32_t* boxBuf = (uint32_t*)result->surface->data;

	if (m_gradient_type == GRADIENT_ONECOLOR)
		gradientBuf = gradientOneColor(m_gradient_color, NULL, (m_gradient_orientation == gPainter::GRADIENT_VERTICAL) ? size().height() : size().width() , m_gradient_mode, m_gradient_intensity);
	else if (m_gradient_type == GRADIENT_COLOR2TRANSPARENT)
		gradientBuf = gradientColorToTransparent(m_gradient_color, NULL, (m_gradient_orientation == gPainter::GRADIENT_VERTICAL) ? size().height() : size().width(), m_gradient_mode, m_gradient_intensity);
	else if (m_gradient_type == GRADIENT_COLOR2COLOR)
		gradientBuf = gradientColorToColor(m_gradient_color, m_gradient_second_color, NULL, (m_gradient_orientation == gPainter::GRADIENT_VERTICAL) ? size().height() : size().width(), m_gradient_mode, m_gradient_intensity);


	uint32_t *bp = boxBuf;
	uint32_t *gra = gradientBuf;

	if (m_gradient_orientation == gPainter::GRADIENT_VERTICAL)
	{
		for (int pos = 0; pos < size().width(); pos++) 
		{
			for(int count = 0; count < size().height(); count++) 
			{
				*(bp + pos) = (uint32_t)(*(gra + count));
				bp += size().width();
			}
			bp = boxBuf;
		}
	}
	else
	{
		for (int line = 0; line < size().height(); line++) 
		{
			int gra_pos = 0;
			for (int pos = 0; pos < size().width(); pos++) 
			{
				if ( (pos >= 0) && (gra_pos < size().width()))
				{
					*(bp + pos) = (uint32_t)(*(gra + gra_pos));
					gra_pos++;
				}
			}
			bp += size().width();
		}
	}
	free(gradientBuf);
	return 0;
}
