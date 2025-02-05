// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Inkscape::Extension::Internal::GimpGrad implementation
 */

/*
 * Authors:
 *   Ted Gould <ted@gould.cx>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004-2005 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <color-rgba.h>
#include "io/sys.h"
#include "extension/system.h"
#include "svg/css-ostringstream.h"
#include "svg/svg-color.h"

#include "gimpgrad.h"
#include "streq.h"
#include "strneq.h"
#include "document.h"
#include "extension/extension.h"

namespace Inkscape::Extension::Internal {

bool GimpGrad::load(Inkscape::Extension::Extension *)
{
    return true;
}

void GimpGrad::unload(Inkscape::Extension::Extension *)
{
    return;
}

static void append_css_num(Glib::ustring &str, double const num)
{
    CSSOStringStream stream;
    stream << num;
    str += stream.str();
}

/**
    \brief  A function to turn a color into a gradient stop
    \param  in_color  The color for the stop
    \param  location  Where the stop is placed in the gradient
    \return The text that is the stop.  Full SVG containing the element.

    This function encapsulates all of the translation of the ColorRGBA
    and the location into the gradient.  It is really pretty simple except
    that the ColorRGBA is in floats that are 0 to 1 and the SVG wants
    hex values from 0 to 255 for color.  Otherwise mostly this is just
    turning the values into strings and returning it.
*/
static Glib::ustring stop_svg(ColorRGBA const in_color, double const location)
{
    Glib::ustring ret("<stop stop-color=\"");

    char stop_color_css[16];
    sp_svg_write_color(stop_color_css, sizeof(stop_color_css), in_color.getIntValue());
    ret += stop_color_css;
    ret += '"';

    if (in_color[3] != 1) {
        ret += " stop-opacity=\"";
        append_css_num(ret, in_color[3]);
        ret += '"';
    }
    ret += " offset=\"";
    append_css_num(ret, location);
    ret += "\"/>\n";
    return ret;
}

/**
    \brief  Actually open the gradient and turn it into an SPDocument
    \param  module    The input module being used
    \param  filename  The filename of the gradient to be opened
    \return A Document with the gradient in it.

    GIMP gradients are pretty simple (atleast the newer format, this
    function does not handle the old one yet).  They start out with
    the like "GIMP Gradient", then name it, and tell how many entries
    there are.  This function currently ignores the name and the number
    of entries just reading until it fails.

    The other small piece of trickery here is that GIMP gradients define
    a left position, right position and middle position.  SVG gradients
    have no middle position in them.  In order to handle this case the
    left and right colors are averaged in a linear manner and the middle
    position is used for that color.

    That is another point, the GIMP gradients support many different types
    of gradients -- linear being the most simple.  This plugin assumes
    that they are all linear.  Most GIMP gradients are done this way,
    but it is possible to encounter more complex ones -- which won't be
    handled correctly.

    The one optimization that this plugin makes that if the right side
    of the previous segment is the same color as the left side of the
    current segment, then the second one is dropped.  This is often
    done in GIMP gradients and they are not necissary in SVG.

    What this function does is build up an SVG document with a single
    linear gradient in it with all the stops of the colors in the GIMP
    gradient that is passed in.  This document is then turned into a
    document using the \c sp_document_from_mem.  That is then returned
    to Inkscape.
*/
std::unique_ptr<SPDocument> GimpGrad::open(Inkscape::Extension::Input *, char const *filename)
{
    Inkscape::IO::dump_fopen_call(filename, "I");
    FILE *gradient = Inkscape::IO::fopen_utf8name(filename, "r");
    if (gradient == nullptr) {
        return nullptr;
    }

    {
        char tempstr[1024];
        if (fgets(tempstr, 1024, gradient) == nullptr) {
            // std::cout << "Seems that the read failed" << std::endl;
            goto error;
        }
        if (!streq(tempstr, "GIMP Gradient\n")) {
            // std::cout << "This doesn't appear to be a GIMP gradient" << std::endl;
            goto error;
        }

        /* Name field. */
        if (fgets(tempstr, 1024, gradient) == nullptr) {
            // std::cout << "Seems that the second read failed" << std::endl;
            goto error;
        }
        if (!strneq(tempstr, "Name: ", 6)) {
            goto error;
        }
        /* Handle very long names.  (And also handle nul bytes gracefully: don't use strlen.) */
        while (memchr(tempstr, '\n', sizeof(tempstr) - 1) == nullptr) {
            if (fgets(tempstr, sizeof(tempstr), gradient) == nullptr) {
                goto error;
            }
        }

        /* n. segments */
        if (fgets(tempstr, 1024, gradient) == nullptr) {
            // std::cout << "Seems that the third read failed" << std::endl;
            goto error;
        }
        char *endptr = nullptr;
        long const n_segs = strtol(tempstr, &endptr, 10);
        if ((*endptr != '\n')
            || n_segs < 1) {
            /* SVG gradients are allowed to have zero stops (treated as `none'), but gimp 2.2
             * requires at least one segment (i.e. at least two stops) (see gimp_gradient_load in
             * gimpgradient-load.c).  We try to use the same error handling as gimp, so that
             * .ggr files that work in one program work in both programs. */
            goto error;
        }

        ColorRGBA prev_color(-1.0, -1.0, -1.0, -1.0);
        Glib::ustring outsvg("<svg><defs><linearGradient>\n");
        long n_segs_found = 0;
        double prev_right = 0.0;
        while (fgets(tempstr, 1024, gradient) != nullptr) {
            double dbls[3 + 4 + 4];
            gchar *p = tempstr;
            for (double & dbl : dbls) {
                gchar *end = nullptr;
                double const xi = g_ascii_strtod(p, &end);
                if (!end || end == p || !g_ascii_isspace(*end)) {
                    goto error;
                }
                if (xi < 0 || 1 < xi) {
                    goto error;
                }
                dbl = xi;
                p = end + 1;
            }

            double const left = dbls[0];
            if (left != prev_right) {
                goto error;
            }
            double const middle = dbls[1];
            if (!(left <= middle)) {
                goto error;
            }
            double const right = dbls[2];
            if (!(middle <= right)) {
                goto error;
            }

            ColorRGBA const leftcolor(dbls[3], dbls[4], dbls[5], dbls[6]);
            ColorRGBA const rightcolor(dbls[7], dbls[8], dbls[9], dbls[10]);
            g_assert(11 == G_N_ELEMENTS(dbls));

            /* Interpolation enums: curve shape and colour space. */
            {
                /* TODO: Currently we silently ignore type & color, assuming linear interpolation in
                 * sRGB space (or whatever the default in SVG is).  See gimp/app/core/gimpgradient.c
                 * for how gimp uses these.  We could use gimp functions to sample at a few points, and
                 * add some intermediate stops to convert to the linear/sRGB interpolation */
                int type; /* enum: linear, curved, sine, sphere increasing, sphere decreasing. */
                int color_interpolation; /* enum: rgb, hsv anticlockwise, hsv clockwise. */
                if (sscanf(p, "%8d %8d", &type, &color_interpolation) != 2) {
                    continue;
                }
            }

            if (prev_color != leftcolor) {
                outsvg += stop_svg(leftcolor, left);
            }
            if (fabs(middle - .5 * (left + right)) > 1e-4) {
                outsvg += stop_svg(leftcolor.average(rightcolor), middle);
            }
            outsvg += stop_svg(rightcolor, right);

            prev_color = rightcolor;
            prev_right = right;
            ++n_segs_found;
        }
        if (prev_right != 1.0) {
            goto error;
        }

        if (n_segs_found != n_segs) {
            goto error;
        }

        outsvg += "</linearGradient></defs></svg>";

        // std::cout << "SVG Output: " << outsvg << std::endl;

        fclose(gradient);

        return SPDocument::createNewDocFromMem(outsvg.raw(), true);
    }

error:
    fclose(gradient);
    return nullptr;
}

#include "clear-n_.h"

void GimpGrad::init()
{
    // clang-format off
    Inkscape::Extension::build_from_mem(
        "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
            "<name>" N_("GIMP Gradients") "</name>\n"
            "<id>org.inkscape.input.gimpgrad</id>\n"
            "<input>\n"
                "<extension>.ggr</extension>\n"
                "<mimetype>application/x-gimp-gradient</mimetype>\n"
                "<filetypename>" N_("GIMP Gradient (*.ggr)") "</filetypename>\n"
                "<filetypetooltip>" N_("Gradients used in GIMP") "</filetypetooltip>\n"
            "</input>\n"
        "</inkscape-extension>\n", std::make_unique<GimpGrad>());
    // clang-format on
    return;
}

} // namespace Inkscape::Extension::Internal

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
