#ifndef __lib_gui_eMyPILWidget_h
#define __lib_gui_eMyPILWidget_h

#include <lib/gui/ewidget.h>
#include <lib/gdi/gpixmap.h>

// This is the only include needed for the CImg library.
// Make sure the compiler knows where to find it (e.g., via -I flag in Makefile).
#include <CImg.h>

// Forward declaration to avoid including the full painter header here
class gPainter;

/**
 * @brief A generic drawing widget for Enigma2, controlled by Python.
 *
 * This widget provides a canvas (using the CImg library) that can be manipulated
 * through a set of drawing commands exposed to Python. After issuing commands,
 * a call to commit() will render the result to the screen efficiently.
 */
class eMyPILWidget : public eWidget
{
private:
    // The main drawing surface from the CImg library.
    // We use 'unsigned char' for 8-bit color channels (0-255).
    // The canvas will have 4 channels: R, G, B, and Alpha.
    cimg_library::CImg<unsigned char> m_canvas;

    // The pixmap used for the final blitting to the screen.
    // We keep it as a member to avoid re-allocating on every paint event.
    gPixmap *m_pixmap;

    // A flag to prevent unnecessary redraws. It's set to true by commit().
    bool m_redraw_needed;

    // Overridden from eWidget. This is where all painting occurs.
    void- redrawWidget(gPainter *painter, const eRect &area);

public:
    // Constructor: Initializes the widget and the canvas.
    eMyPILWidget();
    // Destructor: Cleans up resources, especially the pixmap.
    ~eMyPILWidget();

    // --- API Methods Exposed to Python via SWIG ---

    /**
     * @brief Resizes the canvas. This will clear all existing drawings.
     * @param width The new width of the canvas.
     * @param height The new height of the canvas.
     */
    void resizeCanvas(int width, int height);

    /**
     * @brief Clears the entire canvas to a specified color.
     * @param r Red component (0-255).
     * @param g Green component (0-255).
     * @param b Blue component (0-255).
     * @param a Alpha component (0-255, where 0 is transparent).
     */
    void clearCanvas(int r, int g, int b, int a);

    /**
     * @brief Draws a rectangle.
     * @param x0 Top-left corner X coordinate.
     * @param y0 Top-left corner Y coordinate.
     * @param x1 Bottom-right corner X coordinate.
     * @param y1 Bottom-right corner Y coordinate.
     * @param r, g, b, a Color components for the rectangle.
     */
    void drawRectangle(int x0, int y0, int x1, int y1, int r, int g, int b, int a);

    /**
     * @brief Draws text onto the canvas.
     * @param x, y The top-left starting point for the text.
     * @param text The string to draw.
     * @param font_path Absolute path to the .ttf font file.
     * @param font_size The size of the font in points.
     * @param r, g, b, a Color components for the text.
     */
    void drawText(int x, int y, const std::string &text, const std::string &font_path, int font_size, int r, int g, int b, int a);

    /**
     * @brief Draws a line.
     * @param x0, y0 Starting point of the line.
     * @param x1, y1 Ending point of the line.
     * @param r, g, b, a Color components for the line.
     */
    void drawLine(int x0, int y0, int x1, int y1, int r, int g, int b, int a);
    
    /**
     * @brief Draws an image from a file onto the canvas.
     * @param x, y Top-left coordinate to place the image.
     * @param image_path Absolute path to the image file (PNG, JPG, etc.).
     * @param opacity The opacity of the drawn image (0.0 to 1.0).
     */
    void drawImage(int x, int y, const std::string &image_path, float opacity);

    /**
     * @brief Commits all drawing operations to the screen.
     *
     * Call this after you have finished a batch of drawing commands. It marks
     * the widget as needing a repaint and triggers the update.
     */
    void commit();
};

#endif // __lib_gui_eMyPILWidget_h
