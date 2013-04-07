#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifndef __SP_MISSING_GLYPH_H__
#define __SP_MISSING_GLYPH_H__

/*
 * SVG <missing-glyph> element implementation
 *
 * Authors:
 *    Felipe C. da S. Sanches <juca@members.fsf.org>
 *
 * Copyright (C) 2008 Felipe C. da S. Sanches
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include "sp-object.h"

#define SP_MISSING_GLYPH(obj) ((SPMissingGlyph*)obj)
#define SP_IS_MISSING_GLYPH(obj) (dynamic_cast<const SPMissingGlyph*>((SPObject*)obj))

class SPMissingGlyph : public SPObject {
public:
	SPMissingGlyph();
	virtual ~SPMissingGlyph();

    char* d;
    double horiz_adv_x;
    double vert_origin_x;
    double vert_origin_y;
    double vert_adv_y;

    virtual void build(SPDocument* doc, Inkscape::XML::Node* repr);
	virtual void release();
	virtual void set(unsigned int key, const gchar* value);
	virtual Inkscape::XML::Node* write(Inkscape::XML::Document* doc, Inkscape::XML::Node* repr, guint flags);
};

#endif //#ifndef __SP_MISSING_GLYPH_H__
