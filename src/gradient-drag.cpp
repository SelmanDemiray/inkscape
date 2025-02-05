// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * On-canvas gradient dragging
 *
 * Authors:
 *   bulia byak <buliabyak@users.sf.net>
 *   Johan Engelen <j.b.c.engelen@ewi.utwente.nl>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   Tavmjong Bah <tavmjong@free.fr>
 *
 * Copyright (C) 2007 Johan Engelen
 * Copyright (C) 2005,2010 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstring>
#include <string>

#include <glibmm/i18n.h>

#include "desktop-style.h"
#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "gradient-chemistry.h"
#include "gradient-drag.h"
#include "selection.h"
#include "snap.h"

#include "display/control/canvas-item-group.h"
#include "display/control/canvas-item-ctrl.h"
#include "display/control/canvas-item-curve.h"

#include "object/sp-linear-gradient.h"
#include "object/sp-mesh-gradient.h"
#include "object/sp-namedview.h"
#include "object/sp-radial-gradient.h"
#include "object/sp-stop.h"
#include "style.h"

#include "svg/css-ostringstream.h"
#include "svg/svg.h"

#include "ui/icon-names.h"
#include "ui/knot/knot.h"
#include "ui/tools/tool-base.h"
#include "ui/widget/events/canvas-event.h"

#include "xml/sp-css-attr.h"

using Inkscape::DocumentUndo;
using Inkscape::allPaintTargets;
// absolute distance between gradient points for them to become a single dragger when the drag is created:
#define MERGE_DIST 0.1

// knot shapes corresponding to GrPointType enum (in sp-gradient.h)
std::unordered_map<GrPointType, Inkscape::CanvasItemCtrlType> const gr_knot_types = {
    {POINT_LG_BEGIN, Inkscape::CANVAS_ITEM_CTRL_TYPE_SIZER},
    {POINT_LG_END, Inkscape::CANVAS_ITEM_CTRL_TYPE_ROTATE},
    {POINT_LG_MID, Inkscape::CANVAS_ITEM_CTRL_TYPE_SHAPER},
    {POINT_RG_CENTER, Inkscape::CANVAS_ITEM_CTRL_TYPE_SIZER},
    {POINT_RG_R1, Inkscape::CANVAS_ITEM_CTRL_TYPE_ROTATE},
    {POINT_RG_R2, Inkscape::CANVAS_ITEM_CTRL_TYPE_ROTATE},
    {POINT_RG_FOCUS, Inkscape::CANVAS_ITEM_CTRL_TYPE_MARKER},
    {POINT_RG_MID1, Inkscape::CANVAS_ITEM_CTRL_TYPE_SHAPER},
    {POINT_RG_MID2, Inkscape::CANVAS_ITEM_CTRL_TYPE_SHAPER},
    {POINT_MG_CORNER, Inkscape::CANVAS_ITEM_CTRL_TYPE_SHAPER},
    {POINT_MG_HANDLE, Inkscape::CANVAS_ITEM_CTRL_TYPE_MESH},
    {POINT_MG_TENSOR, Inkscape::CANVAS_ITEM_CTRL_TYPE_SIZER}
};

std::unordered_map<GrPointType, char const *> const gr_knot_descr = {
    {POINT_LG_BEGIN, N_("Linear gradient <b>start</b>")},
    {POINT_LG_END, N_("Linear gradient <b>end</b>")},
    {POINT_LG_MID, N_("Linear gradient <b>mid stop</b>")},
    {POINT_RG_CENTER, N_("Radial gradient <b>center</b>")},
    {POINT_RG_R1, N_("Radial gradient <b>radius</b>")},
    {POINT_RG_R2, N_("Radial gradient <b>radius</b>")},
    {POINT_RG_FOCUS, N_("Radial gradient <b>focus</b>")},
    {POINT_RG_MID1, N_("Radial gradient <b>mid stop</b>")},
    {POINT_RG_MID2, N_("Radial gradient <b>mid stop</b>")},
    {POINT_MG_CORNER, N_("Mesh gradient <b>corner</b>")},
    {POINT_MG_HANDLE, N_("Mesh gradient <b>handle</b>")},
    {POINT_MG_TENSOR, N_("Mesh gradient <b>tensor</b>")}
};

static void
gr_drag_sel_changed(Inkscape::Selection */*selection*/, gpointer data)
{
    GrDrag *drag = (GrDrag *) data;
    drag->updateDraggers ();
    drag->updateLines ();
    drag->updateLevels ();
}

static void gr_drag_sel_modified(Inkscape::Selection */*selection*/, guint /*flags*/, gpointer data)
{
    GrDrag *drag = (GrDrag *) data;
    if (drag->local_change) {
        drag->refreshDraggers ();  // Needed to move mesh handles and toggle visibility
        drag->local_change = false;
    } else {
        drag->updateDraggers ();
    }
    drag->updateLines ();
    drag->updateLevels ();
}

/**
 * When a _query_style_signal is received, check that \a property requests fill/stroke/opacity (otherwise
 * skip), and fill the \a style with the averaged color of all draggables of the selected dragger, if
 * any.
 */
static int gr_drag_style_query(SPStyle *style, int property, gpointer data)
{
    GrDrag *drag = (GrDrag *) data;

    if (property != QUERY_STYLE_PROPERTY_FILL && property != QUERY_STYLE_PROPERTY_STROKE && property != QUERY_STYLE_PROPERTY_MASTEROPACITY) {
        return QUERY_STYLE_NOTHING;
    }

    if (drag->selected.empty()) {
        return QUERY_STYLE_NOTHING;
    } else {
        int ret = QUERY_STYLE_NOTHING;

        float cf[4];
        cf[0] = cf[1] = cf[2] = cf[3] = 0;

        SPStop* selected = nullptr;
        int count = 0;
        for(auto d : drag->selected) { //for all selected draggers
            for(auto draggable : d->draggables) { //for all draggables of dragger
                if (ret == QUERY_STYLE_NOTHING) {
                    ret = QUERY_STYLE_SINGLE;
                    selected = sp_item_gradient_get_stop(draggable->item, draggable->point_type, draggable->point_i, draggable->fill_or_stroke);
                } else if (ret == QUERY_STYLE_SINGLE) {
                    ret = QUERY_STYLE_MULTIPLE_AVERAGED;
                }

                guint32 c = sp_item_gradient_stop_query_style (draggable->item, draggable->point_type, draggable->point_i, draggable->fill_or_stroke);
                cf[0] += SP_RGBA32_R_F (c);
                cf[1] += SP_RGBA32_G_F (c);
                cf[2] += SP_RGBA32_B_F (c);
                cf[3] += SP_RGBA32_A_F (c);

                count ++;
            }
        }

        if (count) {
            cf[0] /= count;
            cf[1] /= count;
            cf[2] /= count;
            cf[3] /= count;

            // set both fill and stroke with our stop-color and stop-opacity
            style->fill.clear();
            style->fill.setColor( cf[0], cf[1], cf[2] );
            style->fill.set = TRUE;
            style->fill.setTag(selected);
            style->stroke.clear();
            style->stroke.setColor( cf[0], cf[1], cf[2] );
            style->stroke.set = TRUE;
            style->stroke.setTag(selected);

            style->fill_opacity.value = SP_SCALE24_FROM_FLOAT (cf[3]);
            style->fill_opacity.set = TRUE;
            style->stroke_opacity.value = SP_SCALE24_FROM_FLOAT (cf[3]);
            style->stroke_opacity.set = TRUE;

            style->opacity.value = SP_SCALE24_FROM_FLOAT (cf[3]);
            style->opacity.set = TRUE;
        }

        return ret;
    }
}

Glib::ustring GrDrag::makeStopSafeColor( gchar const *str, bool &isNull )
{
    Glib::ustring colorStr;
    if ( str ) {
        isNull = false;
        colorStr = str;
        Glib::ustring::size_type pos = colorStr.find("url(#");
        if ( pos != Glib::ustring::npos ) {
            Glib::ustring targetName = colorStr.substr(pos + 5, colorStr.length() - 6);
            std::vector<SPObject *> gradients = desktop->doc()->getResourceList("gradient");
            for (auto gradient : gradients) {
                auto grad = cast<SPGradient>(gradient);
                if ( targetName == grad->getId() ) {
                    SPGradient *vect = grad->getVector();
                    SPStop *firstStop = (vect) ? vect->getFirstStop() : grad->getFirstStop();
                    if (firstStop) {
                        Glib::ustring stopColorStr = firstStop->getColor().toString();
                        if ( !stopColorStr.empty() ) {
                            colorStr = stopColorStr;
                        }
                    }
                    break;
                }
            }
        }
    } else {
        isNull = true;
    }

    return colorStr;
}

bool GrDrag::styleSet( const SPCSSAttr *css, bool switch_style)
{
    if (selected.empty()) {
        return false;
    }

    SPCSSAttr *stop = sp_repr_css_attr_new();

    // See if the css contains interesting properties, and if so, translate them into the format
    // acceptable for gradient stops

    // any of color properties, in order of increasing priority:
    if (css->attribute("flood-color")) {
        sp_repr_css_set_property (stop, "stop-color", css->attribute("flood-color"));
    }

    if (css->attribute("lighting-color")) {
        sp_repr_css_set_property (stop, "stop-color", css->attribute("lighting-color"));
    }

    if (css->attribute("color")) {
        sp_repr_css_set_property (stop, "stop-color", css->attribute("color"));
    }

    if (css->attribute("stroke") && strcmp(css->attribute("stroke"), "none")) {
        sp_repr_css_set_property (stop, "stop-color", css->attribute("stroke"));
    }

    if (css->attribute("fill") && strcmp(css->attribute("fill"), "none")) {
        sp_repr_css_set_property (stop, "stop-color", css->attribute("fill"));
    }

    if (css->attribute("stop-color")) {
        sp_repr_css_set_property (stop, "stop-color", css->attribute("stop-color"));
    }

    // Make sure the style is allowed for gradient stops.
    if ( !sp_repr_css_property_is_unset( stop, "stop-color") ) {
        bool stopIsNull = false;
        Glib::ustring tmp = makeStopSafeColor( sp_repr_css_property( stop, "stop-color", "" ), stopIsNull );
        if ( !stopIsNull && !tmp.empty() ) {
            sp_repr_css_set_property( stop, "stop-color", tmp.c_str() );
        }
    }


    if (css->attribute("stop-opacity")) { // direct setting of stop-opacity has priority
        sp_repr_css_set_property(stop, "stop-opacity", css->attribute("stop-opacity"));
    } else {  // multiply all opacity properties:
        gdouble accumulated = 1.0;
        accumulated *= sp_svg_read_percentage(css->attribute("flood-opacity"), 1.0);
        accumulated *= sp_svg_read_percentage(css->attribute("opacity"), 1.0);
        accumulated *= sp_svg_read_percentage(css->attribute("stroke-opacity"), 1.0);
        accumulated *= sp_svg_read_percentage(css->attribute("fill-opacity"), 1.0);

        Inkscape::CSSOStringStream os;
        os << accumulated;
        sp_repr_css_set_property(stop, "stop-opacity", os.str().c_str());

        if ((css->attribute("fill") && !css->attribute("stroke") && !strcmp(css->attribute("fill"), "none")) ||
            (css->attribute("stroke") && !css->attribute("fill") && !strcmp(css->attribute("stroke"), "none"))) {
            sp_repr_css_set_property(stop, "stop-opacity", "0"); // if a single fill/stroke property is set to none, don't change color, set opacity to 0
        }
    }

    const auto& al = stop->attributeList();
    if (al.empty()) { // nothing for us here, pass it on
        sp_repr_css_attr_unref(stop);
        return false;
    }

    for(auto d : selected) { //for all selected draggers
        for(auto draggable : d->draggables) { //for all draggables of dragger
            SPGradient* gradient = getGradient(draggable->item, draggable->fill_or_stroke);

            // for linear and radial gradients F&S dialog deals with stops' colors;
            // don't handle style notifications, or else it will not be possible to switch
            // object style back to solid color
            if (switch_style && gradient &&
                (is<SPLinearGradient>(gradient) || is<SPRadialGradient>(gradient))) {
                continue;
            }

            local_change = true;
            sp_item_gradient_stop_set_style(draggable->item, draggable->point_type, draggable->point_i, draggable->fill_or_stroke, stop);
        }
    }

    //sp_repr_css_print(stop);
    sp_repr_css_attr_unref(stop);
    return local_change; // true if handled
}

guint32 GrDrag::getColor()
{
    if (selected.empty()) return 0;

    float cf[4];
    cf[0] = cf[1] = cf[2] = cf[3] = 0;

    int count = 0;

    for(auto d : selected) { //for all selected draggers
        for(auto draggable : d->draggables) { //for all draggables of dragger
            guint32 c = sp_item_gradient_stop_query_style (draggable->item, draggable->point_type, draggable->point_i, draggable->fill_or_stroke);
            cf[0] += SP_RGBA32_R_F (c);
            cf[1] += SP_RGBA32_G_F (c);
            cf[2] += SP_RGBA32_B_F (c);
            cf[3] += SP_RGBA32_A_F (c);

            count ++;
        }
    }

    if (count) {
        cf[0] /= count;
        cf[1] /= count;
        cf[2] /= count;
        cf[3] /= count;
    }

    return SP_RGBA32_F_COMPOSE(cf[0], cf[1], cf[2], cf[3]);
}

// TODO refactor early returns
SPStop *GrDrag::addStopNearPoint(SPItem *item, Geom::Point mouse_p, double tolerance)
{
    gfloat new_stop_offset = 0; // type of SPStop.offset = gfloat
    SPGradient *gradient = nullptr;
    //bool r1_knot = false;

    // For Mesh
    int divide_row = -1;
    int divide_column = -1;
    double divide_coord = 0.5;

    bool addknot = false;

    for (std::vector<Inkscape::PaintTarget>::const_iterator it = allPaintTargets().begin(); (it != allPaintTargets().end()) && !addknot; ++it)
    {
        Inkscape::PaintTarget fill_or_stroke = *it;
        gradient = getGradient(item, fill_or_stroke);
        if (is<SPLinearGradient>(gradient)) {
            Geom::Point begin   = getGradientCoords(item, POINT_LG_BEGIN, 0, fill_or_stroke);
            Geom::Point end     = getGradientCoords(item, POINT_LG_END, 0, fill_or_stroke);
            Geom::LineSegment ls(begin, end);
            double offset = ls.nearestTime(mouse_p);
            Geom::Point nearest = ls.pointAt(offset);
            double dist_screen = Geom::distance(mouse_p, nearest);
            if ( dist_screen < tolerance ) {
                // calculate the new stop offset
                new_stop_offset = distance(begin, nearest) / distance(begin, end);
                // add the knot
                addknot = true;
            }
        } else if (is<SPRadialGradient>(gradient)) {
            Geom::Point begin = getGradientCoords(item, POINT_RG_CENTER, 0, fill_or_stroke);
            Geom::Point end   = getGradientCoords(item, POINT_RG_R1, 0, fill_or_stroke);
            Geom::LineSegment ls(begin, end);
            double offset = ls.nearestTime(mouse_p);
            Geom::Point nearest = ls.pointAt(offset);
            double dist_screen = Geom::distance(mouse_p, nearest);
            if ( dist_screen < tolerance ) {
                // calculate the new stop offset
                new_stop_offset = distance(begin, nearest) / distance(begin, end);
                // add the knot
                addknot = true;
                //r1_knot = true;
            } else {
                end = getGradientCoords(item, POINT_RG_R2, 0, fill_or_stroke);
                ls = Geom::LineSegment(begin, end);
                offset = ls.nearestTime(mouse_p);
                nearest = ls.pointAt(offset);
                dist_screen = Geom::distance(mouse_p, nearest);
                if ( dist_screen < tolerance ) {
                    // calculate the new stop offset
                    new_stop_offset = distance(begin, nearest) / distance(begin, end);
                    // add the knot
                    addknot = true;
                    //r1_knot = false;
                }
            }
        } else if (is<SPMeshGradient>(gradient)) {

            // add_stop_near_point()
            // Find out which curve pointer is over and use that curve to determine
            // which row or column will be divided.
            // This is silly as we already should know which line we are over...
            // but that information is not saved (sp_gradient_context_is_over_line).

            auto mg = cast<SPMeshGradient>(gradient);
            Geom::Affine transform = Geom::Affine(mg->gradientTransform)*(Geom::Affine)item->i2dt_affine();

            guint rows    = mg->array.patch_rows();
            guint columns = mg->array.patch_columns();

            double closest = 1e10;
            for( guint i = 0; i < rows; ++i ) {
                for( guint j = 0; j < columns; ++j ) {

                    SPMeshPatchI patch( &(mg->array.nodes), i, j );
                    Geom::Point p[4];

                    // Top line
                    {
                        p[0] = patch.getPoint( 0, 0 ) * transform; 
                        p[1] = patch.getPoint( 0, 1 ) * transform; 
                        p[2] = patch.getPoint( 0, 2 ) * transform; 
                        p[3] = patch.getPoint( 0, 3 ) * transform; 
                        Geom::BezierCurveN<3> b( p[0], p[1], p[2], p[3] );
                        Geom::Coord coord = b.nearestTime( mouse_p );
                        Geom::Point nearest = b( coord );
                        double dist_screen = Geom::L2 ( mouse_p - nearest );
                        if ( dist_screen < closest ) {
                            closest = dist_screen;
                            divide_row = -1;
                            divide_column = j;
                            divide_coord = coord;
                        }
                    }

                    // Right line (only for last column)
                    if( j == columns - 1 ) {
                        p[0] = patch.getPoint( 1, 0 ) * transform; 
                        p[1] = patch.getPoint( 1, 1 ) * transform; 
                        p[2] = patch.getPoint( 1, 2 ) * transform; 
                        p[3] = patch.getPoint( 1, 3 ) * transform; 
                        Geom::BezierCurveN<3> b( p[0], p[1], p[2], p[3] );
                        Geom::Coord coord = b.nearestTime( mouse_p );
                        Geom::Point nearest = b( coord );
                        double dist_screen = Geom::L2 ( mouse_p - nearest );
                        if ( dist_screen < closest ) {
                            closest = dist_screen;
                            divide_row = i;
                            divide_column = -1;
                            divide_coord = coord;
                        }
                    }

                    // Bottom line (only for last row)
                    if( i == rows - 1 ) {
                        p[0] = patch.getPoint( 2, 0 ) * transform; 
                        p[1] = patch.getPoint( 2, 1 ) * transform; 
                        p[2] = patch.getPoint( 2, 2 ) * transform; 
                        p[3] = patch.getPoint( 2, 3 ) * transform; 
                        Geom::BezierCurveN<3> b( p[0], p[1], p[2], p[3] );
                        Geom::Coord coord = b.nearestTime( mouse_p );
                        Geom::Point nearest = b( coord );
                        double dist_screen = Geom::L2 ( mouse_p - nearest );
                        if ( dist_screen < closest ) {
                            closest = dist_screen;
                            divide_row = -1;
                            divide_column = j;
                            divide_coord = 1.0 - coord;
                        }
                    }

                    // Left line
                    {
                        p[0] = patch.getPoint( 3, 0 ) * transform; 
                        p[1] = patch.getPoint( 3, 1 ) * transform; 
                        p[2] = patch.getPoint( 3, 2 ) * transform; 
                        p[3] = patch.getPoint( 3, 3 ) * transform; 
                        Geom::BezierCurveN<3> b( p[0], p[1], p[2], p[3] );
                        Geom::Coord coord = b.nearestTime( mouse_p );
                        Geom::Point nearest = b( coord );
                        double dist_screen = Geom::L2 ( mouse_p - nearest );
                        if ( dist_screen < closest ) {
                            closest = dist_screen;
                            divide_row = i;
                            divide_column = -1;
                            divide_coord = 1.0 - coord;
                        }
                    }

                } // End loop over columns
            } // End loop rows

            if( closest < tolerance ) {
                addknot = true;
            }

        } // End if mesh

    }

    if (addknot) {

        if( is<SPLinearGradient>(gradient) || is<SPRadialGradient>( gradient ) ) {
            SPGradient *vector = sp_gradient_get_forked_vector_if_necessary (gradient, false);
            SPStop* prev_stop = vector->getFirstStop();
            SPStop* next_stop = prev_stop->getNextStop();
            guint i = 1;
            while ( (next_stop) && (next_stop->offset < new_stop_offset) ) {
                prev_stop = next_stop;
                next_stop = next_stop->getNextStop();
                i++;
            }
            if (!next_stop) {
                // logical error: the endstop should have offset 1 and should always be more than this offset here
                return nullptr;
            }


            SPStop *newstop = sp_vector_add_stop (vector, prev_stop, next_stop, new_stop_offset);
            gradient->ensureVector();
            updateDraggers();

            // so that it does not automatically update draggers in idle loop, as this would deselect
            local_change = true;

            // select the newly created stop
            selectByStop(newstop);

            return newstop;

        } else {

            auto mg = cast<SPMeshGradient>(gradient);

            if( divide_row > -1 ) {
                mg->array.split_row( divide_row, divide_coord );
            } else {
                mg->array.split_column( divide_column, divide_coord );
            }

            // Update repr
            mg->array.write( mg );
            mg->array.built = false;
            mg->ensureArray();
            // How do we do this?
            DocumentUndo::done(desktop->getDocument(), _("Added patch row or column"), INKSCAPE_ICON("mesh-gradient"));

        } // Mesh
    }

    return nullptr;
}


bool GrDrag::dropColor(SPItem */*item*/, gchar const *c, Geom::Point p)
{
    // Note: not sure if a null pointer can come in for the style, but handle that just in case
    bool stopIsNull = false;
    Glib::ustring toUse = makeStopSafeColor( c, stopIsNull );

    // first, see if we can drop onto one of the existing draggers
    for(auto d : draggers) { //for all draggers
        if (Geom::L2(p - d->point)*desktop->current_zoom() < 5) {
           SPCSSAttr *stop = sp_repr_css_attr_new ();
           sp_repr_css_set_property( stop, "stop-color", stopIsNull ? nullptr : toUse.c_str() );
           sp_repr_css_set_property( stop, "stop-opacity", "1" );
           for(auto draggable : d->draggables) { //for all draggables of dragger
               local_change = true;
               sp_item_gradient_stop_set_style (draggable->item, draggable->point_type, draggable->point_i, draggable->fill_or_stroke, stop);
           }
           sp_repr_css_attr_unref(stop);
           return true;
        }
    }

    // now see if we're over line and create a new stop
    for (auto &it : item_curves) {
        if (it.curve->is_line() && it.item && it.curve->contains(p, 5)) {
            if (auto stop = addStopNearPoint(it.item, p, 5 / desktop->current_zoom())) {
                SPCSSAttr *css = sp_repr_css_attr_new();
                sp_repr_css_set_property( css, "stop-color", stopIsNull ? nullptr : toUse.c_str() );
                sp_repr_css_set_property( css, "stop-opacity", "1" );
                sp_repr_css_change(stop->getRepr(), css, "style");
                return true;
            }
        }
    }

    return false;
}

GrDrag::GrDrag(SPDesktop *desktop) :
    keep_selection(false),
    local_change(false),
    desktop(desktop),
    hor_levels(),
    vert_levels(),
    draggers(0),
    selection(desktop->getSelection()),
    sel_changed_connection(),
    sel_modified_connection(),
    style_set_connection(),
    style_query_connection()
{
    sel_changed_connection = selection->connectChangedFirst(
        sigc::bind(
            sigc::ptr_fun(&gr_drag_sel_changed),
            (gpointer)this )

        );
    sel_modified_connection = selection->connectModifiedFirst(
        sigc::bind(
            sigc::ptr_fun(&gr_drag_sel_modified),
            (gpointer)this )
        );

    style_set_connection = desktop->connectSetStyleEx( sigc::mem_fun(*this, &GrDrag::styleSet) );

    style_query_connection = desktop->connectQueryStyle(
        sigc::bind(
            sigc::ptr_fun(&gr_drag_style_query),
            (gpointer)this )
        );

    updateDraggers();
    updateLines();
    updateLevels();

    if (desktop->gr_item) {
        GrDragger *dragger = getDraggerFor(desktop->gr_item, desktop->gr_point_type, desktop->gr_point_i, desktop->gr_fill_or_stroke);
        if (dragger) {
            setSelected(dragger);
        }
    }
}

GrDrag::~GrDrag()
{
    this->sel_changed_connection.disconnect();
    this->sel_modified_connection.disconnect();
    this->style_set_connection.disconnect();
    this->style_query_connection.disconnect();

    if (! this->selected.empty()) {
        GrDraggable *draggable = (*(this->selected.begin()))->draggables[0];
        desktop->gr_item = draggable->item;
        desktop->gr_point_type = draggable->point_type;
        desktop->gr_point_i = draggable->point_i;
        desktop->gr_fill_or_stroke = draggable->fill_or_stroke;
    } else {
        desktop->gr_item = nullptr;
        desktop->gr_point_type = POINT_LG_BEGIN;
        desktop->gr_point_i = 0;
        desktop->gr_fill_or_stroke = Inkscape::FOR_FILL;
    }

    deselect_all();
    for (auto dragger : this->draggers) {
        delete dragger;
    }
    this->draggers.clear();
    this->selected.clear();

    item_curves.clear();
}

GrDraggable::GrDraggable(SPItem *item, GrPointType point_type, guint point_i, Inkscape::PaintTarget fill_or_stroke) :
    item(item),
    point_type(point_type),
    point_i(point_i),
    fill_or_stroke(fill_or_stroke)
{
	sp_object_ref(item);
}

GrDraggable::~GrDraggable()
{
    sp_object_unref(item);
}

SPObject *GrDraggable::getServer()
{
    SPObject *server = nullptr;
    if (item) {
        switch (fill_or_stroke) {
            case Inkscape::FOR_FILL:
                server = item->style->getFillPaintServer();
                break;
            case Inkscape::FOR_STROKE:
                server = item->style->getStrokePaintServer();
                break;
        }
    }

    return server;
}

static void gr_knot_moved_handler(SPKnot *knot, Geom::Point const &ppointer, guint state, gpointer data)
{
    GrDragger *dragger = (GrDragger *) data;

    // Dragger must have at least one draggable
    GrDraggable *draggable = (GrDraggable *) dragger->draggables[0];
    if (!draggable) return;

    // Find mesh corner that corresponds to dragger (only checks first draggable) and highlight it.
    GrDragger *dragger_corner = dragger->getMgCorner();
    if (dragger_corner) {
        dragger_corner->highlightCorner(true);
    }

    // Set-up snapping
    SPDesktop *desktop = dragger->parent->desktop;
    auto &m = desktop->getNamedView()->snap_manager;
    double snap_dist = m.snapprefs.getObjectTolerance() / dragger->parent->desktop->current_zoom();

    Geom::Point p = ppointer;

    if (state & GDK_SHIFT_MASK) {
        // with Shift; unsnap if we carry more than one draggable
        if (dragger->draggables.size()>1) {
            // create a new dragger
            GrDragger *dr_new = new GrDragger (dragger->parent, dragger->point, nullptr);
            dragger->parent->draggers.insert(dragger->parent->draggers.begin(), dr_new);
            // relink to it all but the first draggable in the list
            std::vector<GrDraggable *>::const_iterator i = dragger->draggables.begin();
            for ( ++i ; i != dragger->draggables.end(); ++i ) {
                GrDraggable *draggable = *i; 
                dr_new->addDraggable (draggable);
            }
            dr_new->updateKnotShape();
            if(dragger->draggables.size()>1){
                GrDraggable *tmp = dragger->draggables[0];
                dragger->draggables.clear();
                dragger->draggables.push_back(tmp);
            }
            dragger->updateKnotShape();
            dragger->updateTip();
        }
    } else if (!(state & GDK_CONTROL_MASK)) {
        // without Shift or Ctrl; see if we need to snap to another dragger
        for (std::vector<GrDragger *>::const_iterator di = dragger->parent->draggers.begin(); di != dragger->parent->draggers.end() ; ++di) {
            GrDragger *d_new = *di; 
            if (dragger->mayMerge(d_new) && Geom::L2 (d_new->point - p) < snap_dist) {

                // Merge draggers:
                for (auto draggable : dragger->draggables) {
                    // copy draggable to d_new:
                    GrDraggable *da_new = new GrDraggable (draggable->item, draggable->point_type, draggable->point_i, draggable->fill_or_stroke);
                    d_new->addDraggable (da_new);
                }

                // unlink and delete this dragger
                dragger->parent->draggers.erase(std::remove(dragger->parent->draggers.begin(),dragger->parent->draggers.end(), dragger),dragger->parent->draggers.end());
                d_new->parent->draggers.erase(std::remove(d_new->parent->draggers.begin(),d_new->parent->draggers.end(), dragger),d_new->parent->draggers.end());
                d_new->parent->selected.erase(dragger);
                delete dragger;

                // throw out delayed snap context
                desktop->getTool()->discard_delayed_snap_event();

                // update the new merged dragger
                d_new->fireDraggables(true, false, true);
                d_new->parent->updateLines();
                d_new->parent->setSelected (d_new);
                d_new->updateKnotShape ();
                d_new->updateTip ();
                d_new->updateDependencies(true);
                DocumentUndo::done(d_new->parent->desktop->getDocument(), _("Merge gradient handles"), INKSCAPE_ICON("color-gradient"));
                return;
            }
        }
    }

    if (!((state & GDK_SHIFT_MASK) || (state & GDK_CONTROL_MASK))) {
        m.setup(desktop);
        Inkscape::SnappedPoint s = m.freeSnap(Inkscape::SnapCandidatePoint(p, Inkscape::SNAPSOURCE_OTHER_HANDLE));
        m.unSetup();
        if (s.getSnapped()) {
            p = s.getPoint();
            knot->moveto(p);
        }
    } else if (state & GDK_CONTROL_MASK) {
        IntermSnapResults isr;
        Inkscape::SnapCandidatePoint scp = Inkscape::SnapCandidatePoint(p, Inkscape::SNAPSOURCE_OTHER_HANDLE);
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        unsigned snaps = abs(prefs->getInt("/options/rotationsnapsperpi/value", 12));
        /* 0 means no snapping. */

        for (std::vector<GrDraggable *>::const_iterator i = dragger->draggables.begin(); i != dragger->draggables.end(); ++i) {
            GrDraggable *draggable = *i; 

            Geom::Point dr_snap(Geom::infinity(), Geom::infinity());

            if (draggable->point_type == POINT_LG_BEGIN || draggable->point_type == POINT_LG_END) {
                for (std::vector<GrDragger *>::const_iterator di = dragger->parent->draggers.begin() ; di != dragger->parent->draggers.end() ; ++di) {
                    GrDragger *d_new = *di;
                    if (d_new == dragger)
                        continue;
                    if (d_new->isA (draggable->item,
                                    draggable->point_type == POINT_LG_BEGIN? POINT_LG_END : POINT_LG_BEGIN,
                                    draggable->fill_or_stroke)) {
                        // found the other end of the linear gradient;
                        if (state & GDK_SHIFT_MASK) {
                            // moving linear around center
                            Geom::Point center = Geom::Point (0.5*(d_new->point + dragger->point));
                            dr_snap = center;
                        } else {
                            // moving linear around the other end
                            dr_snap = d_new->point;
                        }
                    }
                }
            } else if (draggable->point_type == POINT_RG_R1 || draggable->point_type == POINT_RG_R2 || draggable->point_type == POINT_RG_FOCUS) {
                for (std::vector<GrDragger *>::const_iterator di = dragger->parent->draggers.begin(); di != dragger->parent->draggers.end(); ++di) {
                    GrDragger *d_new = *di; 
                    if (d_new == dragger)
                        continue;
                    if (d_new->isA (draggable->item,
                                    POINT_RG_CENTER,
                                    draggable->fill_or_stroke)) {
                        // found the center of the radial gradient;
                        dr_snap = d_new->point;
                    }
                }
            } else if (draggable->point_type == POINT_RG_CENTER) {
                // radial center snaps to hor/vert relative to its original position
                dr_snap = dragger->point_original;
            } else if (draggable->point_type == POINT_MG_CORNER ||
                       draggable->point_type == POINT_MG_HANDLE ||
                       draggable->point_type == POINT_MG_TENSOR ) {
                // std::cout << " gr_knot_moved_handler: Got mesh point!" << std::endl;
            }

            // dr_snap contains the origin of the gradient, whereas p will be the new endpoint which we will try to snap now
            Inkscape::SnappedPoint sp;
            if (dr_snap.isFinite()) {
                m.setup(desktop);
                if (state & GDK_ALT_MASK) {
                    // with Alt, snap to the original angle and its perpendiculars
                    sp = m.constrainedAngularSnap(scp, dragger->point_original, dr_snap, 2);
                } else {
                    // with Ctrl, snap to M_PI/snaps
                    sp = m.constrainedAngularSnap(scp, std::optional<Geom::Point>(), dr_snap, snaps);
                }
                m.unSetup();
                isr.points.push_back(sp);
            }
        }

        m.setup(desktop, false); // turn of the snap indicator temporarily
        Inkscape::SnappedPoint bsp = m.findBestSnap(scp, isr, true);
        m.unSetup();
        if (!bsp.getSnapped()) {
            // If we didn't truly snap to an object or to a grid, then we will still have to look for the
            // closest projection onto one of the constraints. findBestSnap() will not do this for us
            for (std::list<Inkscape::SnappedPoint>::const_iterator i = isr.points.begin(); i != isr.points.end(); ++i) {
                if (i == isr.points.begin() || (Geom::L2((*i).getPoint() - p) < Geom::L2(bsp.getPoint() - p))) {
                    bsp.setPoint((*i).getPoint());
                    bsp.setTarget(Inkscape::SNAPTARGET_CONSTRAINED_ANGLE);
                }
            }
        }
        //p = isr.points.front().getPoint();
        p = bsp.getPoint();
        knot->moveto(p);
    }

    GrDrag *drag = dragger->parent;  // There is just one GrDrag.
    drag->keep_selection = (drag->selected.find(dragger)!=drag->selected.end());
    bool scale_radial = (state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK);

    if (drag->keep_selection) {
        Geom::Point diff = p - dragger->point;
        drag->selected_move_nowrite (diff[Geom::X], diff[Geom::Y], scale_radial);
    } else {
        Geom::Point p_old = dragger->point;
        dragger->point = p;
        dragger->fireDraggables (false, scale_radial);
        dragger->updateDependencies(false);
        dragger->moveMeshHandles( p_old, MG_NODE_NO_SCALE );
    }
}


static void gr_midpoint_limits(GrDragger *dragger, SPObject *server, Geom::Point *begin, Geom::Point *end, Geom::Point *low_lim, Geom::Point *high_lim, std::vector<GrDragger *> &moving)
{

    GrDrag *drag = dragger->parent;
    // a midpoint dragger can (logically) only contain one GrDraggable
    GrDraggable *draggable = dragger->draggables[0];

    // get begin and end points between which dragging is allowed:
    // the draglimits are between knot(lowest_i - 1) and knot(highest_i + 1)
    moving.push_back(dragger);

    guint lowest_i = draggable->point_i;
    guint highest_i = draggable->point_i;
    GrDragger *lowest_dragger = dragger;
    GrDragger *highest_dragger = dragger;
    if (dragger->isSelected()) {
        GrDragger* d_add;
        while ( true )
        {
            d_add = drag->getDraggerFor(draggable->item, draggable->point_type, lowest_i - 1, draggable->fill_or_stroke);
            if ( d_add && drag->selected.find(d_add)!=drag->selected.end() ) {
                lowest_i = lowest_i - 1;
                moving.insert(moving.begin(),d_add);
                lowest_dragger = d_add;
            } else {
                break;
            }
        }

        while ( true )
        {
            d_add = drag->getDraggerFor(draggable->item, draggable->point_type, highest_i + 1, draggable->fill_or_stroke);
            if ( d_add && drag->selected.find(d_add)!=drag->selected.end() ) {
                highest_i = highest_i + 1;
                moving.push_back(d_add);
                highest_dragger = d_add;
            } else {
                break;
            }
        }
    }

    if ( is<SPLinearGradient>(server) ) {
        guint num = cast<SPLinearGradient>(server)->vector.stops.size();
        GrDragger *d_temp;
        if (lowest_i == 1) {
            d_temp = drag->getDraggerFor (draggable->item, POINT_LG_BEGIN, 0, draggable->fill_or_stroke);
        } else {
            d_temp = drag->getDraggerFor (draggable->item, POINT_LG_MID, lowest_i - 1, draggable->fill_or_stroke);
        }
        if (d_temp)
            *begin = d_temp->point;

        d_temp = drag->getDraggerFor (draggable->item, POINT_LG_MID, highest_i + 1, draggable->fill_or_stroke);
        if (d_temp == nullptr) {
            d_temp = drag->getDraggerFor (draggable->item, POINT_LG_END, num-1, draggable->fill_or_stroke);
        }
        if (d_temp)
            *end = d_temp->point;
    } else if ( is<SPRadialGradient>(server) ) {
        guint num = cast<SPRadialGradient>(server)->vector.stops.size();
        GrDragger *d_temp;
        if (lowest_i == 1) {
            d_temp = drag->getDraggerFor (draggable->item, POINT_RG_CENTER, 0, draggable->fill_or_stroke);
        } else {
            d_temp = drag->getDraggerFor (draggable->item, draggable->point_type, lowest_i - 1, draggable->fill_or_stroke);
        }
        if (d_temp)
            *begin = d_temp->point;

        d_temp = drag->getDraggerFor (draggable->item, draggable->point_type, highest_i + 1, draggable->fill_or_stroke);
        if (d_temp == nullptr) {
            d_temp = drag->getDraggerFor (draggable->item, (draggable->point_type==POINT_RG_MID1) ? POINT_RG_R1 : POINT_RG_R2, num-1, draggable->fill_or_stroke);
        }
        if (d_temp)
            *end = d_temp->point;
    }

    *low_lim  = dragger->point - (lowest_dragger->point - *begin);
    *high_lim = dragger->point - (highest_dragger->point - *end);
}

/**
 * Called when a midpoint knot is dragged.
 */
static void gr_knot_moved_midpoint_handler(SPKnot */*knot*/, Geom::Point const &ppointer, guint state, gpointer data)
{
    GrDragger *dragger = (GrDragger *) data;
    GrDrag *drag = dragger->parent;
    // a midpoint dragger can (logically) only contain one GrDraggable
    GrDraggable *draggable = dragger->draggables[0];

    // FIXME: take from prefs
    double snap_fraction = 0.1;

    Geom::Point p = ppointer;
    Geom::Point begin(0,0), end(0,0);
    Geom::Point low_lim(0,0), high_lim(0,0);

    SPObject *server = draggable->getServer();

    std::vector<GrDragger *> moving;
    gr_midpoint_limits(dragger, server, &begin, &end, &low_lim, &high_lim, moving);

    if (state & GDK_CONTROL_MASK) {
        Geom::LineSegment ls(low_lim, high_lim);
        p = ls.pointAt(round(ls.nearestTime(p) / snap_fraction) * snap_fraction);
    } else {
        Geom::LineSegment ls(low_lim, high_lim);
        p = ls.pointAt(ls.nearestTime(p));
        if (!(state & GDK_SHIFT_MASK)) {
            Inkscape::Snapper::SnapConstraint cl(low_lim, high_lim - low_lim);
            SPDesktop *desktop = dragger->parent->desktop;
            auto &m = desktop->getNamedView()->snap_manager;
            m.setup(desktop);
            m.constrainedSnapReturnByRef(p, Inkscape::SNAPSOURCE_OTHER_HANDLE, cl);
            m.unSetup();
        }
    }
    Geom::Point displacement = p - dragger->point;

    for (auto drg : moving) {
        SPKnot *drgknot = drg->knot;
        Geom::Point this_move = displacement;
        if (state & GDK_ALT_MASK) {
            // FIXME: unify all these profiles (here, in nodepath, in tweak) in one place
            double alpha = 1.0;
            if (Geom::L2(drg->point - dragger->point) + Geom::L2(drg->point - begin) - 1e-3 > Geom::L2(dragger->point - begin)) { // drg is on the end side from dragger
                double x = Geom::L2(drg->point - dragger->point)/Geom::L2(end - dragger->point);
                this_move = (0.5 * cos (M_PI * (pow(x, alpha))) + 0.5) * this_move;
            } else { // drg is on the begin side from dragger
                double x = Geom::L2(drg->point - dragger->point)/Geom::L2(begin - dragger->point);
                this_move = (0.5 * cos (M_PI * (pow(x, alpha))) + 0.5) * this_move;
            }
        }
        drg->point += this_move;
        drgknot->moveto(drg->point);
        drg->fireDraggables (false);
        drg->updateDependencies(false);
    }

    drag->keep_selection = dragger->isSelected();
}



static void gr_knot_mousedown_handler(SPKnot */*knot*/, unsigned int /*state*/, gpointer data)
{
    GrDragger *dragger = (GrDragger *) data;
    GrDrag *drag = dragger->parent;

    // Turn off all mesh handle highlighting
    for(auto d : drag->draggers) { //for all selected draggers
        d->highlightCorner(false);
    }

    // Highlight only mesh corner corresponding to grabbed corner or handle
    GrDragger *dragger_corner = dragger->getMgCorner();
    if (dragger_corner) {
        dragger_corner->highlightCorner(true);
    }
}

/**
 * Called when the mouse releases a dragger knot; changes gradient writing to repr, updates other draggers if needed.
 */
static void gr_knot_ungrabbed_handler(SPKnot *knot, unsigned int state, gpointer data)
{
    GrDragger *dragger = (GrDragger *) data;

    dragger->point_original = dragger->point = knot->pos;

    if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK)) {
        dragger->fireDraggables (true, true);
    } else {
        dragger->fireDraggables (true);
    }
    dragger->moveMeshHandles( dragger->point_original, MG_NODE_NO_SCALE );
    
    for (std::set<GrDragger *>::const_iterator it = dragger->parent->selected.begin(); it != dragger->parent->selected.end() ; ++it ) {
        if (*it == dragger)
            continue;
        (*it)->fireDraggables (true);
    }

    // make this dragger selected
    if (!dragger->parent->keep_selection) {
        dragger->parent->setSelected (dragger);
    }
    dragger->parent->keep_selection = false;

    dragger->updateDependencies(true);

    // we did an undoable action
    DocumentUndo::done(dragger->parent->desktop->getDocument(), _("Move gradient handle"), INKSCAPE_ICON("color-gradient"));
}

/**
 * Called when a dragger knot is clicked; selects the dragger or deletes it depending on the
 * state of the keyboard keys.
 */
static void gr_knot_clicked_handler(SPKnot */*knot*/, guint state, gpointer data)
{
    GrDragger *dragger = (GrDragger *) data;
    GrDraggable *draggable = dragger->draggables[0];
    if (!draggable) return;

    if ( (state & GDK_CONTROL_MASK) && (state & GDK_ALT_MASK ) ) {
    // delete this knot from vector
        SPGradient *gradient = getGradient(draggable->item, draggable->fill_or_stroke);
        gradient = gradient->getVector();
        if (gradient->vector.stops.size() > 2) { // 2 is the minimum
            SPStop *stop = nullptr;
            switch (draggable->point_type) {  // if we delete first or last stop, move the next/previous to the edge

                case POINT_LG_BEGIN:
                case POINT_RG_CENTER:
                    stop = gradient->getFirstStop();
                    {
                        SPStop *next = stop->getNextStop();
                        if (next) {
                            next->offset = 0;
                            next->getRepr()->setAttributeCssDouble("offset", 0);
                        }
                    }
                    break;

                case POINT_LG_END:
                case POINT_RG_R1:
                case POINT_RG_R2:
                    stop = sp_last_stop(gradient);
                    {
                        SPStop *prev = stop->getPrevStop();
                        if (prev) {
                            prev->offset = 1;
                            prev->getRepr()->setAttributeCssDouble("offset", 1);
                        }
                    }
                    break;

                case POINT_LG_MID:
                case POINT_RG_MID1:
                case POINT_RG_MID2:
                    stop = sp_get_stop_i(gradient, draggable->point_i);
                    break;

                default:
                    return;

            }

            gradient->getRepr()->removeChild(stop->getRepr());
            DocumentUndo::done(gradient->document, _("Delete gradient stop"), INKSCAPE_ICON("color-gradient"));
        }
    } else {
    // select the dragger

        dragger->point_original = dragger->point;

        if ( state & GDK_SHIFT_MASK ) {
            dragger->parent->setSelected (dragger, true, false);
        } else {
            dragger->parent->setSelected (dragger);
        }
    }
}

/**
 * Called when a dragger knot is doubleclicked;
 */
static void gr_knot_doubleclicked_handler(SPKnot */*knot*/, guint /*state*/, gpointer data)
{
    GrDragger *dragger = (GrDragger *) data;

    dragger->point_original = dragger->point;

    if (dragger->draggables.empty())
        return;
}

/**
 * Act upon all draggables of the dragger, setting them to the dragger's point.
 */
void GrDragger::fireDraggables(bool write_repr, bool scale_radial, bool merging_focus)
{
    for (auto draggable : this->draggables) {
        // set local_change flag so that selection_changed callback does not regenerate draggers
        this->parent->local_change = true;

        // change gradient, optionally writing to repr; prevent focus from moving if it's snapped
        // to the center, unless it's the first update upon merge when we must snap it to the point
        if (merging_focus ||
            !(draggable->point_type == POINT_RG_FOCUS && this->isA(draggable->item, POINT_RG_CENTER, draggable->point_i, draggable->fill_or_stroke)))
        {
            sp_item_gradient_set_coords (draggable->item, draggable->point_type, draggable->point_i, this->point, draggable->fill_or_stroke, write_repr, scale_radial);
        }
    }
}

void GrDragger::updateControlSizes()
{
    this->knot->updateCtrl();
    this->updateKnotShape();
}

/**
 * Checks if the dragger has a draggable with this point_type.
 */
bool GrDragger::isA(GrPointType point_type)
{
    for (auto draggable : this->draggables) {
        if (draggable->point_type == point_type) {
            return true;
        }
    }
    return false;
}

/**
 * Checks if the dragger has a draggable with this item, point_type + point_i (number), fill_or_stroke.
 */
bool GrDragger::isA(SPItem *item, GrPointType point_type, gint point_i, Inkscape::PaintTarget fill_or_stroke)
{
    for (auto draggable : this->draggables) {
        if ( (draggable->point_type == point_type) && (draggable->point_i == point_i) && (draggable->item == item) && (draggable->fill_or_stroke == fill_or_stroke) ) {
            return true;
        }
    }
    return false;
}

/**
 * Checks if the dragger has a draggable with this item, point_type, fill_or_stroke.
 */
bool GrDragger::isA(SPItem *item, GrPointType point_type, Inkscape::PaintTarget fill_or_stroke)
{
    for (auto draggable : this->draggables) {
        if ( (draggable->point_type == point_type) && (draggable->item == item) && (draggable->fill_or_stroke == fill_or_stroke) ) {
            return true;
        }
    }
    return false;
}

bool GrDraggable::mayMerge(GrDraggable *da2)
{
    if ((this->item == da2->item) && (this->fill_or_stroke == da2->fill_or_stroke)) {
        // we must not merge the points of the same gradient!
        if (!((this->point_type == POINT_RG_FOCUS && da2->point_type == POINT_RG_CENTER) ||
              (this->point_type == POINT_RG_CENTER && da2->point_type == POINT_RG_FOCUS))) {
            // except that we can snap center and focus together
            return false;
        }
    }
    // disable merging of midpoints.
    if ( (this->point_type == POINT_LG_MID) || (da2->point_type == POINT_LG_MID)
         || (this->point_type == POINT_RG_MID1) || (da2->point_type == POINT_RG_MID1)
         || (this->point_type == POINT_RG_MID2) || (da2->point_type == POINT_RG_MID2) )
        return false;

    return true;
}

bool GrDragger::mayMerge(GrDragger *other)
{
    if (this == other)
        return false;

    for (auto da1 : this->draggables) {
        for (auto da2 : other->draggables) {
            if (!da1->mayMerge(da2))
                return false;
        }
    }
    return true;
}

bool GrDragger::mayMerge(GrDraggable *da2)
{
    for (auto da1 : this->draggables) {
        if (!da1->mayMerge(da2))
            return false;
    }
    return true;
}

/**
 * Update mesh handles when mesh corner is moved.
 * pc_old: old position of corner (could be changed to dp if we figure out transforms).
 * op: how other nodes (handles, tensors) should be moved.
 * Scaling takes place only between a selected and an unselected corner,
 * other wise a handle is displaced the same distance as the adjacent corner.
 * If a side is a line, then the handles are always placed 1/3 of side length
 * from each corner.
 *
 * Ooops, needs to be reimplemented.
 */
void
GrDragger::moveMeshHandles ( Geom::Point pc_old,  MeshNodeOperation op )
{
    // This routine might more properly be in mesh-context.cpp but moving knots is
    // handled here rather than there.

    // We need to update two places:
    //  1. In SPMeshArrayI with object coordinates
    //  2. In Drager/Knots with desktop coordinates.

    // This routine is more complicated than it might need to be inorder to allow
    // corner points to be selected in multiple meshes at the same time... with some
    // sharing the same dragger (overkill, perhaps?).

    // If no corner point in GrDragger then do nothing.
    if( !isA (POINT_MG_CORNER ) ) return;

    GrDrag *drag = this->parent;

    // We need a list of selected corners per mesh if scaling.
    std::map<SPGradient*, std::vector<guint> > selected_corners;
    // scaling was disabled so #if 0'ing out for now.
#if 0
    const bool scale = false;
    if( scale ) {

        for( std::set<GrDragger *>::const_iterator it = drag->selected.begin(); it != drag->selected.end(); ++it ) {
            GrDragger *dragger = *it;
            for (std::vector<GrDraggable *>::const_iterator it2 = dragger->draggables.begin(); it2 != dragger->draggables.end(); ++it2 ) {
                GrDraggable *draggable = *it2; 

                // Check draggable is of type POINT_MG_CORNER (don't allow selection of POINT_MG_HANDLE)
                if( draggable->point_type != POINT_MG_CORNER ) continue;

                // Must be a mesh gradient
                SPGradient *gradient = getGradient(draggable->item, draggable->fill_or_stroke);
                if ( !is<SPMeshGradient>( gradient ) ) continue;

                selected_corners[ gradient ].push_back( draggable->point_i );
            }
        }
    }
#endif

    // Now we do the handle moves.

    // Loop over all draggables in moved corner
    std::map<SPGradient*, std::vector<guint> > dragger_corners;
    for (auto draggable : draggables) {
        SPItem *item           = draggable->item;
        gint    point_type     = draggable->point_type;
        gint    point_i        = draggable->point_i;
        Inkscape::PaintTarget
                fill_or_stroke = draggable->fill_or_stroke;

        // Check draggable is of type POINT_MG_CORNER (don't allow selection of POINT_MG_HANDLE)
        if( point_type != POINT_MG_CORNER ) continue;

        // Must be a mesh gradient
        SPGradient *gradient = getGradient(item, fill_or_stroke);
        if ( !is<SPMeshGradient>( gradient ) ) continue;
        auto mg = cast<SPMeshGradient>( gradient );

        // pc_old is the old corner position in desktop coordinates, we need it in gradient coordinate.
        gradient = sp_gradient_convert_to_userspace (gradient, item, (fill_or_stroke == Inkscape::FOR_FILL) ? "fill" : "stroke");
        Geom::Affine i2d ( item->i2dt_affine() );
        Geom::Point pcg_old = pc_old * i2d.inverse();
        pcg_old *= (gradient->gradientTransform).inverse();

        mg->array.update_handles( point_i, selected_corners[ gradient ], pcg_old, op );
        mg->array.write( mg );

        // Move on-screen knots
        for( guint i = 0; i < mg->array.handles.size(); ++i ) {
            GrDragger *handle = drag->getDraggerFor( item, POINT_MG_HANDLE, i, fill_or_stroke ); 
            SPKnot *knot = handle->knot;
            Geom::Point pk = getGradientCoords( item, POINT_MG_HANDLE, i, fill_or_stroke );
            knot->moveto(pk);

        }

        for( guint i = 0; i < mg->array.tensors.size(); ++i ) {

            GrDragger *handle = drag->getDraggerFor( item, POINT_MG_TENSOR, i, fill_or_stroke ); 
            SPKnot *knot = handle->knot;
            Geom::Point pk = getGradientCoords( item, POINT_MG_TENSOR, i, fill_or_stroke );
            knot->moveto(pk);

        }

    } // Loop over draggables.
}


/**
 * Updates the statusbar tip of the dragger knot, based on its draggables.
 */
void GrDragger::updateTip()
{
    g_return_if_fail(this->knot != nullptr);

    char *tip = nullptr;
    
    if (this->draggables.size() == 1) {
        GrDraggable *draggable = this->draggables[0];
        char *item_desc = draggable->item->detailedDescription();
        switch (draggable->point_type) {
            case POINT_LG_MID:
            case POINT_RG_MID1:
            case POINT_RG_MID2:
                tip = g_strdup_printf (_("%s %d for: %s%s; drag with <b>Ctrl</b> to snap offset; click with <b>Ctrl+Alt</b> to delete stop"),
                                       _(gr_knot_descr.at(draggable->point_type)),
                                       draggable->point_i,
                                       item_desc,
                                       (draggable->fill_or_stroke == Inkscape::FOR_STROKE) ? _(" (stroke)") : "");
                break;

            case POINT_MG_CORNER:
            case POINT_MG_HANDLE:
            case POINT_MG_TENSOR:
                tip = g_strdup_printf (_("%s for: %s%s"),
                                       _(gr_knot_descr.at(draggable->point_type)),
                                       item_desc,
                                       (draggable->fill_or_stroke == Inkscape::FOR_STROKE) ? _(" (stroke)") : "");
                break;

            default:
                tip = g_strdup_printf (_("%s for: %s%s; drag with <b>Ctrl</b> to snap angle, with <b>Ctrl+Alt</b> to preserve angle, with <b>Ctrl+Shift</b> to scale around center"),
                                       _(gr_knot_descr.at(draggable->point_type)),
                                       item_desc,
                                       (draggable->fill_or_stroke == Inkscape::FOR_STROKE) ? _(" (stroke)") : "");
                break;
        }
        g_free(item_desc);
    } else if (draggables.size() == 2 && isA (POINT_RG_CENTER) && isA (POINT_RG_FOCUS)) {
        tip = g_strdup_printf ("%s", _("Radial gradient <b>center</b> and <b>focus</b>; drag with <b>Shift</b> to separate focus"));
    } else {
        int length = this->draggables.size();
        tip = g_strdup_printf (ngettext("Gradient point shared by <b>%d</b> gradient; drag with <b>Shift</b> to separate",
                                        "Gradient point shared by <b>%d</b> gradients; drag with <b>Shift</b> to separate",
                                        length),
                               length);
    }

    knot->setTip(tip);
    g_free(tip);
}

/**
 * Adds a draggable to the dragger.
 */
void GrDragger::updateKnotShape()
{
    if (draggables.empty())
        return;
    GrDraggable *last = draggables.back();

    this->knot->ctrl->set_type(gr_knot_types.at(last->point_type));
}

/**
 * Adds a draggable to the dragger.
 */
void GrDragger::addDraggable(GrDraggable *draggable)
{
    this->draggables.insert(this->draggables.begin(), draggable);

    this->updateTip();
}


/**
 * Moves this dragger to the point of the given draggable, acting upon all other draggables.
 */
void GrDragger::moveThisToDraggable(SPItem *item, GrPointType point_type, gint point_i, Inkscape::PaintTarget fill_or_stroke, bool write_repr)
{
    if (draggables.empty())
        return;

    GrDraggable *dr_first = draggables[0];

    this->point = getGradientCoords(dr_first->item, dr_first->point_type, dr_first->point_i, dr_first->fill_or_stroke);
    this->point_original = this->point;

    this->knot->moveto(this->point);

    for (auto da : draggables) {
        if ( (da->item == item) &&
             (da->point_type == point_type) &&
             (point_i == -1 || da->point_i == point_i) &&
             (da->fill_or_stroke == fill_or_stroke) ) {
            // Don't move initial draggable
            continue;
        }
        sp_item_gradient_set_coords(da->item, da->point_type, da->point_i, this->point, da->fill_or_stroke, write_repr, false);
    }
    // FIXME: here we should also call this->updateDependencies(write_repr); to propagate updating, but how to prevent loops?
}


/**
 * Moves all midstop draggables that depend on this one.
 */
void GrDragger::updateMidstopDependencies(GrDraggable *draggable, bool write_repr)
{
    SPObject *server = draggable->getServer();
    if (!server)
        return;
    guint num = cast<SPGradient>(server)->vector.stops.size();
    if (num <= 2) return;

    if ( is<SPLinearGradient>(server) ) {
        for ( guint i = 1; i < num - 1; i++ ) {
            this->moveOtherToDraggable (draggable->item, POINT_LG_MID, i, draggable->fill_or_stroke, write_repr);
        }
    } else  if ( is<SPRadialGradient>(server) ) {
        for ( guint i = 1; i < num - 1; i++ ) {
            this->moveOtherToDraggable (draggable->item, POINT_RG_MID1, i, draggable->fill_or_stroke, write_repr);
            this->moveOtherToDraggable (draggable->item, POINT_RG_MID2, i, draggable->fill_or_stroke, write_repr);
        }
    }
}


/**
 * Moves all draggables that depend on this one.
 */
void GrDragger::updateDependencies(bool write_repr)
{
    for (auto draggable : draggables) {
        switch (draggable->point_type) {
            case POINT_LG_BEGIN:
                {
                    // the end point is dependent only when dragging with ctrl+shift
                    this->moveOtherToDraggable (draggable->item, POINT_LG_END, -1, draggable->fill_or_stroke, write_repr);

                    this->updateMidstopDependencies (draggable, write_repr);
                }
                break;
            case POINT_LG_END:
                {
                    // the begin point is dependent only when dragging with ctrl+shift
                    this->moveOtherToDraggable (draggable->item, POINT_LG_BEGIN, 0, draggable->fill_or_stroke, write_repr);

                    this->updateMidstopDependencies (draggable, write_repr);
                }
                break;
            case POINT_LG_MID:
                // no other nodes depend on mid points.
                break;
            case POINT_RG_R2:
                this->moveOtherToDraggable (draggable->item, POINT_RG_R1, -1, draggable->fill_or_stroke, write_repr);
                this->moveOtherToDraggable (draggable->item, POINT_RG_FOCUS, -1, draggable->fill_or_stroke, write_repr);
                this->updateMidstopDependencies (draggable, write_repr);
                break;
            case POINT_RG_R1:
                this->moveOtherToDraggable (draggable->item, POINT_RG_R2, -1, draggable->fill_or_stroke, write_repr);
                this->moveOtherToDraggable (draggable->item, POINT_RG_FOCUS, -1, draggable->fill_or_stroke, write_repr);
                this->updateMidstopDependencies (draggable, write_repr);
                break;
            case POINT_RG_CENTER:
                this->moveOtherToDraggable (draggable->item, POINT_RG_R1, -1, draggable->fill_or_stroke, write_repr);
                this->moveOtherToDraggable (draggable->item, POINT_RG_R2, -1, draggable->fill_or_stroke, write_repr);
                this->moveOtherToDraggable (draggable->item, POINT_RG_FOCUS, -1, draggable->fill_or_stroke, write_repr);
                this->updateMidstopDependencies (draggable, write_repr);
                break;
            case POINT_RG_FOCUS:
                // nothing can depend on that
                break;
            case POINT_RG_MID1:
                this->moveOtherToDraggable (draggable->item, POINT_RG_MID2, draggable->point_i, draggable->fill_or_stroke, write_repr);
                break;
            case POINT_RG_MID2:
                this->moveOtherToDraggable (draggable->item, POINT_RG_MID1, draggable->point_i, draggable->fill_or_stroke, write_repr);
                break;
            default:
                break;
        }
    }
}



GrDragger::GrDragger(GrDrag *parent, Geom::Point p, GrDraggable *draggable)
  : point(p),
    point_original(p)
{
    this->draggables.clear();

    this->parent = parent;


    // create the knot
    this->knot = new SPKnot(parent->desktop, "", Inkscape::CANVAS_ITEM_CTRL_TYPE_SIZER, "CanvasItemCtrl::GrDragger");
    this->knot->updateCtrl();

    // move knot to the given point
    this->knot->setPosition(p, SP_KNOT_STATE_NORMAL);
    this->knot->show();

    // connect knot's signals
    if ( (draggable)  // it can be NULL if a node in unsnapped (eg. focus point unsnapped from center)
                       // luckily, midstops never snap to other nodes so are never unsnapped...
         && ( (draggable->point_type == POINT_LG_MID)
              || (draggable->point_type == POINT_RG_MID1)
              || (draggable->point_type == POINT_RG_MID2) ) )
    {
        this->_moved_connection = this->knot->moved_signal.connect(sigc::bind(sigc::ptr_fun(gr_knot_moved_midpoint_handler), this));
    } else {
        this->_moved_connection = this->knot->moved_signal.connect(sigc::bind(sigc::ptr_fun(gr_knot_moved_handler), this));
    }

    this->_clicked_connection =
        this->knot->click_signal.connect(sigc::bind(sigc::ptr_fun(gr_knot_clicked_handler), this));

    this->_doubleclicked_connection =
        this->knot->doubleclicked_signal.connect(sigc::bind(sigc::ptr_fun(gr_knot_doubleclicked_handler), this));

    this->_mousedown_connection =
        this->knot->mousedown_signal.connect(sigc::bind(sigc::ptr_fun(gr_knot_mousedown_handler), this));

    this->_ungrabbed_connection =
        this->knot->ungrabbed_signal.connect(sigc::bind(sigc::ptr_fun(gr_knot_ungrabbed_handler), this));

    // add the initial draggable
    if (draggable) {
        this->addDraggable (draggable);
    }

    updateKnotShape();
}

GrDragger::~GrDragger()
{
    // unselect if it was selected
    // Hmm, this causes a race condition as it triggers a call to gradient_selection_changed which
    // can be executed while a list of draggers is being deleted. It doesn't actually seem to be
    // necessary.
    //this->parent->setDeselected(this);

    // disconnect signals
    this->_moved_connection.disconnect();
    this->_clicked_connection.disconnect();
    this->_doubleclicked_connection.disconnect();
    this->_mousedown_connection.disconnect();
    this->_ungrabbed_connection.disconnect();

    // unref should call destroy
    SPKnot::unref(knot);

    // delete all draggables
    for (auto draggable : this->draggables) {
        delete draggable;
    }
    this->draggables.clear();
}

/**
 * Select the dragger which has the given draggable.
 */
GrDragger *GrDrag::getDraggerFor(GrDraggable *d) {
    for (auto dragger : this->draggers) {
        for (std::vector<GrDraggable *>::const_iterator j = dragger->draggables.begin(); j != dragger->draggables.end(); ++j ) {
            if (d == *j) {
                return dragger;
            }
        }
    }
    return nullptr;
}

/**
 * Select the dragger which has the given draggable.
 */
GrDragger *GrDrag::getDraggerFor(SPItem *item, GrPointType point_type, gint point_i, Inkscape::PaintTarget fill_or_stroke)
{
    for (auto dragger : this->draggers) {
        for (std::vector<GrDraggable *>::const_iterator j = dragger->draggables.begin(); j != dragger->draggables.end(); ++j ) {
            GrDraggable *da2 = *j; 
            if ( (da2->item == item) &&
                 (da2->point_type == point_type) &&
                 (point_i == -1 || da2->point_i == point_i) && // -1 means this does not matter
                 (da2->fill_or_stroke == fill_or_stroke)) {
                return (dragger);
            }
        }
    }
    return nullptr;
}


void GrDragger::moveOtherToDraggable(SPItem *item, GrPointType point_type, gint point_i, Inkscape::PaintTarget fill_or_stroke, bool write_repr)
{
    GrDragger *d = this->parent->getDraggerFor(item, point_type, point_i, fill_or_stroke);
    if (d && d !=  this) {
        d->moveThisToDraggable(item, point_type, point_i, fill_or_stroke, write_repr);
    }
}

/**
 * Find mesh corner corresponding to given dragger.
 */
GrDragger* GrDragger::getMgCorner(){
    GrDraggable *draggable = (GrDraggable *) this->draggables[0];
    if (draggable) {

        // If corner, we already found it!
        if (draggable->point_type == POINT_MG_CORNER) {
            return this;
        }

        // The mapping between handles and corners is complex... so find corner by bruit force.
        SPGradient *gradient = getGradient(draggable->item, draggable->fill_or_stroke);
        auto mg = cast<SPMeshGradient>(gradient);
        if (mg) {
            std::vector< std::vector< SPMeshNode* > > nodes = mg->array.nodes;
            for (guint i = 0; i < nodes.size(); ++i) {
                for (guint j = 0; j < nodes[i].size(); ++j) {
                    if (nodes[i][j]->set && nodes[i][j]->node_type == MG_NODE_TYPE_HANDLE) {
                        if (draggable->point_i == (gint)nodes[i][j]->draggable) {

                            if (nodes.size() > i+1 && nodes[i+1].size() > j && nodes[i+1][j]->node_type == MG_NODE_TYPE_CORNER) {
                                return this->parent->getDraggerFor(draggable->item, POINT_MG_CORNER,  nodes[i+1][j]->draggable, draggable->fill_or_stroke);
                            }

                            if (j != 0 && nodes.size() > i && nodes[i].size() > j-1 && nodes[i][j-1]->node_type == MG_NODE_TYPE_CORNER) {
                                return this->parent->getDraggerFor(draggable->item, POINT_MG_CORNER,  nodes[i][j-1]->draggable, draggable->fill_or_stroke);
                            }

                            if (i != 0 && nodes.size() > i-1 && nodes[i-1].size() > j && nodes[i-1][j]->node_type == MG_NODE_TYPE_CORNER) {
                                return this->parent->getDraggerFor(draggable->item, POINT_MG_CORNER,  nodes[i-1][j]->draggable, draggable->fill_or_stroke);
                            }

                            if (nodes.size() > i && nodes[i].size() > j+1 && nodes[i][j+1]->node_type == MG_NODE_TYPE_CORNER) {
                                return this->parent->getDraggerFor(draggable->item, POINT_MG_CORNER,  nodes[i][j+1]->draggable, draggable->fill_or_stroke);
                            }
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}

/**
 * Highlight mesh node
 */
void GrDragger::highlightNode(SPMeshNode *node, bool highlight, Geom::Point corner_pos, int index)
{
    GrPointType type = POINT_MG_TENSOR;
    if (node->node_type == MG_NODE_TYPE_HANDLE) {
        type = POINT_MG_HANDLE;
    }

    GrDraggable *draggable = (GrDraggable *) this->draggables[0];
    GrDragger *d = this->parent->getDraggerFor(draggable->item, type, node->draggable, draggable->fill_or_stroke);
    if (d && node->draggable < G_MAXUINT) {
        Geom::Point end = d->knot->pos;
        Geom::Ray ray = Geom::Ray(corner_pos, end);
        if (d->knot->desktop->is_yaxisdown()) {
            end *= Geom::Scale(1, -1);
            corner_pos *= Geom::Scale(1, -1);
            ray.setPoints(corner_pos, end);
        }
        double angl = ray.angle();

        SPKnot *knot = d->knot;
        if (type == POINT_MG_HANDLE) {
            if (highlight) {
                knot->selectKnot(true);
            } else {
                knot->selectKnot(false);
            }
        } else {
            //Code for tensors
            return;
        }

        knot->setAngle(angl);
        knot->updateCtrl();
        d->updateKnotShape();
    }
}

/**
 * Highlight handles for mesh corner corresponding to this dragger.
 */
void  GrDragger::highlightCorner(bool highlight)
{
    // Must be a mesh gradient
    GrDraggable *draggable = (GrDraggable *) this->draggables[0];
    if (draggable &&  draggable->point_type == POINT_MG_CORNER) {
        SPGradient *gradient = getGradient(draggable->item, draggable->fill_or_stroke);
        if (is<SPMeshGradient>( gradient )) {
            Geom::Point corner_point = this->point;
            gint corner = draggable->point_i;
            auto mg = cast<SPMeshGradient>( gradient );
            SPMeshNodeArray mg_arr = mg->array;
            std::vector< std::vector< SPMeshNode* > > nodes = mg_arr.nodes;
            // Find number of patch rows and columns
            guint mrow = mg_arr.patch_rows();
            guint mcol = mg_arr.patch_columns();
            // Number of corners in a row of patches.
            guint ncorners = mcol + 1;
            // Find corner row/column
            guint crow = corner / ncorners;
            guint ccol = corner % ncorners;
            // Find node row/column
            guint nrow  = crow * 3;
            guint ncol  = ccol * 3;

            bool patch[4];
            patch[0] = patch[1] = patch[2] = patch[3] = false;
            if (ccol > 0    && crow > 0    ) patch[0] = true;
            if (ccol < mcol && crow > 0    ) patch[1] = true;
            if (ccol < mcol && crow < mrow ) patch[2] = true;
            if (ccol > 0    && crow < mrow ) patch[3] = true;
            if (patch[0] || patch[1]) {
                highlightNode(nodes[nrow - 1][ncol], highlight, corner_point, 0);
            }
            if (patch[1] || patch[2])  {
                highlightNode(nodes[nrow][ncol + 1], highlight, corner_point, 1);
            }
            if (patch[2] || patch[3]) {
                highlightNode(nodes[nrow + 1][ncol], highlight, corner_point, 2);
            }
            if (patch[3] || patch[0]) {
                highlightNode(nodes[nrow][ncol - 1], highlight, corner_point, 3);
            }
            // Highlight tensors
            /*
            if( patch[0] ) highlightNode(nodes[nrow-1][ncol-1], highlight, corner_point, point_i);
            if( patch[1] ) highlightNode(nodes[nrow-1][ncol+1], highlight, corner_point, point_i);
            if( patch[2] ) highlightNode(nodes[nrow+1][ncol+1], highlight, corner_point, point_i);
            if( patch[3] ) highlightNode(nodes[nrow+1][ncol-1], highlight, corner_point, point_i);
            */
        }
    }
}

/**
 * Draw this dragger as selected.
 */
void GrDragger::select()
{
    this->knot->selectKnot(true);
    highlightCorner(true);
}

/**
 * Draw this dragger as normal (deselected).
 */
void GrDragger::deselect()
{
    this->knot->selectKnot(false);
    highlightCorner(false);
}

bool
GrDragger::isSelected()
{
    return parent->selected.find(this) != parent->selected.end();
}

/**
 * Deselect all stops/draggers (private).
 */
void GrDrag::deselect_all()
{
    for (auto it : selected)
        it->deselect();
    selected.clear();
}

/**
 * Deselect all stops/draggers (public; emits signal).
 */
void GrDrag::deselectAll()
{
    deselect_all();
    desktop->emit_gradient_stop_selected(this, nullptr);
}

/**
 * Select all stops/draggers.
 */
void GrDrag::selectAll()
{
    for (auto d : this->draggers) {
        setSelected (d, true, true);
    }
}

/**
 * Select all stops/draggers that match the coords.
 */
void GrDrag::selectByCoords(std::vector<Geom::Point> coords)
{
    for (auto d : this->draggers) {
        for (auto coord : coords) {
            if (Geom::L2 (d->point - coord) < 1e-4) {
                setSelected (d, true, true);
            }
        }
    }
}

/**
 * Select draggers by stop
 */
void GrDrag::selectByStop(SPStop *stop, bool add_to_selection, bool override )
{
    for (auto dragger : this->draggers) {

        for (std::vector<GrDraggable *>::const_iterator j = dragger->draggables.begin(); j != dragger->draggables.end(); ++j) {

            GrDraggable *d = *j;
            SPGradient *gradient = getGradient(d->item, d->fill_or_stroke);
            SPGradient *vector = gradient->getVector(false);
            SPStop *stop_i = sp_get_stop_i(vector, d->point_i);

            if (stop_i == stop) {
                setSelected(dragger, add_to_selection, override);
            }
        }
    }
}
/**
 * Select all stops/draggers that fall within the rect.
 */
void GrDrag::selectRect(Geom::Rect const &r)
{
    for (auto d : this->draggers) {
        if (r.contains(d->point)) {
           setSelected (d, true, true);
        }
    }
}

/**
 * Select a dragger.
 * @param dragger       The dragger to select.
 * @param add_to_selection   If true, add to selection, otherwise deselect others.
 * @param override      If true, always select this node, otherwise toggle selected status.
*/
void GrDrag::setSelected(GrDragger *dragger, bool add_to_selection, bool override)
{
    GrDragger *seldragger = nullptr;

    // Don't allow selecting a mesh handle or mesh tensor.
    // We might want to rethink since a dragger can have draggables of different types.
    if ( dragger->isA( POINT_MG_HANDLE ) || dragger->isA( POINT_MG_TENSOR ) ) return;

    if (add_to_selection) {
        if (!dragger) return;
        if (override) {
                selected.insert(dragger);
            dragger->select();
            seldragger = dragger;
        } else { // toggle
            if(selected.find(dragger)!=selected.end()) {
                selected.erase(dragger);
                dragger->deselect();
                if (!selected.empty()) {
                    seldragger = *(selected.begin()); // select the dragger that is first in the list
                }
            } else {
                selected.insert(dragger);
                dragger->select();
                seldragger = dragger;
            }
        }
    } else {
        deselect_all();
        if (dragger) {
            selected.insert(dragger);
            dragger->select();
            seldragger = dragger;
        }
    }
    if (seldragger) {
        desktop->emit_gradient_stop_selected(this, nullptr);
    }
}

/**
 * Deselect a dragger.
 * @param dragger       The dragger to deselect.
 */
void GrDrag::setDeselected(GrDragger *dragger)
{
    if (selected.find(dragger) != selected.end()) {
        selected.erase(dragger);
        dragger->deselect();
    }
    desktop->emit_gradient_stop_selected(this, nullptr);
}

/**
 * Create a line from p1 to p2 and add it to the curves list. Used for linear and radial gradients.
 */
void GrDrag::addLine(SPItem *item, Geom::Point p1, Geom::Point p2, Inkscape::PaintTarget fill_or_stroke)
{
    auto const canvas_item_color = fill_or_stroke == Inkscape::FOR_FILL ? Inkscape::CANVAS_ITEM_PRIMARY : Inkscape::CANVAS_ITEM_SECONDARY;

    auto curve = make_canvasitem<Inkscape::CanvasItemCurve>(desktop->getCanvasControls(), p1, p2);
    curve->set_name("GradientLine");
    curve->set_stroke(canvas_item_color);

    auto item_curve = ItemCurve();
    item_curve.item = item;
    item_curve.curve = std::move(curve);
    item_curve.is_fill = fill_or_stroke == Inkscape::FOR_FILL;
    item_curves.emplace_back(std::move(item_curve));
}

/**
 * Create a curve from p0 to p3 and add it to the curves list. Used for mesh sides.
 */
void GrDrag::addCurve(SPItem *item, Geom::Point p0, Geom::Point p1, Geom::Point p2, Geom::Point p3,
                      int corner0, int corner1, int handle0, int handle1, Inkscape::PaintTarget fill_or_stroke)

{
    // Highlight curve if one of its draggers has a mouse over it.
    bool highlight = false;
    GrDragger* dragger0 = getDraggerFor(item, POINT_MG_CORNER, corner0, fill_or_stroke);
    GrDragger* dragger1 = getDraggerFor(item, POINT_MG_CORNER, corner1, fill_or_stroke);
    GrDragger* dragger2 = getDraggerFor(item, POINT_MG_HANDLE, handle0, fill_or_stroke);
    GrDragger* dragger3 = getDraggerFor(item, POINT_MG_HANDLE, handle1, fill_or_stroke);
    if ((dragger0->knot && (dragger0->knot->flags & SP_KNOT_MOUSEOVER)) ||
        (dragger1->knot && (dragger1->knot->flags & SP_KNOT_MOUSEOVER)) ||
        (dragger2->knot && (dragger2->knot->flags & SP_KNOT_MOUSEOVER)) ||
        (dragger3->knot && (dragger3->knot->flags & SP_KNOT_MOUSEOVER)) ) {
        highlight = true;
    }

    auto const canvas_item_color = (fill_or_stroke == Inkscape::FOR_FILL) ^ highlight ? Inkscape::CANVAS_ITEM_PRIMARY : Inkscape::CANVAS_ITEM_SECONDARY;

    auto curve = make_canvasitem<Inkscape::CanvasItemCurve>(desktop->getCanvasControls(), p0, p1, p2, p3);
    curve->set_name("GradientCurve");
    curve->set_stroke(canvas_item_color);

    auto item_curve = ItemCurve();
    item_curve.item = item;
    item_curve.curve = std::move(curve);
    item_curve.is_fill = fill_or_stroke == Inkscape::FOR_FILL;
    item_curve.corner0 = corner0;
    item_curve.corner1 = corner1;
    item_curves.emplace_back(std::move(item_curve));
}

/**
 * If there already exists a dragger within MERGE_DIST of p, add the draggable to it; otherwise create
 * new dragger and add it to draggers list.
 */
GrDragger* GrDrag::addDragger(GrDraggable *draggable)
{
    Geom::Point p = getGradientCoords(draggable->item, draggable->point_type, draggable->point_i, draggable->fill_or_stroke);

    for (auto dragger : this->draggers) {
        if (dragger->mayMerge (draggable) && Geom::L2 (dragger->point - p) < MERGE_DIST) {
            // distance is small, merge this draggable into dragger, no need to create new dragger
            dragger->addDraggable (draggable);
            dragger->updateKnotShape();
            return dragger;
        }
    }

    GrDragger *new_dragger = new GrDragger(this, p, draggable);
    // fixme: draggers should be added AFTER the last one: this way tabbing through them will be from begin to end.
    this->draggers.push_back(new_dragger);
    return new_dragger;
}

/**
 * Add draggers for the radial gradient rg on item.
 */
void GrDrag::addDraggersRadial(SPRadialGradient *rg, SPItem *item, Inkscape::PaintTarget fill_or_stroke)
{
    rg->ensureVector();
    addDragger (new GrDraggable (item, POINT_RG_CENTER, 0, fill_or_stroke));
    guint num = rg->vector.stops.size();
    if (num > 2) {
        for ( guint i = 1; i < num - 1; i++ ) {
            addDragger (new GrDraggable (item, POINT_RG_MID1, i, fill_or_stroke));
        }
    }
    addDragger (new GrDraggable (item, POINT_RG_R1, num-1, fill_or_stroke));
    if (num > 2) {
        for ( guint i = 1; i < num - 1; i++ ) {
            addDragger (new GrDraggable (item, POINT_RG_MID2, i, fill_or_stroke));
        }
    }
    addDragger (new GrDraggable (item, POINT_RG_R2, num - 1, fill_or_stroke));
    addDragger (new GrDraggable (item, POINT_RG_FOCUS, 0, fill_or_stroke));
}

/**
 * Add draggers for the linear gradient lg on item.
 */
void GrDrag::addDraggersLinear(SPLinearGradient *lg, SPItem *item, Inkscape::PaintTarget fill_or_stroke)
{
    lg->ensureVector();
    addDragger(new GrDraggable (item, POINT_LG_BEGIN, 0, fill_or_stroke));
    guint num = lg->vector.stops.size();
    if (num > 2) {
        for ( guint i = 1; i < num - 1; i++ ) {
            addDragger(new GrDraggable (item, POINT_LG_MID, i, fill_or_stroke));
        }
    }
    addDragger(new GrDraggable (item, POINT_LG_END, num - 1, fill_or_stroke));
}

/**
 *Add draggers for the mesh gradient mg on item
 */
void GrDrag::addDraggersMesh(SPMeshGradient *mg, SPItem *item, Inkscape::PaintTarget fill_or_stroke)
{
    mg->ensureArray();
    std::vector< std::vector< SPMeshNode* > > nodes = mg->array.nodes;

    // Show/hide mesh on fill/stroke. This doesn't work at the moment... and prevents node color updating.
    
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool show_handles = (prefs->getBool("/tools/mesh/show_handles", true));
    bool edit_fill    = (prefs->getBool("/tools/mesh/edit_fill",    true));
    bool edit_stroke  = (prefs->getBool("/tools/mesh/edit_stroke",  true));

    // Make sure we have at least one patch defined.
    if( mg->array.patch_rows() == 0 || mg->array.patch_columns() == 0 ) {

        std::cerr << "Empty Mesh, No Draggers to Add" << std::endl;
        return;
    }

    guint icorner = 0;
    guint ihandle = 0;
    guint itensor = 0;
    mg->array.corners.clear();
    mg->array.handles.clear();
    mg->array.tensors.clear();

    if( (fill_or_stroke == Inkscape::FOR_FILL   && !edit_fill) ||
        (fill_or_stroke == Inkscape::FOR_STROKE && !edit_stroke) ) {
        return;
    }

    for(auto & node : nodes) {
        for(auto & j : node) {

            // std::cout << " Draggers: " << i << " " << j << " " << nodes[i][j]->node_type << std::endl;
            switch ( j->node_type ) {

                case MG_NODE_TYPE_CORNER:
                {
                    mg->array.corners.push_back( j );
                    GrDraggable *corner = new GrDraggable (item, POINT_MG_CORNER, icorner, fill_or_stroke);
                    addDragger ( corner );
                    j->draggable = icorner;
                    ++icorner;
                    break;
                }

                case MG_NODE_TYPE_HANDLE:
                {
                    mg->array.handles.push_back( j );
                    GrDraggable *handle = new GrDraggable (item, POINT_MG_HANDLE, ihandle, fill_or_stroke);
                    GrDragger* dragger = addDragger ( handle );

                    if( !show_handles || !j->set ) {
                        dragger->knot->hide();
                    }
                    j->draggable = ihandle;
                    ++ihandle;
                    break;
                }

                case MG_NODE_TYPE_TENSOR:
                {
                    mg->array.tensors.push_back( j );
                    GrDraggable *tensor = new GrDraggable (item, POINT_MG_TENSOR, itensor, fill_or_stroke);
                    GrDragger* dragger = addDragger ( tensor );
                    if( !show_handles || !j->set ) {
                        dragger->knot->hide();
                    }
                    j->draggable = itensor;
                    ++itensor;
                    break;
                }

                default:
                    std::cerr << "Bad Mesh draggable type" << std::endl;
                    break;
            }
        }
    }

    mg->array.draggers_valid = true;
}

/**
 * Refresh draggers, moving and toggling visibility as necessary.
 * Does not regenerate draggers (as does updateDraggersMesh()).
 */
void GrDrag::refreshDraggersMesh(SPMeshGradient *mg, SPItem *item, Inkscape::PaintTarget fill_or_stroke)
{
    mg->ensureArray();
    std::vector< std::vector< SPMeshNode* > > nodes = mg->array.nodes;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool show_handles = (prefs->getBool("/tools/mesh/show_handles", true));

    // Make sure we have at least one patch defined.
    if( mg->array.patch_rows() == 0 || mg->array.patch_columns() == 0 ) {

        std::cerr << "GrDrag::refreshDraggersMesh: Empty Mesh, No Draggers to refresh!" << std::endl;
        return;
    }

    guint ihandle = 0;
    guint itensor = 0;

    for(auto & node : nodes) {
        for(auto & j : node) {

            // std::cout << " Draggers: " << i << " " << j << " " << nodes[i][j]->node_type << std::endl;

            switch ( j->node_type ) {

                case MG_NODE_TYPE_CORNER:
                    // Do nothing, corners are always shown.
                    break;

                case MG_NODE_TYPE_HANDLE:
                {
                    GrDragger* dragger = getDraggerFor(item, POINT_MG_HANDLE, ihandle, fill_or_stroke);
                    if (dragger) {
                        Geom::Point pk = getGradientCoords( item, POINT_MG_HANDLE, ihandle, fill_or_stroke);
                        dragger->knot->moveto(pk);
                        if( !show_handles || !j->set ) {
                            dragger->knot->hide();
                        } else {
                            dragger->knot->show();
                        }
                    } else {
                        // This can happen if a draggable is not visible.
                        // std::cerr << "GrDrag::refreshDraggersMesh: No dragger!" << std::endl;
                    }
                    ++ihandle;
                    break;
                }

                case MG_NODE_TYPE_TENSOR:
                {
                    GrDragger* dragger = getDraggerFor(item, POINT_MG_TENSOR, itensor, fill_or_stroke);
                    if (dragger) {
                        Geom::Point pk = getGradientCoords( item, POINT_MG_TENSOR, itensor, fill_or_stroke);
                        dragger->knot->moveto(pk);
                        if( !show_handles || !j->set ) {
                            dragger->knot->hide();
                        } else {
                            dragger->knot->show();
                        }
                    } else {
                        // This can happen if a draggable is not visible.
                        // std::cerr << "GrDrag::refreshDraggersMesh: No dragger!" << std::endl;
                    }
                    ++itensor;
                    break;
                }

                default:
                    std::cerr << "Bad Mesh draggable type" << std::endl;
                    break;
            }
        }
    }
}

/**
 * Artificially grab the knot of this dragger; used by the gradient context.
 * Not used at the moment.
 */
void GrDrag::grabKnot(GrDragger *dragger, gint x, gint y, guint32 etime)
{
    if (dragger) {
        dragger->knot->startDragging(dragger->point, {x, y}, etime);
    }
}

/**
 * Artificially grab the knot of the dragger with this draggable; used by the gradient context.
 * This allows setting the final point from the end of the drag when creating a new gradient.
 */
void GrDrag::grabKnot(SPItem *item, GrPointType point_type, gint point_i, Inkscape::PaintTarget fill_or_stroke, gint x, gint y, guint32 etime)
{
    GrDragger *dragger = getDraggerFor(item, point_type, point_i, fill_or_stroke);
    if (dragger) {
        dragger->knot->startDragging(dragger->point, {x, y}, etime);
    }
}

/**
 * Regenerates the draggers list from the current selection; is called when selection is changed or
 * modified, also when a radial dragger needs to update positions of other draggers in the gradient.
 */
void GrDrag::updateDraggers()
{
    selected.clear();
    // delete old draggers
    for (auto dragger : this->draggers) {
        delete dragger;
    }
    this->draggers.clear();

    g_return_if_fail(this->selection != nullptr);
    auto list = this->selection->items();
    for (auto i = list.begin(); i != list.end(); ++i) {
        SPItem *item = *i;
        SPStyle *style = item->style;

        if (style && (style->fill.isPaintserver())) {
            SPPaintServer *server = style->getFillPaintServer();
            if (auto gradient = cast<SPGradient>(server)) {
                if (gradient->isSolid() || (gradient->getVector() && gradient->getVector()->isSolid())) {
                    // Suppress "gradientness" of solid paint
                } else if (is<SPLinearGradient>(server)) {
                    addDraggersLinear( cast<SPLinearGradient>(server), item, Inkscape::FOR_FILL );
                } else if (is<SPRadialGradient>(server)) {
                    addDraggersRadial( cast<SPRadialGradient>(server), item, Inkscape::FOR_FILL );
                } else if (is<SPMeshGradient>(server)) {
                    addDraggersMesh(   cast<SPMeshGradient>(server),   item, Inkscape::FOR_FILL );
                }
            }
        }

        if (style && (style->stroke.isPaintserver())) {
            SPPaintServer *server = style->getStrokePaintServer();
            if (auto gradient = cast<SPGradient>(server)) {
                if (gradient->isSolid() || (gradient->getVector() && gradient->getVector()->isSolid())) {
                    // Suppress "gradientness" of solid paint
                } else if (is<SPLinearGradient>(server)) {
                    addDraggersLinear( cast<SPLinearGradient>(server), item, Inkscape::FOR_STROKE );
                } else if (is<SPRadialGradient>(server)) {
                    addDraggersRadial( cast<SPRadialGradient>(server), item, Inkscape::FOR_STROKE );
                } else if (is<SPMeshGradient>(server)) {
                    addDraggersMesh(   cast<SPMeshGradient>(server),   item, Inkscape::FOR_STROKE );
                }
            }
        }
    }
}


/**
 * Refresh draggers, moving and toggling visibility as necessary.
 * Does not regenerate draggers (as does updateDraggers()).
 * Only applies to mesh gradients.
 */
void GrDrag::refreshDraggers()
{

    g_return_if_fail(this->selection != nullptr);
    auto list = this->selection->items();
    for (auto i = list.begin(); i != list.end(); ++i) {
        SPItem *item = *i;
        SPStyle *style = item->style;

        if (style && (style->fill.isPaintserver())) {
            SPPaintServer *server = style->getFillPaintServer();
            if ( server && is<SPGradient>( server ) ) {
                if ( is<SPMeshGradient>(server) ) {
                    refreshDraggersMesh(   cast<SPMeshGradient>(server),   item, Inkscape::FOR_FILL );
                }
            }
        }

        if (style && (style->stroke.isPaintserver())) {
            SPPaintServer *server = style->getStrokePaintServer();
            if ( server && is<SPGradient>( server ) ) {
                if ( is<SPMeshGradient>(server) ) {
                    refreshDraggersMesh(   cast<SPMeshGradient>(server),   item, Inkscape::FOR_STROKE );
                }
            }
        }
    }
}


/**
 * Returns true if at least one of the draggers' knots has the mouse hovering above it.
 */
bool GrDrag::mouseOver()
{
    static bool mouse_out = false;
    // added knot mouse out for future use
    for (auto d : this->draggers) {
        if (d->knot && (d->knot->flags & SP_KNOT_MOUSEOVER)) {
            mouse_out = true;
            updateLines();
            return true;
        }
    }
    if(mouse_out == true){
        updateLines();
        mouse_out = false;
    }
    return false;
}

/**
 * Regenerates the lines list from the current selection; is called on each move of a dragger, so that
 * lines are always in sync with the actual gradient.
 */
void GrDrag::updateLines()
{
    item_curves.clear();

    g_return_if_fail(this->selection != nullptr);

    auto list = this->selection->items();
    for (auto i = list.begin(); i != list.end(); ++i) {
        SPItem *item = *i;

        SPStyle *style = item->style;

        if (style && (style->fill.isPaintserver())) {
            SPPaintServer *server = item->style->getFillPaintServer();
            if (auto gradient = cast<SPGradient>(server)) {
                if (gradient->isSolid() || (gradient->getVector() && gradient->getVector()->isSolid())) {
                    // Suppress "gradientness" of solid paint
                } else if (is<SPLinearGradient>(server)) {
                    addLine(item, getGradientCoords(item, POINT_LG_BEGIN, 0, Inkscape::FOR_FILL), getGradientCoords(item, POINT_LG_END, 0, Inkscape::FOR_FILL), Inkscape::FOR_FILL);
                } else if (is<SPRadialGradient>(server)) {
                    Geom::Point center = getGradientCoords(item, POINT_RG_CENTER, 0, Inkscape::FOR_FILL);
                    addLine(item, center, getGradientCoords(item, POINT_RG_R1, 0, Inkscape::FOR_FILL), Inkscape::FOR_FILL);
                    addLine(item, center, getGradientCoords(item, POINT_RG_R2, 0, Inkscape::FOR_FILL), Inkscape::FOR_FILL);
                } else if (is<SPMeshGradient>(server)) {
                    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
                    bool edit_fill    = (prefs->getBool("/tools/mesh/edit_fill",    true));

                    auto mg = cast<SPMeshGradient>(server);

                    if (edit_fill) {
                    guint rows    = mg->array.patch_rows();
                    guint columns = mg->array.patch_columns();
                    for ( guint i = 0; i < rows; ++i ) {
                        for ( guint j = 0; j < columns; ++j ) {

                            std::vector<Geom::Point> h;

                            SPMeshPatchI patch( &(mg->array.nodes), i, j );

                            // clockwise around patch, used to find corner dragger
                            int corner0 = i * (columns + 1) + j;
                            int corner1 = corner0 + 1;
                            int corner2 = corner1 + columns + 1;
                            int corner3 = corner2 - 1;
                            // clockwise around patch, used to find handle dragger
                            int handle0 = 2*j + i*(2+4*columns);
                            int handle1 = handle0 + 1;
                            int handle2 = j + i*(2+4*columns) + 2*columns + 1;
                            int handle3 = j + i*(2+4*columns) + 3*columns + 2;
                            int handle4 = handle1 + (2+4*columns);
                            int handle5 = handle0 + (2+4*columns);
                            int handle6 = handle3 - 1;
                            int handle7 = handle2 - 1;

                            // Top line
                            h = patch.getPointsForSide( 0 );
                            for( guint p = 0; p < 4; ++p ) {
                                h[p] *= Geom::Affine(mg->gradientTransform) * (Geom::Affine)item->i2dt_affine();
                            }
                            addCurve (item, h[0], h[1], h[2], h[3], corner0, corner1, handle0, handle1, Inkscape::FOR_FILL );

                            // Right line
                            if( j == columns - 1 ) {
                                h = patch.getPointsForSide( 1 );
                                for( guint p = 0; p < 4; ++p ) {
                                    h[p] *= Geom::Affine(mg->gradientTransform) * (Geom::Affine)item->i2dt_affine();
                                }
                                addCurve (item, h[0], h[1], h[2], h[3], corner1, corner2, handle2, handle3, Inkscape::FOR_FILL );
                            }

                            // Bottom line
                            if( i == rows    - 1 ) {
                                h = patch.getPointsForSide( 2 );
                                for( guint p = 0; p < 4; ++p ) {
                                    h[p] *= Geom::Affine(mg->gradientTransform) * (Geom::Affine)item->i2dt_affine();
                                }
                                addCurve (item, h[0], h[1], h[2], h[3], corner2, corner3, handle4, handle5, Inkscape::FOR_FILL );
                            }

                            // Left line
                            h = patch.getPointsForSide( 3 );
                            for( guint p = 0; p < 4; ++p ) {
                                h[p] *= Geom::Affine(mg->gradientTransform) * (Geom::Affine)item->i2dt_affine();
                            }
                            addCurve (item, h[0], h[1], h[2], h[3], corner3, corner0, handle6, handle7, Inkscape::FOR_FILL );
                        }
                    }
                    }
                }
            }
        }

        if (style && (style->stroke.isPaintserver())) {
            SPPaintServer *server = item->style->getStrokePaintServer();
            if (auto gradient = cast<SPGradient>(server)) {
                if (gradient->isSolid() || (gradient->getVector() && gradient->getVector()->isSolid())) {
                    // Suppress "gradientness" of solid paint
                } else if (is<SPLinearGradient>(server)) {
                    addLine(item, getGradientCoords(item, POINT_LG_BEGIN, 0, Inkscape::FOR_STROKE), getGradientCoords(item, POINT_LG_END, 0, Inkscape::FOR_STROKE), Inkscape::FOR_STROKE);
                } else if (is<SPRadialGradient>(server)) {
                    Geom::Point center = getGradientCoords(item, POINT_RG_CENTER, 0, Inkscape::FOR_STROKE);
                    addLine(item, center, getGradientCoords(item, POINT_RG_R1, 0, Inkscape::FOR_STROKE), Inkscape::FOR_STROKE);
                    addLine(item, center, getGradientCoords(item, POINT_RG_R2, 0, Inkscape::FOR_STROKE), Inkscape::FOR_STROKE);
                } else if (is<SPMeshGradient>(server)) {
                    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
                    bool edit_stroke   = (prefs->getBool("/tools/mesh/edit_stroke",   true));

                    if (edit_stroke) {

                    // MESH FIXME: TURN ROUTINE INTO FUNCTION AND CALL FOR BOTH FILL AND STROKE.
                    auto mg = cast<SPMeshGradient>(server);

                    guint rows    = mg->array.patch_rows();
                    guint columns = mg->array.patch_columns();
                    for ( guint i = 0; i < rows; ++i ) {
                        for ( guint j = 0; j < columns; ++j ) {

                            std::vector<Geom::Point> h;

                            SPMeshPatchI patch( &(mg->array.nodes), i, j );

                            // clockwise around patch, used to find corner dragger
                            int corner0 = i * (columns + 1) + j;
                            int corner1 = corner0 + 1;
                            int corner2 = corner1 + columns + 1;
                            int corner3 = corner2 - 1;
                            // clockwise around patch, used to find handle dragger
                            int handle0 = 2*j + i*(2+4*columns);
                            int handle1 = handle0 + 1;
                            int handle2 = j + i*(2+4*columns) + 2*columns + 1;
                            int handle3 = j + i*(2+4*columns) + 3*columns + 2;
                            int handle4 = handle1 + (2+4*columns);
                            int handle5 = handle0 + (2+4*columns);
                            int handle6 = handle3 - 1;
                            int handle7 = handle2 - 1;

                            // Top line
                            h = patch.getPointsForSide( 0 );
                            for( guint p = 0; p < 4; ++p ) {
                                h[p] *= Geom::Affine(mg->gradientTransform) * (Geom::Affine)item->i2dt_affine();
                            }
                            addCurve (item, h[0], h[1], h[2], h[3], corner0, corner1, handle0, handle1, Inkscape::FOR_STROKE);

                            // Right line
                            if( j == columns - 1 ) {
                                h = patch.getPointsForSide( 1 );
                                for( guint p = 0; p < 4; ++p ) {
                                    h[p] *= Geom::Affine(mg->gradientTransform) * (Geom::Affine)item->i2dt_affine();
                                }
                                addCurve (item, h[0], h[1], h[2], h[3], corner1, corner2, handle2, handle3, Inkscape::FOR_STROKE);
                            }

                            // Bottom line
                            if( i == rows    - 1 ) {
                                h = patch.getPointsForSide( 2 );
                                for( guint p = 0; p < 4; ++p ) {
                                    h[p] *= Geom::Affine(mg->gradientTransform) * (Geom::Affine)item->i2dt_affine();
                                }
                                addCurve (item, h[0], h[1], h[2], h[3], corner2, corner3, handle4, handle5, Inkscape::FOR_STROKE);
                            }

                            // Left line
                            h = patch.getPointsForSide( 3 );
                            for( guint p = 0; p < 4; ++p ) {
                                h[p] *= Geom::Affine(mg->gradientTransform) * (Geom::Affine)item->i2dt_affine();
                            }
                            addCurve (item, h[0], h[1], h[2], h[3], corner3, corner0, handle6, handle7,Inkscape::FOR_STROKE);
                        }
                    }
                    }
                }
            }
        }
    }
}

/**
 * Regenerates the levels list from the current selection.
 * Levels correspond to bounding box edges and midpoints.
 */
void GrDrag::updateLevels()
{
    hor_levels.clear();
    vert_levels.clear();

    g_return_if_fail (this->selection != nullptr);

    auto list = this->selection->items();
    for (auto i = list.begin(); i != list.end(); ++i) {
        SPItem *item = *i;
        Geom::OptRect rect = item->desktopVisualBounds();
        if (rect) {
            // Remember the edges of the bbox and the center axis
            hor_levels.push_back(rect->min()[Geom::Y]);
            hor_levels.push_back(rect->max()[Geom::Y]);
            hor_levels.push_back(rect->midpoint()[Geom::Y]);
            vert_levels.push_back(rect->min()[Geom::X]);
            vert_levels.push_back(rect->max()[Geom::X]);
            vert_levels.push_back(rect->midpoint()[Geom::X]);
        }
    }
}

void GrDrag::selected_reverse_vector()
{
    if (selected.empty())
        return;

    for(auto draggable : (*(selected.begin()))->draggables) {
        sp_item_gradient_reverse_vector (draggable->item, draggable->fill_or_stroke);
    }
}

void GrDrag::selected_move_nowrite(double x, double y, bool scale_radial)
{
    selected_move (x, y, false, scale_radial);
}

void GrDrag::selected_move(double x, double y, bool write_repr, bool scale_radial)
{
    if (selected.empty())
        return;

    bool did = false;

    auto delta = Geom::Point(x, y);

    auto prefs = Inkscape::Preferences::get();
    bool const rotated = prefs->getBool("/options/moverotated/value", true);
    if (rotated) {
        delta *= Geom::Rotate(-desktop->current_rotation());
    }

    for(auto d : selected) {
        if (!d->isA(POINT_LG_MID) && !d->isA(POINT_RG_MID1) && !d->isA(POINT_RG_MID2)) {
            // if this is an endpoint,

            // Moving an rg center moves its focus and radii as well.
            // therefore, if this is a focus or radius and if selection
            // contains the center as well, do not move this one
            if (d->isA(POINT_RG_R1) || d->isA(POINT_RG_R2) ||
                (d->isA(POINT_RG_FOCUS) && !d->isA(POINT_RG_CENTER))) {
                bool skip_radius_with_center = false;
                for(auto d_new : selected) {
                    if (d_new->isA (( d->draggables[0])->item,
                                    POINT_RG_CENTER,
                                    0,
                                    (d->draggables[0])->fill_or_stroke)) {
                        // FIXME: here we take into account only the first draggable!
                        skip_radius_with_center = true;
                    }
                }
                if (skip_radius_with_center)
                    continue;
            }

            did = true;
            Geom::Point p_old = d->point;
            d->point += delta;
            d->point_original = d->point;
            d->knot->moveto(d->point);

            d->fireDraggables (write_repr, scale_radial);
            d->moveMeshHandles( p_old, MG_NODE_NO_SCALE );
            d->updateDependencies(write_repr);
        }
    }

    if (write_repr && did) {
        // we did an undoable action
        DocumentUndo::maybeDone(desktop->getDocument(), "grmoveh", _("Move gradient handle(s)"), INKSCAPE_ICON("color-gradient"));
        return;
    }

    if (!did) { // none of the end draggers are selected, so let's try to move the mids

        GrDragger *dragger = *(selected.begin());
        // a midpoint dragger can (logically) only contain one GrDraggable
        GrDraggable *draggable = dragger->draggables[0];

        Geom::Point begin(0,0), end(0,0);
        Geom::Point low_lim(0,0), high_lim(0,0);

        SPObject *server = draggable->getServer();
        std::vector<GrDragger *> moving;
        gr_midpoint_limits(dragger, server, &begin, &end, &low_lim, &high_lim, moving);

        Geom::LineSegment ls(low_lim, high_lim);
        Geom::Point p = ls.pointAt(ls.nearestTime(dragger->point + Geom::Point(x,y)));
        Geom::Point displacement = p - dragger->point;

        for(auto drg : moving) {
            SPKnot *drgknot = drg->knot;
            drg->point += displacement;
            drgknot->moveto(drg->point);
            drg->fireDraggables (true);
            drg->updateDependencies(true);
            did = true;
        }

        if (write_repr && did) {
            // we did an undoable action
            DocumentUndo::maybeDone(desktop->getDocument(), "grmovem", _("Move gradient mid stop(s)"), INKSCAPE_ICON("color-gradient"));
        }
    }
}

void GrDrag::selected_move_screen(double x, double y)
{
    gdouble zoom = desktop->current_zoom();
    gdouble zx = x / zoom;
    gdouble zy = y / zoom;

    selected_move (zx, zy);
}

/**
 * Handle arrow key events
 * @param event Event with type GDK_KEY_PRESS
 * @return True if the event was handled, false otherwise
 */
bool GrDrag::key_press_handler(Inkscape::KeyPressEvent const &event)
{
    if (mod_ctrl(event)) {
        return false;
    }

    auto keyval = Inkscape::UI::Tools::get_latin_keyval(event);
    double x_dir = 0;
    double y_dir = 0;

    switch (keyval) {
        case GDK_KEY_Left: // move handle left
        case GDK_KEY_KP_Left:
        case GDK_KEY_KP_4:
            x_dir = -1;
            break;

        case GDK_KEY_Up: // move handle up
        case GDK_KEY_KP_Up:
        case GDK_KEY_KP_8:
            y_dir = 1;
            break;

        case GDK_KEY_Right: // move handle right
        case GDK_KEY_KP_Right:
        case GDK_KEY_KP_6:
            x_dir = 1;
            break;

        case GDK_KEY_Down: // move handle down
        case GDK_KEY_KP_Down:
        case GDK_KEY_KP_2:
            y_dir = -1;
            break;

        default:
            return false;
    }

    y_dir *= -desktop->yaxisdir();

    gint mul = 1 + Inkscape::UI::Tools::gobble_key_events(keyval, 0); // with any mask

    if (mod_shift(event)) {
        mul *= 10;
    }

    if (mod_alt(event)) {
        selected_move_screen(mul * x_dir, mul * y_dir);
    } else {
        auto *prefs = Inkscape::Preferences::get();
        auto nudge = prefs->getDoubleLimited("/options/nudgedistance/value", 2, 0, 1000, "px"); // in px

        mul *= nudge;
        selected_move(mul * x_dir, mul * y_dir);
    }

    return true;
}

/**
 * Select the knot next to the last selected one and deselect all other selected.
 */
GrDragger *GrDrag::select_next()
{
    GrDragger *d = nullptr;
    if (selected.empty() || (++find(draggers.begin(),draggers.end(),*(selected.begin())))==draggers.end()) {
        if (!draggers.empty())
            d = draggers[0];
    } else {
        d = *(++find(draggers.begin(),draggers.end(),*(selected.begin())));
    }
    if (d)
        setSelected (d);
    return d;
}

/**
 * Select the knot previous from the last selected one and deselect all other selected.
 */
GrDragger *GrDrag::select_prev()
{
    GrDragger *d = nullptr;
    if (selected.empty() || draggers[0] == (*(selected.begin()))) {
        if (!draggers.empty())
            d = draggers[draggers.size()-1];
    } else {
        d = *(--find(draggers.begin(),draggers.end(),*(selected.begin())));
    }
    if (d)
        setSelected (d);
    return d;
}


// FIXME: i.m.o. an ugly function that I just made to work, but... aargh! (Johan)
void GrDrag::deleteSelected(bool just_one)
{
    if (selected.empty()) return;

    SPDocument *document = nullptr;

    struct StructStopInfo {
        SPStop * spstop;
        GrDraggable * draggable;
        SPGradient * gradient;
        SPGradient * vector;
    };

    std::vector<SPStop *> midstoplist;// list of stops that must be deleted (will be deleted first)
    std::vector<StructStopInfo *> endstoplist;// list of stops that must be deleted

    while (!selected.empty()) {
        GrDragger *dragger = *(selected.begin());
        for(auto draggable : dragger->draggables) {
            SPGradient *gradient = getGradient(draggable->item, draggable->fill_or_stroke);
            SPGradient *vector   = sp_gradient_get_forked_vector_if_necessary (gradient, false);

            switch (draggable->point_type) {
                case POINT_LG_MID:
                case POINT_RG_MID1:
                case POINT_RG_MID2:
                    {
                        SPStop *stop = sp_get_stop_i(vector, draggable->point_i);
                        // check if already present in list. (e.g. when both RG_MID1 and RG_MID2 were selected)
                        bool present = false;
                        for (auto i:midstoplist) {
                            if ( i == stop ) {
                                present = true;
                                break; // no need to search further.
                            }
                        }
                        if (!present)
                            midstoplist.push_back(stop);
                    }
                    break;
                case POINT_LG_BEGIN:
                case POINT_LG_END:
                case POINT_RG_CENTER:
                case POINT_RG_R1:
                case POINT_RG_R2:
                    {
                        SPStop *stop = nullptr;
                        if ( (draggable->point_type == POINT_LG_BEGIN) || (draggable->point_type == POINT_RG_CENTER) ) {
                            stop = vector->getFirstStop();
                        } else {
                            stop = sp_last_stop(vector);
                        }
                        if (stop) {
                            StructStopInfo *stopinfo = new StructStopInfo;
                            stopinfo->spstop = stop;
                            stopinfo->draggable = draggable;
                            stopinfo->gradient = gradient;
                            stopinfo->vector = vector;
                            // check if already present in list. (e.g. when both R1 and R2 were selected)
                            bool present = false;
                            for (auto i : endstoplist) {
                                if ( i->spstop == stopinfo->spstop ) {
                                    present = true;
                                    break; // no need to search further.
                                }
                            }
                            if (!present)
                                endstoplist.push_back(stopinfo);
                            else
                                delete stopinfo;
                        }
                    }
                    break;

                default:
                    break;
            }
        }
        selected.erase(dragger);
        if ( just_one ) break; // iterate once if just_one is set.
    }
    for (auto stop:midstoplist) {
        document = stop->document;
        Inkscape::XML::Node * parent = stop->getRepr()->parent();
        parent->removeChild(stop->getRepr());
    }

    for (auto stopinfo:endstoplist) {
        document = stopinfo->spstop->document;

        // 2 is the minimum, cannot delete more than that without deleting the whole vector
        // cannot use vector->vector.stops.size() because the vector might be invalidated by deletion of a midstop
        // manually count the children, don't know if there already exists a function for this...
        int len = 0;
        for (auto& child: stopinfo->vector->children)
        {
            if ( is<SPStop>(&child) ) {
                len ++;
            }
        }
        if (len > 2)
        {
            switch (stopinfo->draggable->point_type) {
                case POINT_LG_BEGIN:
                    {
                        stopinfo->vector->getRepr()->removeChild(stopinfo->spstop->getRepr());

                        auto lg = cast<SPLinearGradient>(stopinfo->gradient);
                        Geom::Point oldbegin = Geom::Point (lg->x1.computed, lg->y1.computed);
                        Geom::Point end = Geom::Point (lg->x2.computed, lg->y2.computed);
                        SPStop *stop = stopinfo->vector->getFirstStop();
                        gdouble offset = stop->offset;
                        Geom::Point newbegin = oldbegin + offset * (end - oldbegin);
                        lg->x1.computed = newbegin[Geom::X];
                        lg->y1.computed = newbegin[Geom::Y];

                        Inkscape::XML::Node *repr = stopinfo->gradient->getRepr();
                        repr->setAttributeSvgDouble("x1", lg->x1.computed);
                        repr->setAttributeSvgDouble("y1", lg->y1.computed);
                        stop->offset = 0;
                        stop->getRepr()->setAttributeCssDouble("offset", 0);

                        // iterate through midstops to set new offset values such that they won't move on canvas.
                        SPStop *laststop = sp_last_stop(stopinfo->vector);
                        stop = stop->getNextStop();
                        while ( stop != laststop ) {
                            stop->offset = (stop->offset - offset)/(1 - offset);
                            stop->getRepr()->setAttributeCssDouble("offset", stop->offset);
                            stop = stop->getNextStop();
                        }
                    }
                    break;
                case POINT_LG_END:
                    {
                        stopinfo->vector->getRepr()->removeChild(stopinfo->spstop->getRepr());

                        auto lg = cast<SPLinearGradient>(stopinfo->gradient);
                        Geom::Point begin = Geom::Point (lg->x1.computed, lg->y1.computed);
                        Geom::Point oldend = Geom::Point (lg->x2.computed, lg->y2.computed);
                        SPStop *laststop = sp_last_stop(stopinfo->vector);
                        gdouble offset = laststop->offset;
                        Geom::Point newend = begin + offset * (oldend - begin);
                        lg->x2.computed = newend[Geom::X];
                        lg->y2.computed = newend[Geom::Y];

                        Inkscape::XML::Node *repr = stopinfo->gradient->getRepr();
                        repr->setAttributeSvgDouble("x2", lg->x2.computed);
                        repr->setAttributeSvgDouble("y2", lg->y2.computed);
                        laststop->offset = 1;
                        laststop->getRepr()->setAttributeCssDouble("offset", 1);

                        // iterate through midstops to set new offset values such that they won't move on canvas.
                        SPStop *stop = stopinfo->vector->getFirstStop();
                        stop = stop->getNextStop();
                        while ( stop != laststop ) {
                            stop->offset = stop->offset / offset;
                            stop->getRepr()->setAttributeCssDouble("offset", stop->offset);
                            stop = stop->getNextStop();
                        }
                    }
                    break;
                case POINT_RG_CENTER:
                    {
                        SPStop *newfirst = stopinfo->spstop->getNextStop();
                        if (newfirst) {
                            newfirst->offset = 0;
                            newfirst->getRepr()->setAttributeCssDouble("offset", 0);
                        }
                        stopinfo->vector->getRepr()->removeChild(stopinfo->spstop->getRepr());
                    }
                    break;
                case POINT_RG_R1:
                case POINT_RG_R2:
                    {
                        stopinfo->vector->getRepr()->removeChild(stopinfo->spstop->getRepr());

                        auto rg = cast<SPRadialGradient>(stopinfo->gradient);
                        double oldradius = rg->r.computed;
                        SPStop *laststop = sp_last_stop(stopinfo->vector);
                        gdouble offset = laststop->offset;
                        double newradius = offset * oldradius;
                        rg->r.computed = newradius;

                        Inkscape::XML::Node *repr = rg->getRepr();
                        repr->setAttributeSvgDouble("r", rg->r.computed);
                        laststop->offset = 1;
                        laststop->getRepr()->setAttributeCssDouble("offset", 1);

                        // iterate through midstops to set new offset values such that they won't move on canvas.
                        SPStop *stop = stopinfo->vector->getFirstStop();
                        stop = stop->getNextStop();
                        while ( stop != laststop ) {
                            stop->offset = stop->offset / offset;
                            stop->getRepr()->setAttributeCssDouble("offset", stop->offset);
                            stop = stop->getNextStop();
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        else
        { // delete the gradient from the object. set fill to unset  FIXME: set to fill of unselected node?
            SPCSSAttr *css = sp_repr_css_attr_new ();

            // stopinfo->spstop is the selected stop
            Inkscape::XML::Node *unselectedrepr = stopinfo->vector->getRepr()->firstChild();
            if (unselectedrepr == stopinfo->spstop->getRepr() ) {
                unselectedrepr = unselectedrepr->next();
            }

            if (unselectedrepr == nullptr) {
                if (stopinfo->draggable->fill_or_stroke == Inkscape::FOR_FILL) {
                    sp_repr_css_unset_property (css, "fill");
                } else {
                    sp_repr_css_unset_property (css, "stroke");
                }
            } else {
                SPCSSAttr *stopcss = sp_repr_css_attr(unselectedrepr, "style");
                if (stopinfo->draggable->fill_or_stroke == Inkscape::FOR_FILL) {
                    sp_repr_css_set_property(css, "fill", sp_repr_css_property(stopcss, "stop-color", "inkscape:unset"));
                    sp_repr_css_set_property(css, "fill-opacity", sp_repr_css_property(stopcss, "stop-opacity", "1"));
                } else {
                    sp_repr_css_set_property(css, "stroke", sp_repr_css_property(stopcss, "stop-color", "inkscape:unset"));
                    sp_repr_css_set_property(css, "stroke-opacity", sp_repr_css_property(stopcss, "stop-opacity", "1"));
                }
                sp_repr_css_attr_unref (stopcss);
            }

            sp_repr_css_change(stopinfo->draggable->item->getRepr(), css, "style");
            sp_repr_css_attr_unref (css);
        }

        delete stopinfo;
    }

    if (document) {
        DocumentUndo::done( document, _("Delete gradient stop(s)"), INKSCAPE_ICON("color-gradient"));
    }
}


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
