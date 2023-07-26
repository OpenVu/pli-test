#include <lib/gui/elistbox.h>
#include <lib/gui/elistboxcontent.h>
#include <lib/gui/eslider.h>
#include <lib/actions/action.h>
#include <lib/gdi/epng.h>
#include <lib/base/wrappers.h>

eListbox::eListbox(eWidget *parent) :
	eWidget(parent), m_scrollbar_mode(showNever), m_scroll_mode(byPage), m_prev_scrollbar_page(-1),
	m_content_changed(false), m_enabled_wrap_around(false), m_itemheight_set(false),
	m_itemwidth_set(false), m_selectionheight_set(false), m_selectionwidth_set(false), m_columns_set(false), m_rows_set(false), m_scrollbar_width(20), m_scrollbar_offset(5),
	m_top(0), m_selected(0), m_flex_mode(flexVertical), m_itemheight(20), m_itemwidth(20), m_selectionheight(20), m_selectionwidth(20), m_columns(2), m_rows(2),
	m_items_per_page(0), m_selection_enabled(1), xoffset(0), yoffset(0), m_native_keys_bound(false), m_first_selectable_item(-1), m_last_selectable_item(-1), m_center_list(true), m_scrollbar(nullptr)
{
	memset(static_cast<void*>(&m_style), 0, sizeof(m_style));
	m_style.m_text_offset = ePoint(1,1);
//	setContent(new eListboxStringContent());

	allowNativeKeys(true);
}

eListbox::~eListbox()
{
	if (m_scrollbar)
		delete m_scrollbar;

	allowNativeKeys(false);
}

void eListbox::setScrollbarMode(int mode)
{
	m_scrollbar_mode = mode;
	if (m_scrollbar)
	{
		if (m_scrollbar_mode == showNever)
		{
			delete m_scrollbar;
			m_scrollbar=0;
		}
	}
	else
	{
		m_scrollbar = new eSlider(this);
		m_scrollbar->hide();
		m_scrollbar->setBorderWidth(1);
		m_scrollbar->setOrientation(eSlider::orVertical);
		m_scrollbar->setRange(0,100);
		if (m_scrollbarbackgroundpixmap) m_scrollbar->setBackgroundPixmap(m_scrollbarbackgroundpixmap);
		if (m_scrollbarpixmap) m_scrollbar->setPixmap(m_scrollbarpixmap);
		if (m_style.m_sliderborder_color_set) m_scrollbar->setSliderBorderColor(m_style.m_sliderborder_color);
	}
}

void eListbox::setFlexMode(int mode)
{
	if (mode <= 2)
	{
		int old_mode = m_flex_mode;
		m_flex_mode = mode;
		if (old_mode != mode)
		{
			recalcSize();
			entryReset(false);
		}
	}
}

void eListbox::setScrollMode(int scroll)
{
	if (m_scrollbar && m_scroll_mode != scroll)
	{
		m_scroll_mode = scroll;
		updateScrollBar();
		return;
	}
	m_scroll_mode = scroll;
}

void eListbox::setWrapAround(bool state)
{
	m_enabled_wrap_around = state;
}

void eListbox::setContent(iListboxContent *content)
{
	m_content = content;
	if (content)
		m_content->setListbox(this);
	entryReset();
}

void eListbox::allowNativeKeys(bool allow)
{
	if (m_native_keys_bound != allow)
	{
		ePtr<eActionMap> ptr;
		eActionMap::getInstance(ptr);
		if (allow)
			ptr->bindAction("ListboxActions", 0, 0, this);
		else
			ptr->unbindAction(this, 0);
		m_native_keys_bound = allow;
	}
}

bool eListbox::atBegin()
{
	if (m_content && !m_selected)
		return true;
	return false;
}

bool eListbox::atEnd()
{
	if (m_content && m_content->size() == m_selected+1)
		return true;
	return false;
}

void eListbox::moveToEnd()
{
	if (!m_content)
		return;
	/* move to last existing one ("end" is already invalid) */
	m_content->cursorEnd(); m_content->cursorMove(-1);
	/* current selection invisible? */
	if (m_top + m_items_per_page <= m_content->cursorGet())
	{
		int rest = m_content->size() % m_items_per_page;
		if (rest)
			m_top = m_content->cursorGet() - rest + 1;
		else
			m_top = m_content->cursorGet() - m_items_per_page + 1;
		if (m_top < 0)
			m_top = 0;
	}
}

void eListbox::moveSelection(long dir)
{
	/* refuse to do anything without a valid list. */
	if (!m_content)
		return;
	/* if our list does not have one entry, don't do anything. */
	if (!m_items_per_page || !m_content->size())
		return;
	/* we need the old top/sel to see what we have to redraw */
	int oldtop = m_top;
	int oldsel = m_selected;
	int prevsel = oldsel;
	int newsel;
	bool indexchanged = dir > 100;
	if(indexchanged) {
		dir -= 100;
	}

	switch (dir)
	{
	case moveEnd:
		m_content->cursorEnd();
		[[fallthrough]];
	case moveUp:
	{
		if (m_flex_mode == flexGrid)
		{
			do
			{
				m_content->cursorMove(-m_columns);
				newsel = m_content->cursorGet();
				if (newsel == prevsel)
				{ // cursorMove reached top and left cursor position the same. Must wrap around ?
					if (m_enabled_wrap_around)
					{
						m_content->cursorEnd();
						m_content->cursorMove(-1);
						newsel = m_content->cursorGet();
					}
					else
					{
						m_content->cursorSet(oldsel);
						break;
					}
				}
				prevsel = newsel;
			} while (newsel != oldsel && !m_content->currentCursorSelectable());
			break;
		}
		else if (m_flex_mode == flexHorizontal)
		{
			int pageind;
			do
			{
				m_content->cursorMove(-m_items_per_page);
				newsel = m_content->cursorGet();
				pageind = newsel % m_items_per_page; // rememer were we land in thsi page (could be different on topmost page)
				prevsel = newsel - pageind;			 // get top of page index
				// find first selectable entry in new page. First check bottom part, than upper part
				while (newsel != prevsel + m_items_per_page && m_content->cursorValid() && !m_content->currentCursorSelectable())
				{
					m_content->cursorMove(1);
					newsel = m_content->cursorGet();
				}
				if (!m_content->currentCursorSelectable()) // no selectable found in bottom part of page
				{
					m_content->cursorSet(prevsel + pageind);
					while (newsel != prevsel && !m_content->currentCursorSelectable())
					{
						m_content->cursorMove(-1);
						newsel = m_content->cursorGet();
					}
				}
				if (m_content->currentCursorSelectable())
					break;
				if (newsel == 0) // at top and nothing found . Go down till something selectable or old location
				{
					while (newsel != oldsel && !m_content->currentCursorSelectable())
					{
						m_content->cursorMove(1);
						newsel = m_content->cursorGet();
					}
					break;
				}
				m_content->cursorSet(prevsel + pageind);
			} while (newsel == prevsel);
			break;
		}
		else
		{
			do
			{
				m_content->cursorMove(-1);
				newsel = m_content->cursorGet();
				if (newsel == prevsel)
				{ // cursorMove reached top and left cursor position the same. Must wrap around ?
					if (m_enabled_wrap_around)
					{
						m_content->cursorEnd();
						m_content->cursorMove(-1);
						newsel = m_content->cursorGet();
					}
					else
					{
						m_content->cursorSet(oldsel);
						break;
					}
				}
				prevsel = newsel;
			} while (newsel != oldsel && !m_content->currentCursorSelectable());
			break;
		}
	}
	case refresh:
		oldsel = ~m_selected;
		break;
	case moveTop:
		m_content->cursorHome();
		[[fallthrough]];
	case justCheck:
		if (m_content->cursorValid() && m_content->currentCursorSelectable())
			break;
		[[fallthrough]];
	case moveDown:
	{
		if (m_flex_mode == flexGrid)
		{
			do
			{
				m_content->cursorMove(m_columns);
				if (!m_content->cursorValid())
				{ // cursorMove reached end and left cursor position past the list. Must wrap around ?
					if (m_enabled_wrap_around)
					{
						if (oldsel + 1 < m_content->size())
							m_content->cursorMove(-1);
						else
							m_content->cursorHome();
					}
					else
					{
						if (oldsel + 1 < m_content->size())
							m_content->cursorMove(-1);
						else
							m_content->cursorSet(oldsel);
					}
				}
				newsel = m_content->cursorGet();
			} while (newsel != oldsel && !m_content->currentCursorSelectable());
			break;
		}
		else if (m_flex_mode == flexHorizontal)
		{
			int pageind;
			do
			{
				m_content->cursorMove(m_items_per_page);
				if (!m_content->cursorValid())
					m_content->cursorMove(-1);
				newsel = m_content->cursorGet();
				pageind = newsel % m_items_per_page;
				prevsel = newsel - pageind; // get top of page index
				// find a selectable entry in the new page. first look up then down from current screenlocation on the page
				while (newsel != prevsel && !m_content->currentCursorSelectable())
				{
					m_content->cursorMove(-1);
					newsel = m_content->cursorGet();
				}
				if (!m_content->currentCursorSelectable()) // no selectable found in top part of page
				{
					m_content->cursorSet(prevsel + pageind);
					do
					{
						m_content->cursorMove(1);
						newsel = m_content->cursorGet();
					} while (newsel != prevsel + m_items_per_page && m_content->cursorValid() && !m_content->currentCursorSelectable());
				}
				if (!m_content->cursorValid())
				{
					// we reached the end of the list
					// Back up till something selectable or we reach oldsel again
					// E.g this should bring us back to the last selectable item on the original page
					do
					{
						m_content->cursorMove(-1);
						newsel = m_content->cursorGet();
					} while (newsel != oldsel && !m_content->currentCursorSelectable());
					break;
				}
				if (newsel != prevsel + m_items_per_page)
					break;
				m_content->cursorSet(prevsel + pageind); // prepare for next page down
			} while (newsel == prevsel + m_items_per_page);
			break;
		}
		else
		{
			do
			{
				m_content->cursorMove(1);
				if (!m_content->cursorValid())
				{ // cursorMove reached end and left cursor position past the list. Must wrap around ?
					if (m_enabled_wrap_around)
						m_content->cursorHome();
					else
						m_content->cursorSet(oldsel);
				}
				newsel = m_content->cursorGet();
			} while (newsel != oldsel && !m_content->currentCursorSelectable());
			break;
		}
	}
	case pageUp:
	{
		if (m_flex_mode == flexGrid || m_flex_mode == flexHorizontal)
		{
			do
			{
				m_content->cursorMove(-1);
				newsel = m_content->cursorGet();
				if (newsel == prevsel)
				{ // cursorMove reached top and left cursor position the same. Must wrap around ?
					if (m_enabled_wrap_around)
					{
						m_content->cursorEnd();
						m_content->cursorMove(-1);
						newsel = m_content->cursorGet();
					}
					else
					{
						m_content->cursorSet(oldsel);
						break;
					}
				}
				prevsel = newsel;
			} while (newsel != oldsel && !m_content->currentCursorSelectable());
			break;
		}
		else
		{
			int pageind;
			do
			{
				m_content->cursorMove(-m_items_per_page);
				newsel = m_content->cursorGet();
				pageind = newsel % m_items_per_page; // rememer were we land in thsi page (could be different on topmost page)
				prevsel = newsel - pageind;			 // get top of page index
				// find first selectable entry in new page. First check bottom part, than upper part
				while (newsel != prevsel + m_items_per_page && m_content->cursorValid() && !m_content->currentCursorSelectable())
				{
					m_content->cursorMove(1);
					newsel = m_content->cursorGet();
				}
				if (!m_content->currentCursorSelectable()) // no selectable found in bottom part of page
				{
					m_content->cursorSet(prevsel + pageind);
					while (newsel != prevsel && !m_content->currentCursorSelectable())
					{
						m_content->cursorMove(-1);
						newsel = m_content->cursorGet();
					}
				}
				if (m_content->currentCursorSelectable())
					break;
				if (newsel == 0) // at top and nothing found . Go down till something selectable or old location
				{
					while (newsel != oldsel && !m_content->currentCursorSelectable())
					{
						m_content->cursorMove(1);
						newsel = m_content->cursorGet();
					}
					break;
				}
				m_content->cursorSet(prevsel + pageind);
			} while (newsel == prevsel);
			break;
		}
	}
	case pageDown:
	{
		if (m_flex_mode == flexGrid || m_flex_mode == flexHorizontal)
		{
			do
			{
				m_content->cursorMove(1);
				if (!m_content->cursorValid())
				{ // cursorMove reached end and left cursor position past the list. Must wrap around ?
					if (m_enabled_wrap_around)
						m_content->cursorHome();
					else
						m_content->cursorSet(oldsel);
				}
				newsel = m_content->cursorGet();
			} while (newsel != oldsel && !m_content->currentCursorSelectable());
			break;
		}
		else
		{
			int pageind;
			do
			{
				m_content->cursorMove(m_items_per_page);
				if (!m_content->cursorValid())
					m_content->cursorMove(-1);
				newsel = m_content->cursorGet();
				pageind = newsel % m_items_per_page;
				prevsel = newsel - pageind; // get top of page index
				// find a selectable entry in the new page. first look up then down from current screenlocation on the page
				while (newsel != prevsel && !m_content->currentCursorSelectable())
				{
					m_content->cursorMove(-1);
					newsel = m_content->cursorGet();
				}
				if (!m_content->currentCursorSelectable()) // no selectable found in top part of page
				{
					m_content->cursorSet(prevsel + pageind);
					do
					{
						m_content->cursorMove(1);
						newsel = m_content->cursorGet();
					} while (newsel != prevsel + m_items_per_page && m_content->cursorValid() && !m_content->currentCursorSelectable());
				}
				if (!m_content->cursorValid())
				{
					// we reached the end of the list
					// Back up till something selectable or we reach oldsel again
					// E.g this should bring us back to the last selectable item on the original page
					do
					{
						m_content->cursorMove(-1);
						newsel = m_content->cursorGet();
					} while (newsel != oldsel && !m_content->currentCursorSelectable());
					break;
				}
				if (newsel != prevsel + m_items_per_page)
					break;
				m_content->cursorSet(prevsel + pageind); // prepare for next page down
			} while (newsel == prevsel + m_items_per_page);
			break;
		}
	}
	case prevPage:
	{
		int pageind;
		do
		{
			m_content->cursorMove(-m_items_per_page);
			newsel = m_content->cursorGet();
			pageind = newsel % m_items_per_page; // rememer were we land in thsi page (could be different on topmost page)
			prevsel = newsel - pageind;			 // get top of page index
			// find first selectable entry in new page. First check bottom part, than upper part
			while (newsel != prevsel + m_items_per_page && m_content->cursorValid() && !m_content->currentCursorSelectable())
			{
				m_content->cursorMove(1);
				newsel = m_content->cursorGet();
			}
			if (!m_content->currentCursorSelectable()) // no selectable found in bottom part of page
			{
				m_content->cursorSet(prevsel + pageind);
				while (newsel != prevsel && !m_content->currentCursorSelectable())
				{
					m_content->cursorMove(-1);
					newsel = m_content->cursorGet();
				}
			}
			if (m_content->currentCursorSelectable())
				break;
			if (newsel == 0) // at top and nothing found . Go down till something selectable or old location
			{
				while (newsel != oldsel && !m_content->currentCursorSelectable())
				{
					m_content->cursorMove(1);
					newsel = m_content->cursorGet();
				}
				break;
			}
			m_content->cursorSet(prevsel + pageind);
		} while (newsel == prevsel);
		break;
	}
	case nextPage:
	{
		int pageind;
		do
		{
			m_content->cursorMove(m_items_per_page);
			if (!m_content->cursorValid())
				m_content->cursorMove(-1);
			newsel = m_content->cursorGet();
			pageind = newsel % m_items_per_page;
			prevsel = newsel - pageind; // get top of page index
			// find a selectable entry in the new page. first look up then down from current screenlocation on the page
			while (newsel != prevsel && !m_content->currentCursorSelectable())
			{
				m_content->cursorMove(-1);
				newsel = m_content->cursorGet();
			}
			if (!m_content->currentCursorSelectable()) // no selectable found in top part of page
			{
				m_content->cursorSet(prevsel + pageind);
				do
				{
					m_content->cursorMove(1);
					newsel = m_content->cursorGet();
				} while (newsel != prevsel + m_items_per_page && m_content->cursorValid() && !m_content->currentCursorSelectable());
			}
			if (!m_content->cursorValid())
			{
				// we reached the end of the list
				// Back up till something selectable or we reach oldsel again
				// E.g this should bring us back to the last selectable item on the original page
				do
				{
					m_content->cursorMove(-1);
					newsel = m_content->cursorGet();
				} while (newsel != oldsel && !m_content->currentCursorSelectable());
				break;
			}
			if (newsel != prevsel + m_items_per_page)
				break;
			m_content->cursorSet(prevsel + pageind); // prepare for next page down
		} while (newsel == prevsel + m_items_per_page);
		break;
	}
	}

	/* now, look wether the current selection is out of screen */
	m_selected = m_content->cursorGet();
	m_top = m_selected - (m_selected % m_items_per_page);

	/*  new scollmode by line if not on the first page */
	if(m_scroll_mode == byLine && m_content->size() > m_items_per_page && m_flex_mode != flexGrid)
	{

		int oldline = m_content->cursorRestoreLine();
		int max = m_content->size() - m_items_per_page;
		// eDebug("[eListbox] moveSelection 1 dir=%d oldline=%d oldsel=%d m_selected=%d m_items_per_page=%d sz=%d max=%d", dir, oldline, oldsel, m_selected, m_items_per_page, m_content->size(), max);

		bool jumpBottom = (dir == moveEnd);

		// if(dir == pageDown && m_selected > max && m_flex_mode == flexVertical_list) {
		// 	jumpBottom = true;
		// }

		if(dir == moveUp ||  dir == pageUp) {
			if(m_selected > oldsel) {
				jumpBottom = true;
			}
			else if (oldline > 0)
				oldline-= oldsel - m_selected;

			if(oldline < 0 && m_selected > m_items_per_page)
				oldline = 0;

		}

		if(m_last_selectable_item == -1 && dir == justCheck)
		{
			m_content->cursorEnd();
			do
			{
				m_content->cursorMove(-1);
				m_last_selectable_item = m_content->cursorGet();
			}
			while (!m_content->currentCursorSelectable());
			m_content->cursorSet(m_selected);
		}

		if(dir == moveDown || dir == pageDown) {
				
			int newline = oldline + (m_selected - oldsel);
			if(newline < m_items_per_page && newline > 0) {
				m_top = oldsel - oldline;
			}
			else{
				m_top = m_selected - (m_items_per_page - 1);
				if(m_selected < m_items_per_page )
					m_top=0;
			}
			if (m_last_selectable_item != m_content->size() - 1 && m_selected >= m_last_selectable_item)
				jumpBottom = true;
		}

		if(jumpBottom) {
			m_top = max;
		}
		else if (dir == justCheck)
		{
			if(m_first_selectable_item==-1)
			{
				m_first_selectable_item = 0;
				if(m_selected>0)
				{
					m_content->cursorHome();
					if (!m_content->currentCursorSelectable()) {
						do
						{
							m_content->cursorMove(1);
							m_first_selectable_item = m_content->cursorGet();
						}
						while (!m_content->currentCursorSelectable());
					}
					m_content->cursorSet(m_selected);
					if(oldline==0)
						oldline = m_selected;
				}
			}
			if(indexchanged && m_selected < m_items_per_page) {
				oldline = m_selected;
			}

			m_top = m_selected - oldline;
		}
		else if (dir == moveUp || dir == pageUp)
		{
			if(m_first_selectable_item > 0 && m_selected == m_first_selectable_item)
			{
				oldline = m_selected;
			}
			m_top = m_selected - oldline;
		}

		if(m_top < 0 || oldline < 0) {
			m_top = 0;
		}

		// eDebug("[eListbox] moveSelection 2 dir=%d oldline=%d oldtop=%d m_top=%d m_selected=%d m_items_per_page=%d sz=%d jumpBottom=%d columns=%d", dir, oldline, oldtop, m_top, m_selected, m_items_per_page, m_content->size(),jumpBottom, m_columns);

	}

	// if it is, then the old selection clip is irrelevant, clear it or we'll get artifacts
	if (m_top != oldtop && m_content)
		m_content->resetClip();

	if (oldsel != m_selected)
		/* emit */ selectionChanged();

	updateScrollBar();

	if (m_top != oldtop)
		invalidate();
	else if (m_selected != oldsel)
	{
		/* redraw the old and newly selected */
		ePoint newmargin = (m_selected > 0) ? m_margin : ePoint(0, 0);
		ePoint oldmargin = (oldsel > 0) ? m_margin : ePoint(0, 0);
		int shadow_offset_x = (m_style.m_shadow_set && m_style.m_shadow) ? (((m_selectionwidth + 40) - m_selectionwidth) / 2) : 0;
		int shadow_offset_y = (m_style.m_shadow_set && m_style.m_shadow) ? -(((m_selectionheight + 40) - m_selectionheight) / 2) + yoffset : yoffset;
		int shadow_size = (m_style.m_shadow_set && m_style.m_shadow) ? 40 : 0;
		if (m_flex_mode == flexHorizontal)
		{
			gRegion inv = eRect((((m_itemwidth + newmargin.x()) * (m_selected - m_top)) - shadow_offset_x) + xoffset, shadow_offset_y, m_selectionwidth + shadow_size, m_selectionheight + shadow_size);
			inv |= eRect((((m_itemwidth + oldmargin.x()) * (oldsel - m_top)) - shadow_offset_x) + xoffset, shadow_offset_y, m_selectionwidth + shadow_size, m_selectionheight + shadow_size);
			invalidate(inv);
		}
		else if (m_flex_mode == flexGrid)
		{
			shadow_offset_y = (m_style.m_shadow_set && m_style.m_shadow) ? (((m_selectionheight + 40) - m_selectionheight) / 2) : 0;
			gRegion inv = eRect((((m_itemwidth + newmargin.x()) * ((m_selected - m_top) % m_columns)) - shadow_offset_x) + xoffset, (((m_itemheight + newmargin.y()) * ((m_selected - m_top) / m_columns)) - shadow_offset_y) + yoffset, m_selectionwidth + shadow_size, m_selectionheight + shadow_size);
			inv |= eRect((((m_itemwidth + oldmargin.x()) * ((oldsel - m_top) % m_columns)) - shadow_offset_x) + xoffset, (((m_itemheight + oldmargin.y()) * ((oldsel - m_top) / m_columns)) - shadow_offset_y) + yoffset, m_selectionwidth + shadow_size, m_selectionheight + shadow_size);
			invalidate(inv);
		}
		else
		{
			gRegion inv = eRect(xoffset, ((m_itemheight + newmargin.y()) * (m_selected - m_top)) + yoffset, m_selectionwidth, m_selectionheight);
			inv |= eRect(xoffset, ((m_itemheight + oldmargin.y()) * (oldsel - m_top)) + yoffset, m_selectionwidth, m_selectionheight);
			invalidate(inv);
		}
	}
}

void eListbox::moveSelectionTo(int index)
{
	if (m_content)
	{
		m_content->cursorSet(index);
		moveSelection(justCheck);
	}
}

int eListbox::getCurrentIndex()
{
	if (m_content && m_content->cursorValid())
		return m_content->cursorGet();
	return 0;
}

void eListbox::updateScrollBar()
{
	if (!m_content || m_scrollbar_mode == showNever )
		return;
	int entries = m_content->size();
	if (m_content_changed)
	{
		int width = size().width();
		int height = size().height();
		m_content_changed = false;
		if (m_scrollbar_mode == showLeft)
		{
			m_content->setSize(eSize(width-m_scrollbar_width-5, m_itemheight));
			m_scrollbar->move(ePoint(0, 0));
			m_scrollbar->resize(eSize(m_scrollbar_width, height));
			if (entries > m_items_per_page)
			{
				m_scrollbar->show();
			}
			else
			{
				m_scrollbar->hide();
			}
		}
		else if (entries > m_items_per_page || m_scrollbar_mode == showAlways)
		{
			m_scrollbar->move(ePoint(width-m_scrollbar_width, 0));
			m_scrollbar->resize(eSize(m_scrollbar_width, height));
			m_content->setSize(eSize(width-m_scrollbar_width-5, m_itemheight));
			m_scrollbar->show();
		}
		else
		{
			m_content->setSize(eSize(width, m_itemheight));
			m_scrollbar->hide();
		}
	}
	if (m_items_per_page && entries)
	{
		int curVisiblePage = m_top / m_items_per_page;
		if (m_prev_scrollbar_page != curVisiblePage)
		{
			m_prev_scrollbar_page = curVisiblePage;
			int pages = entries / m_items_per_page;
			if ((pages*m_items_per_page) < entries)
				++pages;
			int start=(m_top*100)/(pages*m_items_per_page);
			int vis=(m_items_per_page*100+pages*m_items_per_page-1)/(pages*m_items_per_page);
			if (vis < 3)
				vis=3;
			m_scrollbar->setStartEnd(start,start+vis);
		}
	}
}

int eListbox::getEntryTop()
{
	return (m_selected - m_top) * m_itemheight;
}

int eListbox::event(int event, void *data, void *data2)
{
	switch (event)
	{
	case evtPaint:
	{
		ePtr<eWindowStyle> style;

		if (!m_content)
			return eWidget::event(event, data, data2);
		ASSERT(m_content);

		getStyle(style);

		if (!m_content)
			return 0;

		gPainter &painter = *(gPainter *)data2;

		m_content->cursorSave();
		m_content->cursorMove(m_top - m_selected);

		gRegion entryrect;
		const gRegion &paint_region = *(gRegion *)data;
		gRGB last_col;
		gRGB def_col = m_style.m_background_color;
		int line = 0;

		if (m_flex_mode != flexVertical)
		{
			if (!isTransparent())
			{
				painter.clip(paint_region);
				if (m_style.m_background_color_global_set)
					painter.setBackgroundColor(m_style.m_background_color_global);
				else
				{
					if (m_style.m_background_color_set) painter.setBackgroundColor(m_style.m_background_color);
				}
				painter.clear();
				painter.clippop();
			}
		}

		for (int posx = 0, posy = 0, i = 0; (m_flex_mode == flexVertical) ? i <= m_items_per_page : i < m_items_per_page; posx += m_itemwidth + m_margin.x(), ++i)
		{
			if(m_style.m_background_color_set && m_style.m_background_color_rows_set){
				if (m_style.m_background_col_current == def_col || i == 0){
					m_style.m_background_color = def_col;
					m_style.m_background_col_current = m_style.m_background_color_rows;
				}
				else if (m_style.m_background_col_current == m_style.m_background_color_rows && last_col != m_style.m_background_color_rows && m_content->cursorValid() && i > 0){
					m_style.m_background_color = m_style.m_background_color_rows;
					m_style.m_background_col_current = def_col;
				}
				else{
					m_style.m_background_color = def_col;
					m_style.m_background_col_current = m_style.m_background_color_rows;
				}
				last_col = m_style.m_background_color;
			}

			if (m_flex_mode == flexGrid && i > 0)
			{
				if (i % m_columns == 0)
				{
					posy += m_itemheight + m_margin.y();
					posx = 0;
				}
			}
			if (m_flex_mode == flexVertical)
			{
				posx = 0;
				if (i > 0)
					posy += m_itemheight + m_margin.y();
			}

			bool sel = (m_selected == m_content->cursorGet() && m_content->size() && m_selection_enabled);
			if(sel)
				line = i;

			entryrect = eRect(posx + xoffset, posy + yoffset, m_selectionwidth, m_selectionheight);
			gRegion entry_clip_rect = paint_region & entryrect;

			if (!entry_clip_rect.empty())
			{
				if (m_flex_mode != flexVertical && m_content->cursorValid())
				{
					if (i != (m_selected - m_top) || !m_selection_enabled)
						m_content->paint(painter, *style, ePoint(posx + xoffset, posy + yoffset), 0);
				}
				else if (m_flex_mode == flexVertical)
					m_content->paint(painter, *style, ePoint(posx + xoffset, posy + yoffset), sel);
			}

			m_content->cursorMove(+1);
			entryrect.moveBy(ePoint(posx + xoffset, posy + yoffset));
		}
		m_content->cursorSaveLine(line);
		m_content->cursorRestore();
		if (m_selected == m_content->cursorGet() && m_content->size() && m_selection_enabled && m_flex_mode != flexVertical)
		{
			ePoint margin = (m_selected > 0) ? m_margin : ePoint(0, 0);
			int posx_sel = (m_flex_mode == flexGrid) ? (m_itemwidth + margin.x()) * ((m_selected - m_top) % m_columns) : (m_itemwidth + margin.x()) * (m_selected - m_top);
			int posy_sel = (m_flex_mode == flexGrid) ? (m_itemheight + margin.y()) * ((m_selected - m_top) / m_columns) : 0;
			if (m_content)
			{
				if (m_flex_mode == flexVertical)
				{
					posx_sel = 0;
					posy_sel = (m_itemheight + margin.y()) * (m_selected - m_top);
				}

				if (m_style.m_shadow_set && m_style.m_shadow)
				{
					int shadow_x = posx_sel - (((m_selectionwidth + 40) - m_selectionwidth) / 2);
					int shadow_y = posy_sel - (((m_selectionheight + 40) - m_selectionheight) / 2);
					eRect rect(shadow_x + xoffset, shadow_y + yoffset, m_selectionwidth + 40, m_selectionheight + 40);

					painter.clip(rect);
					painter.blitScale(m_style.m_shadow , rect, rect, gPainter::BT_ALPHABLEND);
					painter.clippop();
				}

				m_content->paint(painter, *style, ePoint(posx_sel + xoffset, posy_sel + yoffset), 1);
			}
		}

		// clear/repaint empty/unused space between scrollbar and listboxentrys
		if (m_scrollbar && m_scrollbar->isVisible() && !isTransparent() && m_flex_mode == flexVertical)
		{
			style->setStyle(painter, eWindowStyle::styleListboxNormal);
			painter.clip(eRect(m_scrollbar->position() - ePoint(m_scrollbar_offset,0), eSize(m_scrollbar_offset,m_scrollbar->size().height())));
			if (m_style.m_background_color_set) painter.setBackgroundColor(m_style.m_background_color);
			painter.clear();
			painter.clippop();
		}

		return 0;
	}

	case evtChangedSize:
		recalcSize();
		return eWidget::event(event, data, data2);

	case evtAction:
		if (isVisible() && !isLowered())
		{
			moveSelection((long)data2);
			return 1;
		}
		return 0;
	default:
		return eWidget::event(event, data, data2);
	}
}

void eListbox::recalcSize()
{
	m_content_changed=true;
	m_prev_scrollbar_page=-1;
	if (m_content)
		m_content->setSize(eSize(size().width(), m_itemheight));
	m_items_per_page = size().height() / m_itemheight;

	if (m_items_per_page < 0) /* TODO: whyever - our size could be invalid, or itemheigh could be wrongly specified. */
 		m_items_per_page = 0;

	moveSelection(justCheck);
}

void eListbox::setItemHeight(int h)
{
	if (h)
		m_itemheight = h;
	else
		m_itemheight = 20;
	recalcSize();
}

void eListbox::setItemWidth(int w)
{
	if (w && m_itemwidth != w)
	{
		m_itemwidth = w;
		m_itemwidth_set = true;
		recalcSize();
		invalidate();
	}
}

void eListbox::setSelectionHeight(int h)
{
	if (h && m_selectionheight != h)
	{
		m_selectionheight = h;
		m_selectionheight_set = true;
		recalcSize();
		invalidate();
	}
}

void eListbox::setSelectionWidth(int w)
{
	if (w && m_selectionwidth != w)
	{
		m_selectionwidth = w;
		m_selectionwidth_set = true;
		recalcSize();
		invalidate();
	}
}

void eListbox::setColumns(int c)
{
	if (c >= 1 && m_columns != c)
	{
		m_columns = c;
		m_columns_set = true;
		recalcSize();
	}
}

void eListbox::setRows(int r)
{

	if (r > 1 && m_rows != r)
	{
		m_rows = r;
		m_rows_set = true;
		recalcSize();
	}
}

void eListbox::setMargin(const ePoint &margin)
{
	if ((margin.x() >= 0 && margin.y() >= 0))
	{
		m_margin = margin;
		recalcSize();
	}
}

void eListbox::setSelectionEnable(int en)
{
	if (m_selection_enabled == en)
		return;
	m_selection_enabled = en;
	entryChanged(m_selected); /* redraw current entry */
}

void eListbox::entryAdded(int index)
{
	if (m_content && (m_content->size() % m_items_per_page) == 1)
		m_content_changed=true;
	/* manage our local pointers. when the entry was added before the current position, we have to advance. */

		/* we need to check <= - when the new entry has the (old) index of the cursor, the cursor was just moved down. */
	if (index <= m_selected)
		++m_selected;
	if (index <= m_top)
		++m_top;

		/* we have to check wether our current cursor is gone out of the screen. */
		/* moveSelection will check for this case */
	moveSelection(justCheck);

		/* now, check if the new index is visible. */
	if ((m_top <= index) && (index < (m_top + m_items_per_page)))
	{
			/* todo, calc exact invalidation... */
		invalidate();
	}
}

void eListbox::entryRemoved(int index)
{
	if (m_content && !(m_content->size() % m_items_per_page))
		m_content_changed=true;

	if (index == m_selected && m_content)
		m_selected = m_content->cursorGet();

	if (m_content && m_content->cursorGet() >= m_content->size())
		moveSelection(moveUp);
	else
		moveSelection(justCheck);

	if ((m_top <= index) && (index < (m_top + m_items_per_page)))
	{
			/* todo, calc exact invalidation... */
		invalidate();
	}
}

void eListbox::entryChanged(int index)
{
	if ((m_top <= index) && (index < (m_top + m_items_per_page)))
	{
		gRegion inv = eRect(0, m_itemheight * (index-m_top), size().width(), m_itemheight);
		invalidate(inv);
	}
}

void eListbox::entryReset(bool selectionHome)
{
	m_content_changed = true;
	m_prev_scrollbar_page = -1;
	int oldsel;

	if (selectionHome)
	{
		if (m_content)
			m_content->cursorHome();
		m_top = 0;
		m_selected = 0;
	}

	if (m_content && (m_selected >= m_content->size()))
	{
		if (m_content->size())
			m_selected = m_content->size() - 1;
		else
			m_selected = 0;
		m_content->cursorSet(m_selected);
	}

	oldsel = m_selected;
	moveSelection(justCheck);
		/* if oldsel != m_selected, selectionChanged was already
		   emitted in moveSelection. we want it in any case, so otherwise,
		   emit it now. */
	if (oldsel == m_selected)
		/* emit */ selectionChanged();
	invalidate();
}

void eListbox::setFont(gFont *font)
{
	m_style.m_font = font;
}

void eListbox::setSecondFont(gFont *font)
{
	m_style.m_secondfont = font;
}

void eListbox::setVAlign(int align)
{
	m_style.m_valign = align;
}

void eListbox::setHAlign(int align)
{
	m_style.m_halign = align;
}

void eListbox::setTextOffset(const ePoint &textoffset)
{
	m_style.m_text_offset = textoffset;
}

void eListbox::setCenterList(bool center)
{
	m_center_list = center;
}

void eListbox::setBackgroundColor(gRGB &col)
{
	m_style.m_background_color = col;
	m_style.m_background_color_set = 1;
}

void eListbox::setBackgroundColorRows(gRGB &col)
{
	m_style.m_background_color_rows = col;
	m_style.m_background_color_rows_set = 1;
}

void eListbox::setBackgroundColorGlobal(gRGB &col)
{
	m_style.m_background_color_global = col;
	m_style.m_background_color_global_set = 1;
}

void eListbox::setBackgroundColorSelected(gRGB &col)
{
	m_style.m_background_color_selected = col;
	m_style.m_background_color_selected_set = 1;
}

void eListbox::setForegroundColor(gRGB &col)
{
	m_style.m_foreground_color = col;
	m_style.m_foreground_color_set = 1;
}

void eListbox::setForegroundColorSelected(gRGB &col)
{
	m_style.m_foreground_color_selected = col;
	m_style.m_foreground_color_selected_set = 1;
}

void eListbox::setBorderColor(const gRGB &col)
{
	m_style.m_border_color = col;
}

void eListbox::setBorderWidth(int size)
{
	m_style.m_border_size = size;
	if (m_scrollbar) m_scrollbar->setBorderWidth(size);
}

void eListbox::setScrollbarSliderBorderWidth(int size)
{
	m_style.m_scrollbarsliderborder_size = size;
	m_style.m_scrollbarsliderborder_size_set = 1;
	if (m_scrollbar) m_scrollbar->setSliderBorderWidth(size);
}

void eListbox::setScrollbarWidth(int size)
{
	m_scrollbar_width = size;
}

void eListbox::setScrollbarOffset(int size)
{
	m_scrollbar_offset = size;
}

void eListbox::setBackgroundPicture(ePtr<gPixmap> &pm)
{
	m_style.m_background = pm;
}

void eListbox::setSelectionPicture(ePtr<gPixmap> &pm)
{
	m_style.m_selection = pm;
}

void eListbox::setSliderPicture(ePtr<gPixmap> &pm)
{
	m_scrollbarpixmap = pm;
	if (m_scrollbar && m_scrollbarpixmap) m_scrollbar->setPixmap(pm);
}

void eListbox::setSliderForegroundColor(gRGB &col)
{
	m_style.m_sliderforeground_color = col;
	m_style.m_sliderforeground_color_set = 1;
	if (m_scrollbar) m_scrollbar->setSliderForegroundColor(col);
}

void eListbox::setSliderBorderColor(const gRGB &col)
{
	m_style.m_sliderborder_color = col;
	m_style.m_sliderborder_color_set = 1;
	if (m_scrollbar) m_scrollbar->setSliderBorderColor(col);
}

void eListbox::setSliderBorderWidth(int size)
{
	m_style.m_sliderborder_size = size;
	if (m_scrollbar) m_scrollbar->setSliderBorderWidth(size);
}

void eListbox::setScrollbarBackgroundPicture(ePtr<gPixmap> &pm)
{
	m_scrollbarbackgroundpixmap = pm;
	if (m_scrollbar && m_scrollbarbackgroundpixmap) m_scrollbar->setBackgroundPixmap(pm);
}

void eListbox::invalidate(const gRegion &region)
{
	gRegion tmp(region);
	if (m_content)
		m_content->updateClip(tmp);
	eWidget::invalidate(tmp);
}

struct eListboxStyle *eListbox::getLocalStyle(void)
{
		/* transparency is set directly in the widget */
	m_style.m_transparent_background = isTransparent();
	return &m_style;
}
