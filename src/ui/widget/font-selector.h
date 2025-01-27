// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 *
 * The routines here create and manage a font selector widget with three parts,
 * one each for font-family, font-style, and font-size.
 *
 * It is used by the TextEdit  and Glyphs panel dialogs. The FontLister class is used
 * to access the list of font-families and their associated styles for fonts either
 * on the system or in the document. The FontLister class is also used by the Text
 * toolbar. Fonts are kept track of by their "fontspecs"  which are the same as the
 * strings that Pango generates.
 *
 * The main functions are:
 *   Create the font-seletor widget.
 *   Update the lists when a new text selection is made.
 *   Update the Style list when a new font-family is selected, highlighting the
 *     best match to the original font style (as not all fonts have the same style options).
 *   Emit a signal when any change is made so that the Text Preview can be updated.
 *   Provide the currently selected values.
 */
/*
 * Author:
 *   Tavmjong Bah <tavmjong@free.fr>
 *
 * Copyright (C) 2018 Tavmong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_FONT_SELECTOR_H
#define INKSCAPE_UI_WIDGET_FONT_SELECTOR_H

#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/frame.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treeview.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include "ui/widget/font-variations.h"

namespace Gdk {
class Drag;
} // namespace Gdk

namespace Gtk {
class DragSource;
} // namespace Gtk

namespace Inkscape::UI::Widget {

/**
 * A container of widgets for selecting font faces.
 *
 * It is used by the TextEdit and Glyphs panel dialogs. The FontSelector class utilizes the
 * FontLister class to obtain a list of font-families and their associated styles for fonts either
 * on the system or in the document. The FontLister class is also used by the Text toolbar. Fonts
 * are kept track of by their "fontspecs" which are the same as the strings that Pango generates.
 *
 * The main functions are:
 *   Create the font-selector widget.
 *   Update the child widgets when a new text selection is made.
 *   Update the Style list when a new font-family is selected, highlighting the
 *     best match to the original font style (as not all fonts have the same style options).
 *   Emit a signal when any change is made to a child widget.
 */
class FontSelector : public Gtk::Box
{
public:

    /**
     * Constructor
     */
    FontSelector (bool with_size = true, bool with_variations = true);
    void hide_others();

protected:

    // Font family
    Gtk::Frame          family_frame;
    Gtk::ScrolledWindow family_scroll;
    Gtk::TreeView       family_treeview;
    Gtk::TreeViewColumn family_treecolumn;
    Gtk::CellRendererText family_cell;

    // Font style
    Gtk::Frame          style_frame;
    Gtk::ScrolledWindow style_scroll;
    Gtk::TreeView       style_treeview;
    Gtk::TreeViewColumn style_treecolumn;
    Gtk::CellRendererText style_cell;

    // Font size
    Gtk::Label          size_label;
    Gtk::ComboBoxText   size_combobox;

    // Font variations
    Gtk::ScrolledWindow font_variations_scroll;
    FontVariations      font_variations;

private:
    // Set sizes in font size combobox.
    void set_sizes();
    void set_fontsize_tooltip();

    // Use font style when listing style names.
    void style_cell_data_func(Gtk::CellRenderer *renderer,
                              Gtk::TreeModel::const_iterator const &iter);

    // Signal handlers
    void on_family_changed();
    void on_style_changed();
    void on_size_changed();
    void on_variations_changed();

    // Signals
    sigc::signal<void (Glib::ustring)> signal_changed;
    void changed_emit();
    bool signal_block;

    // Variables
    double font_size;

    bool initial = true;

    // control font variations update and UI element size
    void update_variations(const Glib::ustring& fontspec);

    static gboolean set_cell_markup(gpointer);
    void on_realize_list();
    // For drag and drop.
    Glib::RefPtr<Gdk::ContentProvider> on_drag_prepare(Gtk::DragSource const &source,
                                                       double x, double y);
    void on_drag_begin(Gtk::DragSource &source,
                       Glib::RefPtr<Gdk::Drag> const &drag);

public:
    /**
     * Update GUI based on fontspec
     */
    void update_font ();
    void update_size (double size);
    void unset_model();
    void set_model();

    /**
     * Get fontspec based on current settings. (Does not handle size, yet.)
     */
    Glib::ustring get_fontspec(bool use_variations = true);

    /**
     * Get font size. Could be merged with fontspec.
     */
    double get_fontsize() { return font_size; };

    /**
     * Let others know that user has changed GUI settings.
     * (Used to enable 'Apply' and 'Default' buttons.)
     */
    sigc::connection connectChanged(sigc::slot<void (Glib::ustring)> slot) {
        return signal_changed.connect(slot);
    }
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_FONT_SETTINGS_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
