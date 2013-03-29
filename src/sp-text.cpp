/*
 * SVG <text> and <tspan> implementation
 *
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

/*
 * fixme:
 *
 * These subcomponents should not be items, or alternately
 * we have to invent set of flags to mark, whether standard
 * attributes are applicable to given item (I even like this
 * idea somewhat - Lauris)
 *
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <2geom/affine.h>
#include <libnrtype/FontFactory.h>
#include <libnrtype/font-instance.h>
#include <libnrtype/font-style-to-pos.h>

#include <glibmm/i18n.h>
#include "svg/svg.h"
#include "svg/stringstream.h"
#include "display/drawing-text.h"
#include "attributes.h"
#include "document.h"
#include "preferences.h"
#include "desktop-handles.h"
#include "sp-namedview.h"
#include "style.h"
#include "inkscape.h"
#include "sp-metrics.h"
#include "xml/quote.h"
#include "xml/repr.h"
#include "mod360.h"
#include "sp-title.h"
#include "sp-desc.h"
#include "sp-text.h"

#include "sp-textpath.h"
#include "sp-tref.h"
#include "sp-tspan.h"

#include "text-editing.h"
#include "unit-constants.h"

/*#####################################################
#  SPTEXT
#####################################################*/

static void sp_text_class_init (SPTextClass *classname);
static void sp_text_init (SPText *text);
static void sp_text_release (SPObject *object);

static void sp_text_build (SPObject *object, SPDocument *document, Inkscape::XML::Node *repr);
static void sp_text_set (SPObject *object, unsigned key, gchar const *value);
static void sp_text_child_added (SPObject *object, Inkscape::XML::Node *rch, Inkscape::XML::Node *ref);
static void sp_text_remove_child (SPObject *object, Inkscape::XML::Node *rch);
static void sp_text_update (SPObject *object, SPCtx *ctx, guint flags);
static void sp_text_modified (SPObject *object, guint flags);
static Inkscape::XML::Node *sp_text_write (SPObject *object, Inkscape::XML::Document *doc, Inkscape::XML::Node *repr, guint flags);

static Geom::OptRect sp_text_bbox(SPItem const *item, Geom::Affine const &transform, SPItem::BBoxType type);
static Inkscape::DrawingItem *sp_text_show (SPItem *item, Inkscape::Drawing &drawing, unsigned key, unsigned flags);
static void sp_text_hide (SPItem *item, unsigned key);
static char *sp_text_description (SPItem *item);
static void sp_text_snappoints(SPItem const *item, std::vector<Inkscape::SnapCandidatePoint> &p, Inkscape::SnapPreferences const *snapprefs);
static Geom::Affine sp_text_set_transform(SPItem *item, Geom::Affine const &xform);
static void sp_text_print (SPItem *item, SPPrintContext *gpc);

static SPItemClass *text_parent_class;

GType
sp_text_get_type ()
{
    static GType type = 0;
    if (!type) {
        GTypeInfo info = {
            sizeof (SPTextClass),
            NULL,    /* base_init */
            NULL,    /* base_finalize */
            (GClassInitFunc) sp_text_class_init,
            NULL,    /* class_finalize */
            NULL,    /* class_data */
            sizeof (SPText),
            16,    /* n_preallocs */
            (GInstanceInitFunc) sp_text_init,
            NULL,    /* value_table */
        };
        type = g_type_register_static (SP_TYPE_ITEM, "SPText", &info, (GTypeFlags)0);
    }
    return type;
}

static void
sp_text_class_init (SPTextClass *classname)
{
    SPObjectClass *sp_object_class = (SPObjectClass *) classname;
    SPItemClass *item_class = (SPItemClass *) classname;

    text_parent_class = (SPItemClass*)g_type_class_ref (SP_TYPE_ITEM);

//    sp_object_class->release = sp_text_release;
//    //sp_object_class->build = sp_text_build;
//    sp_object_class->set = sp_text_set;
//    sp_object_class->child_added = sp_text_child_added;
//    sp_object_class->remove_child = sp_text_remove_child;
//    sp_object_class->update = sp_text_update;
//    sp_object_class->modified = sp_text_modified;
//    sp_object_class->write = sp_text_write;

//    item_class->bbox = sp_text_bbox;
//    item_class->show = sp_text_show;
//    item_class->hide = sp_text_hide;
//    item_class->description = sp_text_description;
//    item_class->snappoints = sp_text_snappoints;
//    item_class->set_transform = sp_text_set_transform;
//    item_class->print = sp_text_print;
}

CText::CText(SPText* text) : CItem(text) {
	this->sptext = text;
}

CText::~CText() {
}

static void
sp_text_init (SPText *text)
{
	text->ctext = new CText(text);
	text->citem = text->ctext;
	text->cobject = text->ctext;

    new (&text->layout) Inkscape::Text::Layout;
    new (&text->attributes) TextTagAttributes;
}

void CText::onRelease() {
	SPText* object = this->sptext;

    SPText *text = SP_TEXT(object);
    text->attributes.~TextTagAttributes();
    text->layout.~Layout();

    CItem::onRelease();
}

static void
sp_text_release (SPObject *object)
{
	((SPText*)object)->ctext->onRelease();
}

void CText::onBuild(SPDocument *doc, Inkscape::XML::Node *repr) {
	SPText* object = this->sptext;

    object->readAttr( "x" );
    object->readAttr( "y" );
    object->readAttr( "dx" );
    object->readAttr( "dy" );
    object->readAttr( "rotate" );

    CItem::onBuild(doc, repr);

    object->readAttr( "sodipodi:linespacing" );    // has to happen after the styles are read
}

static void
sp_text_build (SPObject *object, SPDocument *doc, Inkscape::XML::Node *repr)
{
	((SPText*)object)->ctext->onBuild(doc, repr);
}

void CText::onSet(unsigned int key, const gchar* value) {
	SPText* object = this->sptext;

    SPText *text = SP_TEXT (object);

    if (text->attributes.readSingleAttribute(key, value)) {
        object->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    } else {
        switch (key) {
            case SP_ATTR_SODIPODI_LINESPACING:
                // convert deprecated tag to css
                if (value) {
                    text->style->line_height.set = TRUE;
                    text->style->line_height.inherit = FALSE;
                    text->style->line_height.normal = FALSE;
                    text->style->line_height.unit = SP_CSS_UNIT_PERCENT;
                    text->style->line_height.value = text->style->line_height.computed = sp_svg_read_percentage (value, 1.0);
                }
                object->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_TEXT_LAYOUT_MODIFIED_FLAG);
                break;
            default:
                CItem::onSet(key, value);
                break;
        }
    }
}

static void
sp_text_set(SPObject *object, unsigned key, gchar const *value)
{
	((SPText*)object)->ctext->onSet(key, value);
}

void CText::onChildAdded(Inkscape::XML::Node *rch, Inkscape::XML::Node *ref) {
	SPText* object = this->sptext;

    SPText *text = SP_TEXT (object);

    CItem::onChildAdded(rch, ref);

    text->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_TEXT_CONTENT_MODIFIED_FLAG | SP_TEXT_LAYOUT_MODIFIED_FLAG);
}

static void
sp_text_child_added (SPObject *object, Inkscape::XML::Node *rch, Inkscape::XML::Node *ref)
{
	((SPText*)object)->ctext->onChildAdded(rch, ref);
}

void CText::onRemoveChild(Inkscape::XML::Node *rch) {
	SPText* object = this->sptext;

    SPText *text = SP_TEXT (object);

    CItem::onRemoveChild(rch);

    text->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_TEXT_CONTENT_MODIFIED_FLAG | SP_TEXT_LAYOUT_MODIFIED_FLAG);
}

static void
sp_text_remove_child (SPObject *object, Inkscape::XML::Node *rch)
{
	((SPText*)object)->ctext->onRemoveChild(rch);
}

void CText::onUpdate(SPCtx *ctx, guint flags) {
	SPText* object = this->sptext;

    SPText *text = SP_TEXT (object);

    CItem::onUpdate(ctx, flags);

    guint cflags = (flags & SP_OBJECT_MODIFIED_CASCADE);
    if (flags & SP_OBJECT_MODIFIED_FLAG) cflags |= SP_OBJECT_PARENT_MODIFIED_FLAG;


    // Create temporary list of children
    GSList *l = NULL;
    for (SPObject *child = object->firstChild() ; child ; child = child->getNext() ) {
        sp_object_ref(child, object);
        l = g_slist_prepend (l, child);
    }
    l = g_slist_reverse (l);
    while (l) {
        SPObject *child = reinterpret_cast<SPObject*>(l->data); // We just built this list, so cast is safe.
        l = g_slist_remove (l, child);
        if (cflags || (child->uflags & (SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_CHILD_MODIFIED_FLAG))) {
            /* fixme: Do we need transform? */
            child->updateDisplay(ctx, cflags);
        }
        sp_object_unref(child, object);
    }
    if (flags & ( SP_OBJECT_STYLE_MODIFIED_FLAG |
                  SP_OBJECT_CHILD_MODIFIED_FLAG |
                  SP_TEXT_LAYOUT_MODIFIED_FLAG   ) )
    {
        /* fixme: It is not nice to have it here, but otherwise children content changes does not work */
        /* fixme: Even now it may not work, as we are delayed */
        /* fixme: So check modification flag everywhere immediate state is used */
        text->rebuildLayout();

        Geom::OptRect paintbox = text->geometricBounds();
        for (SPItemView* v = text->display; v != NULL; v = v->next) {
            Inkscape::DrawingGroup *g = dynamic_cast<Inkscape::DrawingGroup *>(v->arenaitem);
            text->_clearFlow(g);
            g->setStyle(object->style);
            // pass the bbox of the text object as paintbox (used for paintserver fills)
            text->layout.show(g, paintbox);
        }
    }
}

static void sp_text_update(SPObject *object, SPCtx *ctx, guint flags)
{
	((SPText*)object)->ctext->onUpdate(ctx, flags);
}

void CText::onModified(guint flags) {
	SPText* object = this->sptext;

	// CPPIFY: This doesn't make no sense.
	// CObject::onModified is pure and CItem doesn't override this method. What was the idea behind these lines?
//	if (((SPObjectClass *) text_parent_class)->modified) {
//		((SPObjectClass *) text_parent_class)->modified (object, flags);
//	}
//	CItem::onModified(flags);

    guint cflags = (flags & SP_OBJECT_MODIFIED_CASCADE);
    if (flags & SP_OBJECT_MODIFIED_FLAG) {
        cflags |= SP_OBJECT_PARENT_MODIFIED_FLAG;
    }

    // FIXME: all that we need to do here is to call setStyle, to set the changed
    // style, but there's no easy way to access the drawing glyphs or texts corresponding to a
    // text object. Therefore we do here the same as in _update, that is, destroy all items
    // and create new ones. This is probably quite wasteful.
    if (flags & ( SP_OBJECT_STYLE_MODIFIED_FLAG )) {
        SPText *text = SP_TEXT (object);
        Geom::OptRect paintbox = text->geometricBounds();
        for (SPItemView* v = text->display; v != NULL; v = v->next) {
            Inkscape::DrawingGroup *g = dynamic_cast<Inkscape::DrawingGroup *>(v->arenaitem);
            text->_clearFlow(g);
            g->setStyle(object->style);
            text->layout.show(g, paintbox);
        }
    }

    // Create temporary list of children
    GSList *l = NULL;
    for (SPObject *child = object->firstChild() ; child ; child = child->getNext() ) {
        sp_object_ref(child, object);
        l = g_slist_prepend (l, child);
    }
    l = g_slist_reverse (l);
    while (l) {
        SPObject *child = reinterpret_cast<SPObject*>(l->data); // We just built this list, so cast is safe.
        l = g_slist_remove (l, child);
        if (cflags || (child->mflags & (SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_CHILD_MODIFIED_FLAG))) {
            child->emitModified(cflags);
        }
        sp_object_unref(child, object);
    }
}

static void sp_text_modified(SPObject *object, guint flags)
{
	((SPText*)object)->ctext->onModified(flags);
}

Inkscape::XML::Node *CText::onWrite(Inkscape::XML::Document *xml_doc, Inkscape::XML::Node *repr, guint flags) {
	SPText* object = this->sptext;

    SPText *text = SP_TEXT (object);

    if (flags & SP_OBJECT_WRITE_BUILD) {
        if (!repr) {
            repr = xml_doc->createElement("svg:text");
        }
        GSList *l = NULL;
        for (SPObject *child = object->firstChild() ; child ; child = child->getNext() ) {
            if (SP_IS_TITLE(child) || SP_IS_DESC(child)) {
                continue;
            }
            Inkscape::XML::Node *crepr = NULL;
            if (SP_IS_STRING(child)) {
                crepr = xml_doc->createTextNode(SP_STRING(child)->string.c_str());
            } else {
                crepr = child->updateRepr(xml_doc, NULL, flags);
            }
            if (crepr) {
                l = g_slist_prepend (l, crepr);
            }
        }
        while (l) {
            repr->addChild((Inkscape::XML::Node *) l->data, NULL);
            Inkscape::GC::release((Inkscape::XML::Node *) l->data);
            l = g_slist_remove (l, l->data);
        }
    } else {
        for (SPObject *child = object->firstChild() ; child ; child = child->getNext() ) {
            if (SP_IS_TITLE(child) || SP_IS_DESC(child)) {
                continue;
            }
            if (SP_IS_STRING(child)) {
                child->getRepr()->setContent(SP_STRING(child)->string.c_str());
            } else {
                child->updateRepr(flags);
            }
        }
    }

    text->attributes.writeTo(repr);

    // deprecated attribute, but keep it around for backwards compatibility
    if (text->style->line_height.set && !text->style->line_height.inherit && !text->style->line_height.normal && text->style->line_height.unit == SP_CSS_UNIT_PERCENT) {
        Inkscape::SVGOStringStream os;
        os << (text->style->line_height.value * 100.0) << "%";
        text->getRepr()->setAttribute("sodipodi:linespacing", os.str().c_str());
    } else {
        text->getRepr()->setAttribute("sodipodi:linespacing", NULL);
    }

    CItem::onWrite(xml_doc, repr, flags);

    return repr;
}

static Inkscape::XML::Node *sp_text_write(SPObject *object, Inkscape::XML::Document *xml_doc, Inkscape::XML::Node *repr, guint flags)
{
	return ((SPText*)object)->ctext->onWrite(xml_doc, repr, flags);
}

Geom::OptRect CText::onBbox(Geom::Affine const &transform, SPItem::BBoxType type) {
	SPText* item = this->sptext;

    Geom::OptRect bbox = SP_TEXT(item)->layout.bounds(transform);

    // FIXME this code is incorrect
    if (bbox && type == SPItem::VISUAL_BBOX && !item->style->stroke.isNone()) {
        double scale = transform.descrim();
        bbox->expandBy(0.5 * item->style->stroke_width.computed * scale);
    }
    return bbox;
}

static Geom::OptRect
sp_text_bbox(SPItem const *item, Geom::Affine const &transform, SPItem::BBoxType type)
{
	return ((SPText*)item)->ctext->onBbox(transform, type);
}

Inkscape::DrawingItem* CText::onShow(Inkscape::Drawing &drawing, unsigned key, unsigned flags) {
	SPText* item = this->sptext;

    SPText *group = (SPText *) item;

    Inkscape::DrawingGroup *flowed = new Inkscape::DrawingGroup(drawing);
    flowed->setPickChildren(false);
    flowed->setStyle(group->style);

    // pass the bbox of the text object as paintbox (used for paintserver fills)
    group->layout.show(flowed, group->geometricBounds());

    return flowed;
}

static Inkscape::DrawingItem *
sp_text_show(SPItem *item, Inkscape::Drawing &drawing, unsigned key, unsigned flags)
{
	return ((SPText*)item)->ctext->onShow(drawing, key, flags);
}

void CText::onHide(unsigned int key) {
	// CPPIFY: This doesn't make no sense.
	// CItem::onHide is pure and CLPEItem doesn't override it. What was the idea behind these lines?
//    if (((SPItemClass *) text_parent_class)->hide)
//        ((SPItemClass *) text_parent_class)->hide (item, key);
//	CItem::onHide(key);
}

static void
sp_text_hide(SPItem *item, unsigned key)
{
	((SPText*)item)->ctext->onHide(key);
}

gchar* CText::onDescription() {
	SPText* item = this->sptext;

    SPText *text = reinterpret_cast<SPText *>(item);
    SPStyle *style = text->style;

    font_instance *tf = font_factory::Default()->FaceFromStyle(style);

    char name_buf[256];
    char *n;
    if (tf) {
        tf->Name(name_buf, sizeof(name_buf));
        n = xml_quote_strdup(name_buf);
        tf->Unref();
    } else {
        /* TRANSLATORS: For description of font with no name. */
        n = g_strdup(_("&lt;no name found&gt;"));
    }

    GString *xs = SP_PX_TO_METRIC_STRING(style->font_size.computed, sp_desktop_namedview(SP_ACTIVE_DESKTOP)->getDefaultMetric());

    char const *trunc = "";
    Inkscape::Text::Layout const *layout = te_get_layout((SPItem *) item);
    if (layout && layout->inputTruncated()) {
        trunc = _(" [truncated]");
    }

    char *ret = ( SP_IS_TEXT_TEXTPATH(item)
                  ? g_strdup_printf(_("<b>Text on path</b>%s (%s, %s)"), trunc, n, xs->str)
                  : g_strdup_printf(_("<b>Text</b>%s (%s, %s)"), trunc, n, xs->str) );
    g_free(n);
    return ret;
}

static char * sp_text_description(SPItem *item)
{
	return ((SPText*)item)->ctext->onDescription();
}

void CText::onSnappoints(std::vector<Inkscape::SnapCandidatePoint> &p, Inkscape::SnapPreferences const *snapprefs) {
	SPText* item = this->sptext;

    if (snapprefs->isTargetSnappable(Inkscape::SNAPTARGET_TEXT_BASELINE)) {
        // Choose a point on the baseline for snapping from or to, with the horizontal position
        // of this point depending on the text alignment (left vs. right)
        Inkscape::Text::Layout const *layout = te_get_layout((SPItem *) item);
        if (layout != NULL && layout->outputExists()) {
            boost::optional<Geom::Point> pt = layout->baselineAnchorPoint();
            if (pt) {
                p.push_back(Inkscape::SnapCandidatePoint((*pt) * item->i2dt_affine(), Inkscape::SNAPSOURCE_TEXT_ANCHOR, Inkscape::SNAPTARGET_TEXT_ANCHOR));
            }
        }
    }
}

static void sp_text_snappoints(SPItem const *item, std::vector<Inkscape::SnapCandidatePoint> &p, Inkscape::SnapPreferences const *snapprefs)
{
	((SPText*)item)->ctext->onSnappoints(p, snapprefs);
}

Geom::Affine CText::onSetTransform(Geom::Affine const &xform) {
	SPText* item = this->sptext;

    SPText *text = SP_TEXT(item);

    // we cannot optimize textpath because changing its fontsize will break its match to the path
    if (SP_IS_TEXT_TEXTPATH (text))
        return xform;

    /* This function takes care of scaling & translation only, we return whatever parts we can't
       handle. */

// TODO: pjrm tried to use fontsize_expansion(xform) here and it works for text in that font size
// is scaled more intuitively when scaling non-uniformly; however this necessitated using
// fontsize_expansion instead of expansion in other places too, where it was not appropriate
// (e.g. it broke stroke width on copy/pasting of style from horizontally stretched to vertically
// stretched shape). Using fontsize_expansion only here broke setting the style via font
// dialog. This needs to be investigated further.
    double const ex = xform.descrim();
    if (ex == 0) {
        return xform;
    }

    Geom::Affine ret(Geom::Affine(xform).withoutTranslation());
    ret[0] /= ex;
    ret[1] /= ex;
    ret[2] /= ex;
    ret[3] /= ex;

    // Adjust x/y, dx/dy
    text->_adjustCoordsRecursive (item, xform * ret.inverse(), ex);

    // Adjust font size
    text->_adjustFontsizeRecursive (item, ex);

    // Adjust stroke width
    item->adjust_stroke_width_recursive (ex);

    // Adjust pattern fill
    item->adjust_pattern(xform * ret.inverse());

    // Adjust gradient fill
    item->adjust_gradient(xform * ret.inverse());

    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_TEXT_LAYOUT_MODIFIED_FLAG);

    return ret;
}

static Geom::Affine
sp_text_set_transform (SPItem *item, Geom::Affine const &xform)
{
	return ((SPText*)item)->ctext->onSetTransform(xform);
}

void CText::onPrint(SPPrintContext *ctx) {
	SPText* item = this->sptext;

    SPText *group = SP_TEXT (item);
    Geom::OptRect pbox, bbox, dbox;

    pbox = item->geometricBounds();
    bbox = item->desktopVisualBounds();
    dbox = Geom::Rect::from_xywh(Geom::Point(0,0), item->document->getDimensions());
    Geom::Affine const ctm (item->i2dt_affine());

    group->layout.print(ctx,pbox,dbox,bbox,ctm);
}

static void
sp_text_print (SPItem *item, SPPrintContext *ctx)
{
	((SPText*)item)->ctext->onPrint(ctx);
}

/*
 * Member functions
 */

unsigned SPText::_buildLayoutInput(SPObject *root, Inkscape::Text::Layout::OptionalTextTagAttrs const &parent_optional_attrs, unsigned parent_attrs_offset, bool in_textpath)
{
    unsigned length = 0;
    int child_attrs_offset = 0;
    Inkscape::Text::Layout::OptionalTextTagAttrs optional_attrs;

    if (SP_IS_TEXT(root)) {
        SP_TEXT(root)->attributes.mergeInto(&optional_attrs, parent_optional_attrs, parent_attrs_offset, true, true);
    }
    else if (SP_IS_TSPAN(root)) {
        SPTSpan *tspan = SP_TSPAN(root);
        // x, y attributes are stripped from some tspans marked with role="line" as we do our own line layout.
        // This should be checked carefully, as it can undo line layout in imported SVG files.
        bool use_xy = !in_textpath && (tspan->role == SP_TSPAN_ROLE_UNSPECIFIED || !tspan->attributes.singleXYCoordinates());
        tspan->attributes.mergeInto(&optional_attrs, parent_optional_attrs, parent_attrs_offset, use_xy, true);
    }
    else if (SP_IS_TREF(root)) {
        SP_TREF(root)->attributes.mergeInto(&optional_attrs, parent_optional_attrs, parent_attrs_offset, true, true);
    }
    else if (SP_IS_TEXTPATH(root)) {
        in_textpath = true;
        SP_TEXTPATH(root)->attributes.mergeInto(&optional_attrs, parent_optional_attrs, parent_attrs_offset, false, true);
        optional_attrs.x.clear();
        optional_attrs.y.clear();
    }
    else {
        optional_attrs = parent_optional_attrs;
        child_attrs_offset = parent_attrs_offset;
    }

    if (SP_IS_TSPAN(root))
        if (SP_TSPAN(root)->role != SP_TSPAN_ROLE_UNSPECIFIED) {
            // we need to allow the first line not to have role=line, but still set the source_cookie to the right value
            SPObject *prev_object = root->getPrev();
            if (prev_object && SP_IS_TSPAN(prev_object)) {
                if (!layout.inputExists()) {
                    layout.appendText("", prev_object->style, prev_object, &optional_attrs);
                }
                layout.appendControlCode(Inkscape::Text::Layout::PARAGRAPH_BREAK, prev_object);
            }
            if (!root->hasChildren()) {
                layout.appendText("", root->style, root, &optional_attrs);
            }
            length++;     // interpreting line breaks as a character for the purposes of x/y/etc attributes
                          // is a liberal interpretation of the svg spec, but a strict reading would mean
                          // that if the first line is empty the second line would take its place at the
                          // start position. Very confusing.
            child_attrs_offset--;
        }

    for (SPObject *child = root->firstChild() ; child ; child = child->getNext() ) {
        if (SP_IS_STRING(child)) {
            Glib::ustring const &string = SP_STRING(child)->string;
            layout.appendText(string, root->style, child, &optional_attrs, child_attrs_offset + length);
            length += string.length();
        } /*XML Tree being directly used here while it shouldn't be.*/ else if (!sp_repr_is_meta_element(child->getRepr())) {
            length += _buildLayoutInput(child, optional_attrs, child_attrs_offset + length, in_textpath);
        }
    }

    return length;
}

void SPText::rebuildLayout()
{
    layout.clear();
    Inkscape::Text::Layout::OptionalTextTagAttrs optional_attrs;
    _buildLayoutInput(this, optional_attrs, 0, false);
    layout.calculateFlow();
    for (SPObject *child = firstChild() ; child ; child = child->getNext() ) {
        if (SP_IS_TEXTPATH(child)) {
            SPTextPath const *textpath = SP_TEXTPATH(child);
            if (textpath->originalPath != NULL) {
                //g_print(layout.dumpAsText().c_str());
                layout.fitToPathAlign(textpath->startOffset, *textpath->originalPath);
            }
        }
    }
    //g_print(layout.dumpAsText().c_str());

    // set the x,y attributes on role:line spans
    for (SPObject *child = firstChild() ; child ; child = child->getNext() ) {
        if (SP_IS_TSPAN(child)) {
            SPTSpan *tspan = SP_TSPAN(child);
            if ( (tspan->role != SP_TSPAN_ROLE_UNSPECIFIED)
                 && tspan->attributes.singleXYCoordinates() ) {
                Inkscape::Text::Layout::iterator iter = layout.sourceToIterator(tspan);
                Geom::Point anchor_point = layout.chunkAnchorPoint(iter);
                tspan->attributes.setFirstXY(anchor_point);
            }
        }
    }
}


void SPText::_adjustFontsizeRecursive(SPItem *item, double ex, bool is_root)
{
    SPStyle *style = item->style;

    if (style && !Geom::are_near(ex, 1.0)) {
        if (!style->font_size.set && is_root) {
            style->font_size.set = 1;
        }
        style->font_size.type = SP_FONT_SIZE_LENGTH;
        style->font_size.computed *= ex;
        style->letter_spacing.computed *= ex;
        style->word_spacing.computed *= ex;
        item->updateRepr();
    }

    for (SPObject *o = item->children; o != NULL; o = o->next) {
        if (SP_IS_ITEM(o))
            _adjustFontsizeRecursive(SP_ITEM(o), ex, false);
    }
}

void SPText::_adjustCoordsRecursive(SPItem *item, Geom::Affine const &m, double ex, bool is_root)
{
    if (SP_IS_TSPAN(item))
        SP_TSPAN(item)->attributes.transform(m, ex, ex, is_root);
              // it doesn't matter if we change the x,y for role=line spans because we'll just overwrite them anyway
    else if (SP_IS_TEXT(item))
        SP_TEXT(item)->attributes.transform(m, ex, ex, is_root);
    else if (SP_IS_TEXTPATH(item))
        SP_TEXTPATH(item)->attributes.transform(m, ex, ex, is_root);
    else if (SP_IS_TREF(item)) {
        SP_TREF(item)->attributes.transform(m, ex, ex, is_root);
    }

    for (SPObject *o = item->children; o != NULL; o = o->next) {
        if (SP_IS_ITEM(o))
            _adjustCoordsRecursive(SP_ITEM(o), m, ex, false);
    }
}


void SPText::_clearFlow(Inkscape::DrawingGroup *in_arena)
{
    in_arena->clearChildren();
}


/*
 * TextTagAttributes implementation
 */

void TextTagAttributes::readFrom(Inkscape::XML::Node const *node)
{
    readSingleAttribute(SP_ATTR_X, node->attribute("x"));
    readSingleAttribute(SP_ATTR_Y, node->attribute("y"));
    readSingleAttribute(SP_ATTR_DX, node->attribute("dx"));
    readSingleAttribute(SP_ATTR_DY, node->attribute("dy"));
    readSingleAttribute(SP_ATTR_ROTATE, node->attribute("rotate"));
}

bool TextTagAttributes::readSingleAttribute(unsigned key, gchar const *value)
{
    std::vector<SVGLength> *attr_vector;
    switch (key) {
        case SP_ATTR_X:      attr_vector = &attributes.x; break;
        case SP_ATTR_Y:      attr_vector = &attributes.y; break;
        case SP_ATTR_DX:     attr_vector = &attributes.dx; break;
        case SP_ATTR_DY:     attr_vector = &attributes.dy; break;
        case SP_ATTR_ROTATE: attr_vector = &attributes.rotate; break;
        default: return false;
    }

    // FIXME: sp_svg_length_list_read() amalgamates repeated separators. This prevents unset values.
    *attr_vector = sp_svg_length_list_read(value);
    return true;
}

void TextTagAttributes::writeTo(Inkscape::XML::Node *node) const
{
    writeSingleAttribute(node, "x", attributes.x);
    writeSingleAttribute(node, "y", attributes.y);
    writeSingleAttribute(node, "dx", attributes.dx);
    writeSingleAttribute(node, "dy", attributes.dy);
    writeSingleAttribute(node, "rotate", attributes.rotate);
}

void TextTagAttributes::writeSingleAttribute(Inkscape::XML::Node *node, gchar const *key, std::vector<SVGLength> const &attr_vector)
{
    if (attr_vector.empty())
        node->setAttribute(key, NULL);
    else {
        Glib::ustring string;
        gchar single_value_string[32];

        // FIXME: this has no concept of unset values because sp_svg_length_list_read() can't read them back in
        for (std::vector<SVGLength>::const_iterator it = attr_vector.begin() ; it != attr_vector.end() ; ++it) {
            g_ascii_formatd(single_value_string, sizeof (single_value_string), "%.8g", it->computed);
            if (!string.empty()) string += ' ';
            string += single_value_string;
        }
        node->setAttribute(key, string.c_str());
    }
}

bool TextTagAttributes::singleXYCoordinates() const
{
    return attributes.x.size() <= 1 && attributes.y.size() <= 1;
}

bool TextTagAttributes::anyAttributesSet() const
{
    return !attributes.x.empty() || !attributes.y.empty() || !attributes.dx.empty() || !attributes.dy.empty() || !attributes.rotate.empty();
}

Geom::Point TextTagAttributes::firstXY() const
{
    Geom::Point point;
    if (attributes.x.empty()) point[Geom::X] = 0.0;
    else point[Geom::X] = attributes.x[0].computed;
    if (attributes.y.empty()) point[Geom::Y] = 0.0;
    else point[Geom::Y] = attributes.y[0].computed;
    return point;
}

void TextTagAttributes::setFirstXY(Geom::Point &point)
{
    SVGLength zero_length;
    zero_length = 0.0;

    if (attributes.x.empty())
        attributes.x.resize(1, zero_length);
    if (attributes.y.empty())
        attributes.y.resize(1, zero_length);
    attributes.x[0].computed = point[Geom::X];
    attributes.y[0].computed = point[Geom::Y];
}

void TextTagAttributes::mergeInto(Inkscape::Text::Layout::OptionalTextTagAttrs *output, Inkscape::Text::Layout::OptionalTextTagAttrs const &parent_attrs, unsigned parent_attrs_offset, bool copy_xy, bool copy_dxdyrotate) const
{
    mergeSingleAttribute(&output->x,      parent_attrs.x,      parent_attrs_offset, copy_xy ? &attributes.x : NULL);
    mergeSingleAttribute(&output->y,      parent_attrs.y,      parent_attrs_offset, copy_xy ? &attributes.y : NULL);
    mergeSingleAttribute(&output->dx,     parent_attrs.dx,     parent_attrs_offset, copy_dxdyrotate ? &attributes.dx : NULL);
    mergeSingleAttribute(&output->dy,     parent_attrs.dy,     parent_attrs_offset, copy_dxdyrotate ? &attributes.dy : NULL);
    mergeSingleAttribute(&output->rotate, parent_attrs.rotate, parent_attrs_offset, copy_dxdyrotate ? &attributes.rotate : NULL);
}

void TextTagAttributes::mergeSingleAttribute(std::vector<SVGLength> *output_list, std::vector<SVGLength> const &parent_list, unsigned parent_offset, std::vector<SVGLength> const *overlay_list)
{
    output_list->clear();
    if (overlay_list == NULL) {
        if (parent_list.size() > parent_offset)
        {
            output_list->reserve(parent_list.size() - parent_offset);
            std::copy(parent_list.begin() + parent_offset, parent_list.end(), std::back_inserter(*output_list));
        }
    } else {
        output_list->reserve(std::max((int)parent_list.size() - (int)parent_offset, (int)overlay_list->size()));
        unsigned overlay_offset = 0;
        while (parent_offset < parent_list.size() || overlay_offset < overlay_list->size()) {
            SVGLength const *this_item;
            if (overlay_offset < overlay_list->size()) {
                this_item = &(*overlay_list)[overlay_offset];
                overlay_offset++;
                parent_offset++;
            } else {
                this_item = &parent_list[parent_offset];
                parent_offset++;
            }
            output_list->push_back(*this_item);
        }
    }
}

void TextTagAttributes::erase(unsigned start_index, unsigned n)
{
    if (n == 0) return;
    if (!singleXYCoordinates()) {
        eraseSingleAttribute(&attributes.x, start_index, n);
        eraseSingleAttribute(&attributes.y, start_index, n);
    }
    eraseSingleAttribute(&attributes.dx, start_index, n);
    eraseSingleAttribute(&attributes.dy, start_index, n);
    eraseSingleAttribute(&attributes.rotate, start_index, n);
}

void TextTagAttributes::eraseSingleAttribute(std::vector<SVGLength> *attr_vector, unsigned start_index, unsigned n)
{
    if (attr_vector->size() <= start_index) return;
    if (attr_vector->size() <= start_index + n)
        attr_vector->erase(attr_vector->begin() + start_index, attr_vector->end());
    else
        attr_vector->erase(attr_vector->begin() + start_index, attr_vector->begin() + start_index + n);
}

void TextTagAttributes::insert(unsigned start_index, unsigned n)
{
    if (n == 0) return;
    if (!singleXYCoordinates()) {
        insertSingleAttribute(&attributes.x, start_index, n, true);
        insertSingleAttribute(&attributes.y, start_index, n, true);
    }
    insertSingleAttribute(&attributes.dx, start_index, n, false);
    insertSingleAttribute(&attributes.dy, start_index, n, false);
    insertSingleAttribute(&attributes.rotate, start_index, n, false);
}

void TextTagAttributes::insertSingleAttribute(std::vector<SVGLength> *attr_vector, unsigned start_index, unsigned n, bool is_xy)
{
    if (attr_vector->size() <= start_index) return;
    SVGLength zero_length;
    zero_length = 0.0;
    attr_vector->insert(attr_vector->begin() + start_index, n, zero_length);
    if (is_xy) {
        double begin = start_index == 0 ? (*attr_vector)[start_index + n].computed : (*attr_vector)[start_index - 1].computed;
        double diff = ((*attr_vector)[start_index + n].computed - begin) / n;   // n tested for nonzero in insert()
        for (unsigned i = 0 ; i < n ; i++)
            (*attr_vector)[start_index + i] = begin + diff * i;
    }
}

void TextTagAttributes::split(unsigned index, TextTagAttributes *second)
{
    if (!singleXYCoordinates()) {
        splitSingleAttribute(&attributes.x, index, &second->attributes.x, false);
        splitSingleAttribute(&attributes.y, index, &second->attributes.y, false);
    }
    splitSingleAttribute(&attributes.dx, index, &second->attributes.dx, true);
    splitSingleAttribute(&attributes.dy, index, &second->attributes.dy, true);
    splitSingleAttribute(&attributes.rotate, index, &second->attributes.rotate, true);
}

void TextTagAttributes::splitSingleAttribute(std::vector<SVGLength> *first_vector, unsigned index, std::vector<SVGLength> *second_vector, bool trimZeros)
{
    second_vector->clear();
    if (first_vector->size() <= index) return;
    second_vector->resize(first_vector->size() - index);
    std::copy(first_vector->begin() + index, first_vector->end(), second_vector->begin());
    first_vector->resize(index);
    if (trimZeros)
        while (!first_vector->empty() && (!first_vector->back()._set || first_vector->back().value == 0.0))
            first_vector->resize(first_vector->size() - 1);
}

void TextTagAttributes::join(TextTagAttributes const &first, TextTagAttributes const &second, unsigned second_index)
{
    if (second.singleXYCoordinates()) {
        attributes.x = first.attributes.x;
        attributes.y = first.attributes.y;
    } else {
        joinSingleAttribute(&attributes.x, first.attributes.x, second.attributes.x, second_index);
        joinSingleAttribute(&attributes.y, first.attributes.y, second.attributes.y, second_index);
    }
    joinSingleAttribute(&attributes.dx, first.attributes.dx, second.attributes.dx, second_index);
    joinSingleAttribute(&attributes.dy, first.attributes.dy, second.attributes.dy, second_index);
    joinSingleAttribute(&attributes.rotate, first.attributes.rotate, second.attributes.rotate, second_index);
}

void TextTagAttributes::joinSingleAttribute(std::vector<SVGLength> *dest_vector, std::vector<SVGLength> const &first_vector, std::vector<SVGLength> const &second_vector, unsigned second_index)
{
    if (second_vector.empty())
        *dest_vector = first_vector;
    else {
        dest_vector->resize(second_index + second_vector.size());
        if (first_vector.size() < second_index) {
            std::copy(first_vector.begin(), first_vector.end(), dest_vector->begin());
            SVGLength zero_length;
            zero_length = 0.0;
            std::fill(dest_vector->begin() + first_vector.size(), dest_vector->begin() + second_index, zero_length);
        } else
            std::copy(first_vector.begin(), first_vector.begin() + second_index, dest_vector->begin());
        std::copy(second_vector.begin(), second_vector.end(), dest_vector->begin() + second_index);
    }
}

void TextTagAttributes::transform(Geom::Affine const &matrix, double scale_x, double scale_y, bool extend_zero_length)
{
    SVGLength zero_length;
    zero_length = 0.0;

    /* edge testcases for this code:
       1) moving text elements whose position is done entirely with transform="...", no x,y attributes
       2) unflowing multi-line flowtext then moving it (it has x but not y)
    */
    unsigned points_count = std::max(attributes.x.size(), attributes.y.size());
    if (extend_zero_length && points_count < 1)
        points_count = 1;
    for (unsigned i = 0 ; i < points_count ; i++) {
        Geom::Point point;
        if (i < attributes.x.size()) point[Geom::X] = attributes.x[i].computed;
        else point[Geom::X] = 0.0;
        if (i < attributes.y.size()) point[Geom::Y] = attributes.y[i].computed;
        else point[Geom::Y] = 0.0;
        point *= matrix;
        if (i < attributes.x.size())
            attributes.x[i] = point[Geom::X];
        else if (point[Geom::X] != 0.0 && extend_zero_length) {
            attributes.x.resize(i + 1, zero_length);
            attributes.x[i] = point[Geom::X];
        }
        if (i < attributes.y.size())
            attributes.y[i] = point[Geom::Y];
        else if (point[Geom::Y] != 0.0 && extend_zero_length) {
            attributes.y.resize(i + 1, zero_length);
            attributes.y[i] = point[Geom::Y];
        }
    }
    for (std::vector<SVGLength>::iterator it = attributes.dx.begin() ; it != attributes.dx.end() ; ++it)
        *it = it->computed * scale_x;
    for (std::vector<SVGLength>::iterator it = attributes.dy.begin() ; it != attributes.dy.end() ; ++it)
        *it = it->computed * scale_y;
}

double TextTagAttributes::getDx(unsigned index)
{
    if( attributes.dx.empty()) {
        return 0.0;
    }
    if( index < attributes.dx.size() ) {
        return attributes.dx[index].computed;
    } else {
        return 0.0; // attributes.dx.back().computed;
    }
}


double TextTagAttributes::getDy(unsigned index)
{
    if( attributes.dy.empty() ) {
        return 0.0;
    }
    if( index < attributes.dy.size() ) {
        return attributes.dy[index].computed;
    } else {
        return 0.0; // attributes.dy.back().computed;
    }
}


void TextTagAttributes::addToDx(unsigned index, double delta)
{
    SVGLength zero_length;
    zero_length = 0.0;

    if (attributes.dx.size() < index + 1) attributes.dx.resize(index + 1, zero_length);
    attributes.dx[index] = attributes.dx[index].computed + delta;
}

void TextTagAttributes::addToDy(unsigned index, double delta)
{
    SVGLength zero_length;
    zero_length = 0.0;

    if (attributes.dy.size() < index + 1) attributes.dy.resize(index + 1, zero_length);
    attributes.dy[index] = attributes.dy[index].computed + delta;
}

void TextTagAttributes::addToDxDy(unsigned index, Geom::Point const &adjust)
{
    SVGLength zero_length;
    zero_length = 0.0;

    if (adjust[Geom::X] != 0.0) {
        if (attributes.dx.size() < index + 1) attributes.dx.resize(index + 1, zero_length);
        attributes.dx[index] = attributes.dx[index].computed + adjust[Geom::X];
    }
    if (adjust[Geom::Y] != 0.0) {
        if (attributes.dy.size() < index + 1) attributes.dy.resize(index + 1, zero_length);
        attributes.dy[index] = attributes.dy[index].computed + adjust[Geom::Y];
    }
}

double TextTagAttributes::getRotate(unsigned index)
{
    if( attributes.rotate.empty() ) {
        return 0.0;
    }
    if( index < attributes.rotate.size() ) {
        return attributes.rotate[index].computed;
    } else {
        return attributes.rotate.back().computed;
    }
}


void TextTagAttributes::addToRotate(unsigned index, double delta)
{
    SVGLength zero_length;
    zero_length = 0.0;

    if (attributes.rotate.size() < index + 2) {
        if (attributes.rotate.empty())
            attributes.rotate.resize(index + 2, zero_length);
        else
            attributes.rotate.resize(index + 2, attributes.rotate.back());
    }
    attributes.rotate[index] = mod360(attributes.rotate[index].computed + delta);
}


void TextTagAttributes::setRotate(unsigned index, double angle)
{
    SVGLength zero_length;
    zero_length = 0.0;

    if (attributes.rotate.size() < index + 2) {
        if (attributes.rotate.empty())
            attributes.rotate.resize(index + 2, zero_length);
        else
            attributes.rotate.resize(index + 2, attributes.rotate.back());
    }
    attributes.rotate[index] = mod360(angle);
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
