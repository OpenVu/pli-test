#ifndef __lib_listbox_h
#define __lib_listbox_h

#include <lib/gui/ewidget.h>
#include <connection.h>

class eListbox;
class eSlider;

class iListboxContent: public iObject
{
public:
	virtual ~iListboxContent()=0;

		/* indices go from 0 to size().
		   the end is reached when the cursor is on size(),
		   i.e. one after the last entry (this mimics
		   stl behavior)

		   cursors never invalidate - they can become invalid
		   when stuff is removed. Cursors will always try
		   to stay on the same data, however when the current
		   item is removed, this won't work. you'll be notified
		   anyway. */
#ifndef SWIG
protected:
	iListboxContent();
	friend class eListbox;
	virtual void updateClip(gRegion &){ };
	virtual void resetClip(){ };
	virtual void cursorHome()=0;
	virtual void cursorEnd()=0;
	virtual int cursorMove(int count=1)=0;
	virtual int cursorValid()=0;
	virtual int cursorSet(int n)=0;
	virtual int cursorGet()=0;

	virtual void cursorSave()=0;
	virtual void cursorRestore()=0;
	virtual void cursorSaveLine(int n)=0;
	virtual int cursorRestoreLine()=0;

	virtual int size()=0;

	virtual int currentCursorSelectable();

	void setListbox(eListbox *lb);

	// void setOutputDevice ? (for allocating colors, ...) .. requires some work, though
	virtual void setSize(const eSize &size)=0;
	virtual void setSelectionSize(const eSize &size) = 0;

		/* the following functions always refer to the selected item */
	virtual void paint(gPainter &painter, eWindowStyle &style, const ePoint &offset, int selected)=0;

	virtual int getItemHeight()=0;
	virtual int getItemWidth() = 0;
	virtual int getSelectionHeight() = 0;
	virtual int getSelectionWidth() = 0;

	eListbox *m_listbox;
#endif
};

#ifndef SWIG
struct eListboxStyle
{
	ePtr<gPixmap> m_background, m_selection, m_shadow, m_overlay;
	int m_transparent_background;
	gRGB m_background_color, m_background_color_gradient_start, m_background_color_gradient_stop, m_background_color_rows, m_background_color_global, m_background_color_selected,
	m_background_color_gradient_selected_start, m_background_color_gradient_selected_stop, m_foreground_color, m_foreground_color_selected, m_border_color, m_sliderborder_color, m_sliderforeground_color;
	gRGB m_background_col_current = m_background_color;
	int m_background_color_set, m_background_color_gradient_direction, m_background_color_gradient_set, m_background_color_global_set, m_background_color_rows_set, m_foreground_color_set, m_background_color_selected_set, m_background_color_gradient_selected_direction, m_background_color_gradient_selected_set, m_foreground_color_selected_set, m_sliderforeground_color_set, m_sliderborder_color_set, m_scrollbarsliderborder_size_set;
	int m_shadow_set, m_overlay_set;	
		/*
			{m_transparent_background m_background_color_set m_background}
			{0 0 0} use global background color
			{0 1 x} use background color
			{0 0 p} use background picture
			{1 x 0} use transparent background
			{1 x p} use transparent background picture
		*/

	enum
	{
		alignLeft,
		alignTop=alignLeft,
		alignCenter,
		alignRight,
		alignBottom=alignRight,
		alignBlock
	};
	int m_valign, m_halign, m_border_size, m_sliderborder_size, m_scrollbarsliderborder_size;
	ePtr<gFont> m_font, m_secondfont;
	ePoint m_text_offset;
};
#endif

class eListbox: public eWidget
{
	void updateScrollBar();
public:
	eListbox(eWidget *parent);
	~eListbox();

	PSignal0<void> selectionChanged;

	enum {
		byPage,
		byLine
	};

	enum
	{
		LayoutVertical,
		LayoutGrid,
		LayoutHorizontal
	};

	enum {
		showOnDemand,
		showAlways,
		showNever,
		showLeft
	};
	void setScrollbarMode(int mode);
	void setWrapAround(bool);

	void setContent(iListboxContent *content);

	void allowNativeKeys(bool allow);

/*	enum Movement {
		moveUp,
		moveDown,
		moveTop,
		moveEnd,
		justCheck
	}; */

	int getCurrentIndex();
	int getCurrentPage();
	int getTotalPages();
	ePoint getSelectionAbsolutePosition();
	void moveSelection(long how);
	void moveSelectionTo(int index);
	void moveToEnd();
	bool atBegin();
	bool atEnd();

	enum ListboxActions {
		moveUp,
		moveDown,
		moveTop,
		moveEnd,
		pageUp,
		pageDown,
		prevPage,
		nextPage,
		justCheck,
		refresh
	};

	void setLayoutMode(int mode);
	void setScrollMode(int scroll);
	void setItemWidth(int w);
	void setSelectionHeight(int h);
	void setSelectionWidth(int w);
	void setRows(int r);
	void setColumns(int c);
	void setMargin(const ePoint &margin);

	void setItemHeight(int h);
	void setSelectionEnable(int en);

	void setShadow(int shadow);
	void setOverlay(int ovelay);

	void setBackgroundColor(gRGB &col);
	void setBackgroundColorGradient(gRGB &start, gRGB &end, int direction);
	void setBackgroundColorGlobal(gRGB &col);
	void setBackgroundColorRows(gRGB &col);
	void setBackgroundColorSelected(gRGB &col);
	void setBackgroundColorGradientSelected(gRGB &start, gRGB &end, int direction);
	void setForegroundColor(gRGB &col);
	void setForegroundColorSelected(gRGB &col);
	void setBorderColor(const gRGB &col);
	void setBorderWidth(int size);
	void setBackgroundPicture(ePtr<gPixmap> &pixmap);
	void setSelectionPicture(ePtr<gPixmap> &pixmap);

	void setSliderPicture(ePtr<gPixmap> &pm);
	void setScrollbarBackgroundPicture(ePtr<gPixmap> &pm);
	void setScrollbarSliderBorderWidth(int size);
	void setScrollbarWidth(int size);
	void setScrollbarOffset(int size);

	void setFont(gFont *font);
	void setSecondFont(gFont *font);
	void setVAlign(int align);
	void setHAlign(int align);
	void setTextOffset(const ePoint &textoffset);
	void setCenterList(bool center);

	void setSliderBorderColor(const gRGB &col);
	void setSliderBorderWidth(int size);
	void setSliderForegroundColor(gRGB &col);

	int getScrollbarWidth() { return m_scrollbar_width; }

#ifndef SWIG
	struct eListboxStyle *getLocalStyle(void);

		/* entryAdded: an entry was added *before* the given index. it's index is the given number. */
	void entryAdded(int index);
		/* entryRemoved: an entry with the given index was removed. */
	void entryRemoved(int index);
		/* entryChanged: the entry with the given index was changed and should be redrawn. */
	void entryChanged(int index);
		/* the complete list changed. you should not attemp to keep the current index. */
	void entryReset(bool cursorHome=true);

	int getEntryTop();
	void invalidate(const gRegion &region = gRegion::invalidRegion());
protected:
	int event(int event, void *data=0, void *data2=0);
	void recalcSize();

private:
	int m_scrollbar_mode, m_scroll_mode, m_prev_scrollbar_page;
	bool m_content_changed;  
	bool m_enabled_wrap_around;
	bool m_itemheight_set;
	bool m_itemwidth_set;
	bool m_selectionheight_set;
	bool m_selectionwidth_set;
	bool m_columns_set;
	bool m_rows_set;
	
	int m_scrollbar_width;
	int m_scrollbar_border_width;
	int m_scrollbar_offset;
	int m_top, m_selected;
	int m_layout_mode;
	int m_itemheight;
	int m_itemwidth;
	int m_selectionheight;
	int m_selectionwidth;
	int m_columns;
	int m_rows;
	int m_items_per_page;
	int m_selection_enabled;
	int xoffset;
	int yoffset;
	bool m_native_keys_bound;
	int m_first_selectable_item;
	int m_last_selectable_item;
	bool m_center_list;
	

	ePoint m_margin;
	ePtr<iListboxContent> m_content;
	eSlider *m_scrollbar;
	eListboxStyle m_style;
	ePtr<gPixmap> m_scrollbarpixmap, m_scrollbarbackgroundpixmap;
#endif
};

#endif
