#include <lib/gui/elistbox.h>
#include <lib/gui/elistboxcontent.h>
#include <lib/gui/eslider.h>
#include <lib/actions/action.h>
#include <lib/gdi/epng.h>
#include <lib/base/wrappers.h>

eListbox::eListbox(eWidget *parent) :
	eWidget(parent), m_scrollbar_mode(showNever), m_scroll_mode(byPage), m_prev_scrollbar_page(-1),
	m_content_changed(false), m_enabled_wrap_around(false), m_itemheight_set(false),
	m_itemwidth_set(false), m_selectionheight_set(false), m_selectionwidth_set(false), m_columns_set(false), m_rows_set(false), m_scrollbar_width(20), m_scrollbar_border_width(1), m_scrollbar_offset(5),
	m_top(0), m_selected(0), m_layout_mode(LayoutVertical), m_itemheight(20), m_itemwidth(20), m_selectionheight(20), m_selectionwidth(20), m_columns(2), m_rows(2),
	m_items_per_page(0), m_selection_enabled(1), xoffset(0), yoffset(0), m_native_keys_bound(false), m_first_selectable_item(-1), m_last_selectable_item(-1), m_center_list(true), m_scrollbar(nullptr),
	// NEW animation-related initializations
	m_animation_timer(nullptr), m_animation_offset(0),
	m_animation_target_offset(0), m_animation_step(20),
	m_animating(false), m_animation_direction(0)
{
	memset(static_cast<void*>(&m_style), 0, sizeof(m_style));
	m_style.m_text_offset = ePoint(1,1);
//	setContent(new eListboxStringContent());
	//eTimer::create(m_animation_timer);  // no 'this'
	m_animation_timer = eTimer::create(eApp);
	CONNECT(m_animation_timer->timeout, eListbox::animateStep);

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

void eListbox::animateStep()
{
    if (!m_animating)
        return;

    m_animation_offset += m_animation_step;

    if (m_animation_offset >= m_animation_target_offset)
    {
        m_animating = false;
        m_animation_offset = 0;
        // m_top and m_selected are updated in moveSelection
        return;
    }

    invalidate();  // trigger repaint
    m_animation_timer->start(20, true);  // continue animation
}

void eListbox::setLayoutMode(int mode)
{
	if (mode <= 2)
	{
		int old_mode = m_layout_mode;
		m_layout_mode = mode;
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
	if(indexchanged)
	{
		dir -= 100;
	}

	//dir = (((dir == pageUp) && (m_layout_mode == LayoutVertical)) || ((dir == moveUp) && (m_layout_mode == LayoutHorizontal))) ? prevPage : 
      	//(((dir == pageDown) && (m_layout_mode == LayoutVertical)) ||  ((dir == moveDown) && (m_layout_mode == LayoutHorizontal))) ? nextPage : dir;

	switch (dir)
	{
	case moveEnd:
		m_content->cursorEnd();
		[[fallthrough]];
	case pageUp:
	case moveUp:
		do
		{
			m_content->cursorMove((m_layout_mode == LayoutGrid && dir == moveUp) ? -m_columns : -1);
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
	case pageDown:
		
	/*case moveDown:
	    {
	        // Move cursor up to index 3, then fix cursor and slide list
	        if (m_layout_mode == LayoutHorizontal && oldsel < 3)
	        {
	            do
	            {
	                m_content->cursorMove(1);
	                newsel = m_content->cursorGet();
	            } while (newsel != oldsel && !m_content->currentCursorSelectable() && newsel < 3);
	            m_selected = newsel;
	            m_top = 0; // Ensure m_top stays at 0 until index 3
	        }
	        else if (m_layout_mode == LayoutHorizontal)
	        {
	            // Fix cursor at index 3 relative to visible area, slide list left
	            if (!m_animating)
	            {
	                m_top += 1; // Slide list by incrementing m_top
	                m_selected = m_top + 3; // Fix cursor at index 3 relative to m_top
	                m_content->cursorSet(m_selected);
	                m_animation_direction = 1;  // moving right
	                m_animation_offset = 0;
	                m_animation_target_offset = m_itemwidth + m_margin.x();
	                if (m_selected < m_content->size()) // Only animate if within bounds
	                {
	                    m_animating = true;
	                    m_animation_timer->start(20, true);
	                }
	                invalidate(); // Trigger repaint for new m_top
	                return;
	            }
	            // If animating, do nothing to avoid interrupting animation
	        }
	        else
	        {
	            do
	            {
	                m_content->cursorMove((m_layout_mode == LayoutGrid && dir == moveDown) ? m_columns : 1);
	                if (!m_content->cursorValid())
	                {
	                    if (m_enabled_wrap_around)
	                    {
	                        if (oldsel + 1 < m_content->size() && m_layout_mode == LayoutGrid && dir == moveDown)
	                            m_content->cursorMove(-1);
	                        else
	                            m_content->cursorHome();
	                    }
	                    else
	                    {
	                        if (oldsel + 1 < m_content->size() && m_layout_mode == LayoutGrid && dir == moveDown)
	                            m_content->cursorMove(-1);
	                        else
	                            m_content->cursorSet(oldsel);
	                    }
	                }
	                newsel = m_content->cursorGet();
	            } while (newsel != oldsel && !m_content->currentCursorSelectable());
	            m_selected = newsel;
	        }
	        break;
	    }*/

	/*case moveDown:
	{
		if (m_layout_mode == LayoutHorizontal)
		{
			// --- Logic for Horizontal Layout ---
			eDebug("[MyListbox-Debug] oldsel=%d, m_top=%d, items_per_page=%d, size=%d", oldsel, m_top, m_items_per_page, m_content->size());
			
			// Guard clause: if already at the last item, do nothing.
			if (oldsel >= m_content->size() - 1)
			{
				if (m_animating)
				{
					m_animating = false;
					m_animation_offset = 0;
					m_animation_timer->stop();
				}
				invalidate();
				return; // Return is appropriate here as it's a full stop.
			}
			
			int last_item_index = m_content->size() - 1;
			int last_visible_index = m_top + m_items_per_page - 1;
			bool stop_sliding = (last_visible_index >= last_item_index - 1);

			if (!stop_sliding && oldsel >= 3)
			{
				// --- BEHAVIOR 2: SLIDING LIST ANIMATION ---
				// This is the animated sliding part. It is a special case and MUST remain
				// self-contained with its own invalidation and return statement because
				// it only triggers the start of an animation.
				eDebug("[MyListbox-Debug] Sliding: oldsel=%d, old_top=%d", oldsel, m_top);

				m_top += 1;
				m_selected = m_top + 3;
				m_content->cursorSet(m_selected);

				eDebug("[MyListbox-Debug] Sliding: new m_top=%d, new m_selected=%d", m_top, m_selected);

				m_animation_direction = 1;
				m_animation_offset = 0;
				m_animation_target_offset = m_itemwidth + m_margin.x();
				m_animating = true;
				m_animation_timer->start(20, true);
				invalidate();
				return; // This return is CORRECT and necessary for the animation.
			}
			
			// --- BEHAVIOR 1: NORMAL CURSOR MOVEMENT (Non-Sliding) ---
			// This block now handles regular, non-animated movement.
			// It will NOT return, but will fall through to the common logic at the end.
			eDebug("[MyListbox-Debug] Sliding stopped or within initial range, moving cursor incrementally.");

			if (m_animating)
			{
				m_animating = false;
				m_animation_offset = 0;
				m_animation_timer->stop();
			}

			do
			{
				m_content->cursorMove(1);
				newsel = m_content->cursorGet();
			} while (newsel != oldsel && !m_content->currentCursorSelectable());
			m_selected = newsel; // Update m_selected for the common logic below
		}
		else
		{
			// --- This is the original, unchanged logic for other layout modes (Vertical, Grid) ---
			do
			{
				m_content->cursorMove((m_layout_mode == LayoutGrid && dir == moveDown) ? m_columns : 1);
				if (!m_content->cursorValid())
				{
					if (m_enabled_wrap_around)
					{
						if (oldsel + 1 < m_content->size() && m_layout_mode == LayoutGrid && dir == moveDown)
							m_content->cursorMove(-1);
						else
							m_content->cursorHome();
					}
					else
					{
						if (oldsel + 1 < m_content->size() && m_layout_mode == LayoutGrid && dir == moveDown)
							m_content->cursorMove(-1);
						else
							m_content->cursorSet(oldsel);
					}
				}
				newsel = m_content->cursorGet();
			} while (newsel != oldsel && !m_content->currentCursorSelectable());
			m_selected = newsel;
		}
		break;
	}*/
		
	case moveDown:
		{
		    if (m_layout_mode == LayoutHorizontal)
		    {
		        // --- Logic for Horizontal Layout ---
		        eDebug("[MyListbox-Debug] oldsel=%d, m_top=%d, items_per_page=%d, size=%d", oldsel, m_top, m_items_per_page, m_content->size());
		        
		        // Guard clause: if already at the last item, do nothing.
		        if (oldsel >= m_content->size() - 1)
		        {
		            if (m_animating)
		            {
		                m_animating = false;
		                m_animation_offset = 0;
		                m_animation_timer->stop();
		            }
		            invalidate();
		            return; // Return is appropriate here as it's a full stop.
		        }
		        
		        int last_item_index = m_content->size() - 1;
		        int last_visible_index = m_top + m_items_per_page - 1;
		        bool stop_sliding = (last_visible_index >= last_item_index - 1);
		
		        if (!stop_sliding && oldsel >= 3)
		        {
		            // --- BEHAVIOR 2: SLIDING LIST ANIMATION ---
		            eDebug("[MyListbox-Debug] Sliding: oldsel=%d, old_top=%d", oldsel, m_top);
		
		            m_top += 1;
		            m_selected = m_top + 3;
		            m_content->cursorSet(m_selected);
		
		            eDebug("[MyListbox-Debug] Sliding: new m_top=%d, new m_selected=%d", m_top, m_selected);
		
		            m_animation_direction = 1;
		            m_animation_offset = 0;
		            m_animation_target_offset = m_itemwidth + m_margin.x();
		            m_animating = true;
		            m_animation_timer->start(20, true);
		            invalidate();
		            return; // This return is CORRECT and necessary for the animation.
		        }
		        
		        // --- BEHAVIOR 1: NORMAL CURSOR MOVEMENT (Non-Sliding) ---
		        eDebug("[MyListbox-Debug] Sliding stopped or within initial range, moving cursor incrementally.");
		
		        if (m_animating)
		        {
		            m_animating = false;
		            m_animation_offset = 0;
		            m_animation_timer->stop();
		        }
		
		        // Move cursor to the next selectable item
		        do
		        {
		            m_content->cursorMove(1);
		            newsel = m_content->cursorGet();
		        } while (newsel != oldsel && !m_content->currentCursorSelectable());
		        m_selected = newsel;
		
		        // If sliding has stopped, adjust m_top to keep the last item visible
		        if (stop_sliding)
		        {
		            // Ensure the last item remains visible by setting m_top appropriately
		            m_top = m_content->size() - m_items_per_page;
		            if (m_top < 0) m_top = 0; // Prevent negative m_top
		        }
		        else if (m_selected >= m_items_per_page)
		        {
		            // For non-sliding movement beyond initial range, adjust m_top to keep cursor in view
		            m_top = m_selected - 3; // Keep cursor at index 3 relative to visible area
		            if (m_top < 0) m_top = 0;
		        }
		    }
		    else
		    {
		        // --- Original logic for other layout modes (Vertical, Grid) ---
		        do
		        {
		            m_content->cursorMove((m_layout_mode == LayoutGrid && dir == moveDown)
		
		 ? m_columns : 1);
		            if (!m_content->cursorValid())
		            {
		                if (m_enabled_wrap_around)
		                {
		                    if (oldsel + 1 < m_content->size() && m_layout_mode == LayoutGrid && dir == moveDown)
		                        m_content->cursorMove(-1);
		                    else
		                        m_content->cursorHome();
		                }
		                else
		                {
		                    if (oldsel + 1 < m_content->size() && m_layout_mode == LayoutGrid && dir == moveDown)
		                        m_content->cursorMove(-1);
		                    else
		                        m_content->cursorSet(oldsel);
		                }
		            }
		            newsel = m_content->cursorGet();
		        } while (newsel != oldsel && !m_content->currentCursorSelectable());
		        m_selected = newsel;
		    }
		    break;
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
	if (m_scroll_mode == byLine && m_content->size() > m_items_per_page && m_layout_mode != LayoutGrid)
	{

		int oldline = m_content->cursorRestoreLine();
		int max = m_content->size() - m_items_per_page;
		// eDebug("[eListbox] moveSelection 1 dir=%d oldline=%d oldsel=%d m_selected=%d m_items_per_page=%d sz=%d max=%d", dir, oldline, oldsel, m_selected, m_items_per_page, m_content->size(), max);

		bool jumpBottom = (dir == moveEnd);

		// if(dir == pageDown && m_selected > max && m_layout_mode == LayoutVertical_list) {
		// 	jumpBottom = true;
		// }

		if (dir == moveUp || dir == pageUp)
		{
			if (m_selected > oldsel)
			{
				jumpBottom = true;
			}
			else if (oldline > 0)
				oldline -= oldsel - m_selected;

			if (oldline < 0 && m_selected > m_items_per_page)
				oldline = 0;
		}

		if (m_last_selectable_item == -1 && dir == justCheck)
		{
			m_content->cursorEnd();
			do
			{
				m_content->cursorMove(-1);
				m_last_selectable_item = m_content->cursorGet();
			} while (!m_content->currentCursorSelectable());
			m_content->cursorSet(m_selected);
		}

		if (dir == moveDown || dir == pageDown)
		{

			int newline = oldline + (m_selected - oldsel);
			if (newline < m_items_per_page && newline > 0)
			{
				m_top = oldsel - oldline;
			}
			else
			{
				m_top = m_selected - (m_items_per_page - 1);
				if (m_selected < m_items_per_page)
					m_top = 0;
			}
			if (m_last_selectable_item != m_content->size() - 1 && m_selected >= m_last_selectable_item)
				jumpBottom = true;
		}

		if (jumpBottom)
		{
			m_top = max;
		}
		else if (dir == justCheck)
		{
			if (m_first_selectable_item == -1)
			{
				m_first_selectable_item = 0;
				if (m_selected > 0)
				{
					m_content->cursorHome();
					if (!m_content->currentCursorSelectable())
					{
						do
						{
							m_content->cursorMove(1);
							m_first_selectable_item = m_content->cursorGet();
						} while (!m_content->currentCursorSelectable());
					}
					m_content->cursorSet(m_selected);
					if (oldline == 0)
						oldline = m_selected;
				}
			}
			if (indexchanged && m_selected < m_items_per_page)
			{
				oldline = m_selected;
			}

			m_top = m_selected - oldline;
		}
		else if (dir == moveUp || dir == pageUp)
		{
			if (m_first_selectable_item > 0 && m_selected == m_first_selectable_item)
			{
				oldline = m_selected;
			}
			m_top = m_selected - oldline;
		}

		if (m_top < 0 || oldline < 0)
		{
			m_top = 0;
		}

		// eDebug("[eListbox] moveSelection 2 dir=%d oldline=%d oldtop=%d m_top=%d m_selected=%d m_items_per_page=%d sz=%d jumpBottom=%d columns=%d", dir, oldline, oldtop, m_top, m_selected, m_items_per_page, m_content->size(),jumpBottom, m_columns);
	}

	// if it is, then the old selection clip is irrelevant, clear it or we'll get artifacts
	if (m_top != oldtop && m_content && !m_animating)
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
		if (m_layout_mode == LayoutHorizontal)
		{
			gRegion inv = eRect((((m_itemwidth + newmargin.x()) * (m_selected - m_top)) - shadow_offset_x) + xoffset, shadow_offset_y, m_selectionwidth + shadow_size, m_selectionheight + shadow_size);
			inv |= eRect((((m_itemwidth + oldmargin.x()) * (oldsel - m_top)) - shadow_offset_x) + xoffset, shadow_offset_y, m_selectionwidth + shadow_size, m_selectionheight + shadow_size);
			invalidate(inv);
		}
		else if (m_layout_mode == LayoutGrid)
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

int eListbox::getCurrentPage()
{
	if (!m_content)
		return 0;
	return (m_content->cursorGet() / m_items_per_page) + 1;
}

int eListbox::getTotalPages()
{
	if (!m_content)
		return 0;
	return m_content->size() / m_items_per_page + (m_content->size() % m_items_per_page > 0 ? 1 : 0);
}

ePoint eListbox::getSelectionAbsolutePosition()
{
	ePoint abspos = ePoint(0, 0);
	if (!m_content || !m_selection_enabled)
		return abspos;

	ePoint margin = (m_selected > 0) ? m_margin : ePoint(0, 0);
	if (m_layout_mode == LayoutHorizontal || m_layout_mode == LayoutGrid)
	{
		int posx_sel = (m_itemwidth + margin.x()) * (m_selected % m_columns);
		int posy_sel = (m_itemheight + margin.y()) * ((m_selected - m_top) / m_columns);
		abspos = ePoint(posx_sel + xoffset, posy_sel + yoffset);
	}
	else
	{
		int posy_sel = (m_itemheight + margin.y()) * (m_selected - m_top);
		abspos = ePoint(0 + xoffset, posy_sel + yoffset);
	}
	return abspos;
}

void eListbox::updateScrollBar()
{
	if (!m_scrollbar || !m_content || m_scrollbar_mode == showNever)
		return;
	int entries = m_content->size();
	bool scrollbarvisible = m_scrollbar->isVisible();
	if (m_content_changed)
	{
		int width = size().width();
		int height = size().height();
		m_content_changed = false;

		if (entries > m_items_per_page || m_scrollbar_mode == showAlways)
		{
			if (m_layout_mode == LayoutVertical)
			{
				m_scrollbar->setOrientation(eSlider::orVertical);
				m_scrollbar->move(ePoint(width - m_scrollbar_width, 0));
				m_scrollbar->resize(eSize(m_scrollbar_width, height));
				m_content->setSize(eSize(width - m_scrollbar_width - m_scrollbar_offset, m_itemheight));
				m_content->setSelectionSize(eSize(width - m_scrollbar_width - m_scrollbar_offset, m_itemheight));
			}
			if (m_layout_mode == LayoutHorizontal)
			{
				int posx = (m_selectionwidth > m_itemwidth) ? ((m_selectionwidth - m_itemwidth) / 2) : 0;
				m_scrollbar->setOrientation(eSlider::orHorizontal);
				m_scrollbar->move(ePoint(posx + xoffset, m_selectionheight + m_margin.y() + yoffset));
				m_scrollbar->resize(eSize(((m_itemwidth + m_margin.x()) * m_columns) - m_margin.x(), m_scrollbar_width));
			}
			if (m_layout_mode == LayoutGrid)
			{
				int posx = (m_selectionwidth > m_itemwidth) ? ((m_selectionwidth - m_itemwidth) / 2) : 0;
				int posy = (m_selectionheight > m_itemheight) ? ((m_selectionheight - m_itemheight) / 2) : 0;
				m_scrollbar->setOrientation(eSlider::orVertical);
				m_scrollbar->move(ePoint( ((((m_itemwidth + m_margin.x()) * m_columns) - m_margin.x()) + m_margin.x() + xoffset + posx), posy + yoffset));
				m_scrollbar->resize(eSize(m_scrollbar_width, ((m_itemheight + m_margin.y()) * m_rows) - m_margin.y()));

			}
			m_scrollbar->show();
			scrollbarvisible = true;
		}
		else
		{
			if (m_layout_mode == LayoutVertical)
			{
				m_content->setSize(eSize(width, m_itemheight));
				m_content->setSelectionSize(eSize(width, m_itemheight));
			}
			m_scrollbar->hide();
			scrollbarvisible = false;
		}
	}
	
	// Don't set Start/End if scollbar not visible or entries/m_items_per_page = 0
	if (m_items_per_page && entries && scrollbarvisible)
	{

		if(m_scroll_mode == byLine && m_layout_mode != LayoutGrid) {

			if(m_prev_scrollbar_page != m_selected) {
				m_prev_scrollbar_page = m_selected;

				int start = 0;
				int range = (m_layout_mode == LayoutVertical) ? size().height() - (m_scrollbar_border_width * 2) : ((m_itemwidth + m_margin.x()) * m_columns) - m_margin.x();
				int end = range;
				// calculate thumb only if needed
				if (entries > 1 && entries > m_items_per_page)
				{
					float fthumb = (float)m_items_per_page / (float)entries * range;
					float fsteps = ((float)(range - fthumb) / (float)entries);
					float fstart = (float)m_selected * fsteps;
					fthumb = (float)range - (fsteps * (float)(entries - 1));
					int visblethumb = fthumb < 4 ? 4 : (int)(fthumb + 0.5);
					start = (int)(fstart + 0.5);
					end = start + visblethumb;
					if (end > range) {
						end = range;
						start = range - visblethumb;
					}
					//eDebug("[eListbox] updateScrollBar thumb=%d steps=%d start=%d end=%d range=%d m_items_per_page=%d entries=%d m_selected=%d", thumb, steps, start, end, range, m_items_per_page, entries, m_selected);
				}

				m_scrollbar->setStartEnd(start, end, true);

			} 
			return;
		}

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

		const gRegion &paint_region = *(gRegion *)data;
		gRegion entryrect;
		gRGB last_col;
		gRGB def_col = m_style.m_background_color;
		int line = 0;

		if (m_layout_mode != LayoutVertical && !isTransparent())
		{
			painter.clip(paint_region);
			if (m_style.m_background_color_global_set)
				painter.setBackgroundColor(m_style.m_background_color_global);
			else if (m_style.m_background_color_set)
				painter.setBackgroundColor(m_style.m_background_color);
			painter.clear();
			painter.clippop();
		}

		// Include 2 extra items if animating horizontally

		int extra_items = (m_layout_mode == LayoutHorizontal) ? 2 : 0;
		//int extra_items = (m_layout_mode == LayoutHorizontal && m_animating) ? 4 : 2;
		int total_items_to_draw = m_items_per_page + extra_items;

		for (int posx = 0, posy = 0, i = 0;
			(m_layout_mode == LayoutVertical) ? i <= m_items_per_page : i < total_items_to_draw;
			posx += m_itemwidth + m_margin.x(), ++i)
		{
			if (!m_content->cursorValid())
				break;

			if (m_style.m_background_color_set && m_style.m_background_color_rows_set)
			{
				if (m_style.m_background_col_current == def_col || i == 0)
				{
					m_style.m_background_color = def_col;
					m_style.m_background_col_current = m_style.m_background_color_rows;
				}
				else if (m_style.m_background_col_current == m_style.m_background_color_rows &&
						 last_col != m_style.m_background_color_rows &&
						 m_content->cursorValid() && i > 0)
				{
					m_style.m_background_color = m_style.m_background_color_rows;
					m_style.m_background_col_current = def_col;
				}
				else
				{
					m_style.m_background_color = def_col;
					m_style.m_background_col_current = m_style.m_background_color_rows;
				}
				last_col = m_style.m_background_color;
			}

			if (m_layout_mode == LayoutGrid && i > 0 && i % m_columns == 0)
			{
				posy += m_itemheight + m_margin.y();
				posx = 0;
			}
			if (m_layout_mode == LayoutVertical)
			{
				posx = 0;
				if (i > 0)
					posy += m_itemheight + m_margin.y();
			}

			bool sel = (m_selected == m_content->cursorGet() && m_content->size() && m_selection_enabled);
			if (sel)
				line = i;

			// Apply horizontal animation offset
			int x_anim_shift = 0;
			if (m_layout_mode == LayoutHorizontal && m_animating)
				x_anim_shift = -m_animation_direction * m_animation_offset;

			int draw_x = posx + xoffset + x_anim_shift;
			int draw_y = posy + yoffset;

			entryrect = eRect(draw_x, draw_y, m_selectionwidth, m_selectionheight);
			gRegion entry_clip_rect = paint_region & entryrect;

			if (!entry_clip_rect.empty())
			{
				if (m_layout_mode != LayoutVertical && m_content->cursorValid())
				{
					if (i != (m_selected - m_top) || !m_selection_enabled)
						m_content->paint(painter, *style, ePoint(draw_x, draw_y), 0);
				}
				else if (m_layout_mode == LayoutVertical)
					m_content->paint(painter, *style, ePoint(draw_x, draw_y), sel);
			}

			m_content->cursorMove(+1);
		}

		m_content->cursorSaveLine(line);
		m_content->cursorRestore();

		// Draw selected item again (on top)
		if (m_selected == m_content->cursorGet() && m_content->size() && m_selection_enabled && m_layout_mode != LayoutVertical)
		{
			ePoint margin = (m_selected > 0) ? m_margin : ePoint(0, 0);
			int posx_sel = (m_layout_mode == LayoutGrid)
				? (m_itemwidth + margin.x()) * ((m_selected - m_top) % m_columns)
				: (m_itemwidth + margin.x()) * (m_selected - m_top);
			int posy_sel = (m_layout_mode == LayoutGrid)
				? (m_itemheight + margin.y()) * ((m_selected - m_top) / m_columns)
				: 0;

			if (m_layout_mode == LayoutVertical)
			{
				posx_sel = 0;
				posy_sel = (m_itemheight + margin.y()) * (m_selected - m_top);
			}

			int sel_x = posx_sel + xoffset;
			int sel_y = posy_sel + yoffset;

			if (m_layout_mode == LayoutHorizontal && m_animating)
				sel_x += -m_animation_direction * m_animation_offset;

			if (m_style.m_shadow_set && m_style.m_shadow)
			{
				int shadow_x = sel_x - (((m_selectionwidth + 40) - m_selectionwidth) / 2);
				int shadow_y = sel_y - (((m_selectionheight + 40) - m_selectionheight) / 2);
				eRect rect(shadow_x, shadow_y, m_selectionwidth + 40, m_selectionheight + 40);

				painter.clip(rect);
				painter.blitScale(m_style.m_shadow, rect, rect, gPainter::BT_ALPHABLEND);
				painter.clippop();
			}

			m_content->paint(painter, *style, ePoint(sel_x, sel_y), 1);
		}

		// Clear empty space near vertical scrollbar
		if (m_scrollbar && m_scrollbar->isVisible() && !isTransparent() && m_layout_mode == LayoutVertical)
		{
			style->setStyle(painter, eWindowStyle::styleListboxNormal);
			painter.clip(eRect(m_scrollbar->position() - ePoint(m_scrollbar_offset, 0),
							   eSize(m_scrollbar_offset, m_scrollbar->size().height())));
			if (m_style.m_background_color_set)
				painter.setBackgroundColor(m_style.m_background_color);
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
	m_content_changed = true;
	m_prev_scrollbar_page = -1;
	if (m_content)
	{
		if (size().width() > -1 && size().height() > -1)
		{
			// if (m_itemheight_set && m_itemwidth_set && m_selectionheight_set && m_selectionwidth_set)
			// {
			// 	if (m_selectionwidth < m_itemwidth)
			// 		m_selectionwidth = m_itemwidth;
			// 	if (m_selectionheight < m_itemheight)
			// 		m_selectionheight = m_itemheight;
			// }
			if (!m_itemheight_set && !m_itemwidth_set && m_selectionheight_set && m_selectionwidth_set)
			{
				m_itemheight = m_selectionheight;
				m_itemwidth = m_selectionwidth;
			}
			if (m_itemheight_set && m_itemwidth_set && !m_selectionheight_set && !m_selectionwidth_set)
			{
				m_selectionwidth = m_itemwidth;
				m_selectionheight = m_itemheight;
			}

			int diffx = (m_selectionwidth > m_itemwidth) ? (m_selectionwidth - m_itemwidth) : 0;
			int diffy = (m_selectionheight > m_itemheight) ? (m_selectionheight - m_itemheight) : 0;

			if (m_layout_mode == LayoutGrid || m_layout_mode == LayoutHorizontal)
			{
				m_columns = (!m_columns_set || (m_columns > size().width() / (m_itemwidth + m_margin.x()))) ? size().width() / (m_itemwidth + m_margin.x()) : m_columns;
				if (m_layout_mode == LayoutGrid)
				{
					m_rows = (!m_rows_set || (m_rows > size().height() / (m_itemheight + m_margin.y()))) ? size().height() / (m_itemheight + m_margin.y()) : m_rows;
					if (m_rows < 2)
						m_layout_mode = LayoutHorizontal;
				}
				if (m_center_list)
				{
					if ((((size().width() - ((m_itemwidth * m_columns) + (m_margin.x() * (m_columns - 1)))) - diffx) / 2) > 0)
						xoffset = (((size().width() - ((m_itemwidth * m_columns) + (m_margin.x() * (m_columns - 1)))) - diffx) / 2);
					if ((((size().height() - ((m_itemheight * m_rows) + (m_margin.y() * (m_rows - 1)))) - diffy) / 2) > 0 && m_layout_mode == LayoutGrid)
						yoffset = (((size().height() - ((m_itemheight * m_rows) + (m_margin.y() * (m_rows - 1)))) - diffy) / 2);
					if ((((size().height() - m_itemheight) - diffy) / 2) > 0 && m_layout_mode == LayoutHorizontal)
						yoffset = (((size().height() - m_itemheight) - diffy) / 2);
				}
			}

			if (m_layout_mode == LayoutVertical)
			{
				m_itemwidth = m_selectionwidth = size().width();
				m_selectionheight = m_itemheight;
				m_columns = size().height() / m_itemheight;
			}

			m_items_per_page = (m_layout_mode == LayoutGrid) ? m_columns * m_rows : m_columns;
			if (m_items_per_page < 0) /* TODO: whyever - our size could be invalid, or itemheigh could be wrongly specified. */
				m_items_per_page = 0;

			if (m_scrollbar_mode == showAlways || (m_content->size() > m_items_per_page && m_scrollbar_mode == showOnDemand))
			{
				if (m_layout_mode == LayoutGrid)
				{
					if ((xoffset - m_scrollbar_width) > 0 && m_center_list)
						xoffset -= m_scrollbar_width;
				}
				if (m_layout_mode == LayoutHorizontal)
				{
					if ((yoffset - m_scrollbar_width) > 0 && m_center_list)
						yoffset -= m_scrollbar_width;
				}
			}
		}

		m_content->setSize(eSize(m_itemwidth, m_itemheight));
		m_content->setSelectionSize(eSize(m_selectionwidth, m_selectionheight));
	}

	moveSelection(justCheck);
}

void eListbox::setItemHeight(int h)
{
	if (h && m_itemheight != h)
	{
		m_itemheight = h;
		m_itemheight_set = true;
		recalcSize();
		invalidate();
	}
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

void eListbox::setShadow(int shadow)
{
	if (shadow == 1 && m_layout_mode != LayoutVertical && !m_style.m_shadow_set)
	{
		if (fileExists("/usr/share/enigma2/skin_default/shadow.png"))
		{
			loadPNG(m_style.m_shadow, "/usr/share/enigma2/skin_default/shadow.png", 1);
			m_style.m_shadow_set = 1;
			// recalcSize();
		}
	}
}

void eListbox::setOverlay(int overlay)
{
	if (overlay == 1 && !m_style.m_overlay_set)
	{
		if (fileExists("/usr/share/enigma2/skin_default/overlay.png"))
		{
			loadPNG(m_style.m_overlay, "/usr/share/enigma2/skin_default/overlay.png", 1);
			m_style.m_overlay_set = 1;
		}
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
		ePoint margin = (index > 0) ? m_margin : ePoint(0, 0);
		if (m_layout_mode == LayoutHorizontal || m_layout_mode == LayoutGrid)
		{
			int posx_sel = (m_layout_mode == LayoutGrid) ? (m_itemwidth + margin.x()) * ((index - m_top) % m_columns) : (m_itemwidth + margin.x()) * (index - m_top);
			int posy_sel = (m_layout_mode == LayoutGrid) ? (m_itemheight + margin.y()) * ((index - m_top) / m_columns) : 0;
			gRegion inv = eRect(posx_sel + xoffset, posy_sel + yoffset, m_selectionwidth, m_selectionheight);
			invalidate(inv);
		}
		else
		{
			gRegion inv = eRect(xoffset, ((m_itemheight + margin.y()) * (index - m_top)) + yoffset, m_selectionwidth, m_selectionheight);
			invalidate(inv);
		}
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

void eListbox::setBackgroundColorGradient(gRGB &start, gRGB &end, int direction)
{
	m_style.m_background_color_gradient_start = start;
	m_style.m_background_color_gradient_stop = end;
	m_style.m_background_color_gradient_direction = direction;
	m_style.m_background_color_gradient_set = 1;
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

void eListbox::setBackgroundColorGradientSelected(gRGB &start, gRGB &end, int direction)
{
	m_style.m_background_color_gradient_selected_start = start;
	m_style.m_background_color_gradient_selected_stop = end;
	m_style.m_background_color_gradient_selected_direction = direction;
	m_style.m_background_color_gradient_selected_set = 1;
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
