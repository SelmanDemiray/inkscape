/**
 * @file
 * Zoom aux toolbar
 */
/* Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Frank Felfe <innerspace@iname.com>
 *   John Cliff <simarilius@yahoo.com>
 *   David Turner <novalis@gnu.org>
 *   Josh Andler <scislac@scislac.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Tavmjong Bah <tavmjong@free.fr>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2003 MenTaLguY
 * Copyright (C) 1999-2011 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "ui/widget/spinbutton.h"
#include "toolbox.h"
#include "zoom-toolbar.h"

#include "../desktop.h"
#include "../desktop-handles.h"
#include "document-undo.h"
#include "../verbs.h"
#include "../inkscape.h"
#include "../selection-chemistry.h"
#include "../selection.h"
#include "../ege-adjustment-action.h"
#include "../ege-output-action.h"
#include "../ege-select-one-action.h"
#include "../ink-action.h"
#include "../ink-comboboxentry-action.h"
#include "../widgets/button.h"
#include "../widgets/spinbutton-events.h"
#include "../widgets/spw-utilities.h"
#include "../widgets/widget-sizes.h"
#include "../xml/repr.h"
#include "ui/uxmanager.h"
#include "../ui/icon-names.h"
#include "../helper/unit-menu.h"
#include "../helper/units.h"
#include "../helper/unit-tracker.h"
#include "../pen-context.h"
#include "../tweak-context.h"


using Inkscape::UnitTracker;
using Inkscape::UI::UXManager;
using Inkscape::DocumentUndo;
using Inkscape::UI::ToolboxFactory;
using Inkscape::UI::PrefPusher;

//########################
//##    Zoom Toolbox    ##
//########################

void sp_zoom_toolbox_prep(SPDesktop * /*desktop*/, GtkActionGroup* /*mainActions*/, GObject* /*holder*/)
{
    // no custom GtkAction setup needed
} // end of sp_zoom_toolbox_prep()

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
