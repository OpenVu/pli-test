#ifndef __lib_gui_elistboxcontent_h
#define __lib_gui_elistboxcontent_h

#include <lib/python/python.h>
#include <lib/gui/elistbox.h>

class eListboxPythonStringContent: public virtual iListboxContent
{
	DECLARE_REF(eListboxPythonStringContent);
public:
	eListboxPythonStringContent();
	~eListboxPythonStringContent();

	void setList(SWIG_PYOBJECT(ePyObject) list);
	void setItemHeight(int height);
	void setItemWidth(int width);
	void setSelectionHeight(int height);
	void setSelectionWidth(int width);
	PyObject *getCurrentSelection();
	int getCurrentSelectionIndex() { return m_cursor; }
	void invalidateEntry(int index);
	void invalidate();
	eSize getItemSize() { return m_itemsize; }
	eSize getSelectionSize() { return m_selectionsize; }
#ifndef SWIG
protected:
	void cursorHome();
	void cursorEnd();
	int cursorMove(int count=1);
	int cursorValid();
	int cursorSet(int n);
	int cursorGet();
	virtual int currentCursorSelectable();

	void cursorSave();
	void cursorRestore();
	void cursorSaveLine(int n);
	int cursorRestoreLine();
	int size();

	RESULT connectItemChanged(const sigc::slot<void()> &itemChanged, ePtr<eConnection> &connection);

	// void setOutputDevice ? (for allocating colors, ...) .. requires some work, though
	void setSize(const eSize &size);
	void setSelectionSize(const eSize &size);

		/* the following functions always refer to the selected item */
	virtual void paint(gPainter &painter, eWindowStyle &style, const ePoint &offset, int selected);

	int getItemHeight() { return m_itemheight; }
	int getItemWidth() { return m_itemwidth; }
	int getSelectionHeight() { return m_selectionheight; }
	int getSelectionWidth() { return m_selectionwidth; }

protected:
	ePyObject m_list;
	int m_cursor, m_saved_cursor, m_saved_cursor_line;
	eSize m_itemsize;
	eSize m_selectionsize;
	ePtr<gFont> m_font;
	int m_itemheight;
	int m_itemwidth;
	int m_selectionheight;
	int m_selectionwidth;
#endif
};

class eListboxPythonConfigContent: public eListboxPythonStringContent
{
public:
	void paint(gPainter &painter, eWindowStyle &style, const ePoint &offset, int selected);
	void setSeperation(int sep) { m_seperation = sep; }
	int currentCursorSelectable();
	void setSlider(int height, int space) { m_slider_height = height; m_slider_space = space; }
private:
	int m_seperation, m_slider_height, m_slider_space;
};

class eListboxPythonMultiContent: public eListboxPythonStringContent
{
	ePyObject m_buildFunc;
	ePyObject m_selectableFunc;
	ePyObject m_template;
	eRect m_selection_clip;
	gRegion m_clip, m_old_clip;
public:
	eListboxPythonMultiContent();
	~eListboxPythonMultiContent();
	enum { TYPE_TEXT, TYPE_PROGRESS, TYPE_PIXMAP, TYPE_PIXMAP_ALPHATEST, TYPE_PIXMAP_ALPHABLEND, TYPE_PROGRESS_PIXMAP };
	void paint(gPainter &painter, eWindowStyle &style, const ePoint &offset, int selected);
	void refresh();
	int currentCursorSelectable();
	void setList(SWIG_PYOBJECT(ePyObject) list);
	void setFont(int fnt, gFont *font);
	void setBuildFunc(SWIG_PYOBJECT(ePyObject) func);
	void setSelectableFunc(SWIG_PYOBJECT(ePyObject) func);
	void setItemHeight(int height);
	void setItemWidth(int width);
	void setSelectionHeight(int height);
	void setSelectionWidth(int width);
	void setFlexMode(int mode);
	void setMargin(const ePoint &margin);
	void setSelectionClip(eRect &rect, bool update=false);
	void updateClip(gRegion &);
	void resetClip();
	void entryRemoved(int idx);
	void setTemplate(SWIG_PYOBJECT(ePyObject) tmplate);
private:
	std::map<int, ePtr<gFont> > m_font;
};

#ifdef SWIG
#define RT_HALIGN_BIDI 0
#define RT_HALIGN_LEFT 1
#define RT_HALIGN_RIGHT 2
#define RT_HALIGN_CENTER 4
#define RT_HALIGN_BLOCK 8
#define RT_VALIGN_TOP 0
#define RT_VALIGN_CENTER 16
#define RT_VALIGN_BOTTOM 32
#define RT_WRAP 64
#define BT_ALPHATEST 1
#define BT_ALPHABLEND 2
#define BT_SCALE 4
#define BT_KEEP_ASPECT_RATIO 8
#define BT_FIXRATIO 8
#define BT_HALIGN_LEFT 0
#define BT_HALIGN_CENTER 16
#define BT_HALIGN_RIGHT 32
#define BT_VALIGN_TOP 0
#define BT_VALIGN_CENTER 64
#define BT_VALIGN_BOTTOM 128
#define BT_ALIGN_CENTER BT_HALIGN_CENTER | BT_VALIGN_CENTER

#endif // SWIG

#endif
