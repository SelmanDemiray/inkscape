// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_GRADIENT_SELECTOR_H
#define SEEN_GRADIENT_SELECTOR_H

/*
 * Gradient vector and position widget
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2001-2002 Lauris Kaplinski
 * Copyright (C) 2001 Ximian, Inc.
 * Copyright (C) 2010 Jon A. Cruz
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <gtkmm/box.h>
#include <gtkmm/liststore.h>
#include <gtkmm/scrolledwindow.h>

#include <vector>
#include "object/sp-gradient-spread.h"
#include "object/sp-gradient-units.h"

class SPDocument;
class SPGradient;
struct SPGradientVectorSelector;

namespace Gtk {
class Button;
class CellRendererPixbuf;
class CellRendererText;
class ScrolledWindow;
class TreeView;
}

class GradientSelector : public Gtk::Box {
public:
    enum SelectorMode {
        MODE_LINEAR,
        MODE_RADIAL,
        MODE_SWATCH
    };

    class ModelColumns : public Gtk::TreeModel::ColumnRecord
    {
      public:
        ModelColumns()
        {
            add(name);
            add(refcount);
            add(color);
            add(data);
            add(pixbuf);
        }
        ~ModelColumns() override = default;

        Gtk::TreeModelColumn<Glib::ustring> name;
        Gtk::TreeModelColumn<unsigned long> color;
        Gtk::TreeModelColumn<gint> refcount;
        Gtk::TreeModelColumn<SPGradient*> data;
        Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf> > pixbuf;

    };


private:
    sigc::signal<void> _signal_grabbed;
    sigc::signal<void> _signal_dragged;
    sigc::signal<void> _signal_released;
    sigc::signal<void, SPGradient *> _signal_changed;
    SelectorMode _mode;

    SPGradientUnits _gradientUnits;
    SPGradientSpread _gradientSpread;

    /* Vector selector */
    GtkWidget *_vectors;

    /* Tree */
    bool _checkForSelected(const Gtk::TreePath& path, const Gtk::TreeIter& iter, SPGradient *vector);
    bool onKeyPressEvent(GdkEventKey *event);
    void onTreeSelection();
    void onGradientRename( const Glib::ustring& path_string, const Glib::ustring& new_text);
    void onTreeNameColClick();
    void onTreeColorColClick();
    void onTreeCountColClick();

    Gtk::TreeView     *_treeview;
    Gtk::ScrolledWindow *_scrolled_window;
    ModelColumns *_columns;
    Glib::RefPtr<Gtk::ListStore> _store;
    Gtk::CellRendererPixbuf* _icon_renderer;
    Gtk::CellRendererText* _text_renderer;

    /* Editing buttons */
    Gtk::Button *_edit;
    Gtk::Button *_add;
    Gtk::Button *_del;
    GtkWidget *merge;

    /* Position widget */
    GtkWidget *position;

    bool _safelyInit;
    bool _blocked;

    std::vector<Gtk::Widget*> _nonsolid;
    std::vector<Gtk::Widget*> _swatch_widgets;

    void selectGradientInTree(SPGradient *vector);
    void moveSelection(int amount, bool down = true, bool toEnd = false);

    void style_button(Gtk::Button *btn, char const *iconName);

    // Signal handlers
    void add_vector_clicked();
    void edit_vector_clicked();
    void delete_vector_clicked();
    void vector_set(SPGradientVectorSelector *gvs, SPGradient *gr);

public:
    GradientSelector();
    ~GradientSelector();

    inline decltype(_signal_changed)  signal_changed()  const {return _signal_changed;}
    inline decltype(_signal_grabbed)  signal_grabbed()  const {return _signal_grabbed;}
    inline decltype(_signal_dragged)  signal_dragged()  const {return _signal_dragged;}
    inline decltype(_signal_released) signal_released() const {return _signal_released;}

    SPGradient *getVector();
    void setVector(SPDocument *doc, SPGradient *vector);
    void setMode(SelectorMode mode);
    void setUnits(SPGradientUnits units);
    SPGradientUnits getUnits();
    void setSpread(SPGradientSpread spread);
    SPGradientSpread getSpread();
};

#endif // SEEN_GRADIENT_SELECTOR_H


/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
