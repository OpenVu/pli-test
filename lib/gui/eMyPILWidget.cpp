#include "eMyPILWidget.h"
#include <lib/gdi/gpainter.h>
#include <lib/gdi/epixmap.h> // For ePixmap

// Define this to use the FreeType library for text rendering in CImg.
// Enigma2 already has and uses FreeType, so this is the perfect choice.
#define cimg_use_freetype

// Define this to disable CImg's attempts to display things in its own window,
// which is not applicable in the Enigma2 environment.
#define cimg_display 0

#include <CImg.h>

eMyPILWidget::eMyPILWidget() : m_pixmap(NULL), m_redraw_needed(true)
{
    // Set the widget to be transparent so our alpha channel works correctly.
    setAlphatest(2); // 2 usually means blend
}

eMyPILWidget::~eMyPILWidget()
{
    // Clean up the pixmap to prevent memory leaks.
    delete m_pixmap;
    m_pixmap = NULL;
}

// This is the core rendering function, called by the GUI system when a repaint is needed.
void eMyPILWidget::redrawWidget(gPainter *painter, const eRect &area)
{
    if (m_redraw_needed && !m_canvas.is_empty())
    {
        // Free the old pixmap if it exists.
        delete m_pixmap;

        // CImg stores data as a planar buffer (RRR...GGG...BBB...), but gPixmap needs
        // an interleaved format (RGBA, RGBA, ...). We need to convert it.
        // First, create a temporary interleaved canvas.
        cimg_library::CImg<unsigned char> interleaved = m_canvas.get_permute_axes("cxyz");

        // Now, create a gPixmap from the raw, interleaved data.
        // The gPixmap constructor can take a pointer to the data.
        m_pixmap = new gPixmap((gRGB*)interleaved.data(), m_canvas.width(), m_canvas.height(), 8, 8, 8, 8); // 8 bits per channel

        m_redraw_needed = false;
    }

    // If we have a valid pixmap, draw it onto the screen.
    if (m_pixmap)
    {
        painter->blit(*m_pixmap, ePoint(0, 0), area);
    }
}

void eMyPILWidget::resizeCanvas(int width, int height)
{
    // Create a new canvas of the given size with 4 channels (RGBA) and initialize to 0.
    m_canvas.assign(width, height, 1, 4, 0);
}

void eMyPILWidget::clearCanvas(int r, int g, int b, int a)
{
    if (m_canvas.is_empty()) return;

    // CImg's draw_rectangle can fill the entire canvas.
    const unsigned char color[] = { (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
    m_canvas.draw_rectangle(0, 0, m_canvas.width() - 1, m_canvas.height() - 1, color, 1.0f);
}

void eMyPILWidget::drawRectangle(int x0, int y0, int x1, int y1, int r, int g, int b, int a)
{
    if (m_canvas.is_empty()) return;

    const unsigned char color[] = { (unsigned char)r, (unsigned char)g, (unsigned char)b };
    float opacity = a / 255.0f;
    m_canvas.draw_rectangle(x0, y0, x1, y1, color, opacity);
}

void eMyPILWidget::drawText(int x, int y, const std::string &text, const std::string &font_path, int font_size, int r, int g, int b, int a)
{
    if (m_canvas.is_empty() || text.empty()) return;

    const unsigned char color[] = { (unsigned char)r, (unsigned char)g, (unsigned char)b };
    float opacity = a / 255.0f;

    // CImg's draw_text can use FreeType fonts directly.
    // The last arguments are for alignment (0=left, 0=top).
    try {
        m_canvas.draw_text(x, y, text.c_str(), color, 0, opacity, font_size, font_path.c_str());
    } catch (const cimg_library::CImgException &e) {
        // eDebug is Enigma2's logging function.
        eDebug("[eMyPILWidget] CImg Error drawing text: %s", e.what());
    }
}

void eMyPILWidget::drawLine(int x0, int y0, int x1, int y1, int r, int g, int b, int a)
{
    if (m_canvas.is_empty()) return;
    
    const unsigned char color[] = { (unsigned char)r, (unsigned char)g, (unsigned char)b };
    float opacity = a / 255.0f;
    m_canvas.draw_line(x0, y0, x1, y1, color, opacity);
}

void eMyPILWidget::drawImage(int x, int y, const std::string &image_path, float opacity)
{
    if (m_canvas.is_empty()) return;

    try {
        cimg_library::CImg<unsigned char> image_to_draw(image_path.c_str());
        
        // Ensure the loaded image has an alpha channel for proper drawing.
        if (image_to_draw.spectrum() == 3) {
            image_to_draw.resize(-100, -100, -100, 4, 255); // Add alpha channel, fully opaque
        }

        // Draw the loaded image onto our main canvas.
        m_canvas.draw_image(x, y, image_to_draw, opacity);

    } catch (const cimg_library::CImgException &e) {
        eDebug("[eMyPILWidget] CImg Error loading image '%s': %s", image_path.c_str(), e.what());
    }
}

void eMyPILWidget::commit()
{
    m_redraw_needed = true;
    // invalidate() tells the Enigma2 GUI system that this widget's area
    // is "dirty" and needs to be repainted on the next frame.
    invalidate();
}
