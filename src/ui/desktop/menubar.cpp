// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Desktop main menu bar code.
 */
/*
 * Authors:
 *   Tavmjong Bah       <tavmjong@free.fr>
 *   Alex Valavanis     <valavanisalex@gmail.com>
 *   Patrick Storz      <eduard.braun2@gmx.de>
 *   Krzysztof Kosiński <tweenk.pl@gmail.com>
 *   Kris De Gussem     <Kris.DeGussem@gmail.com>
 *
 * Copyright (C) 2018 Authors
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 * Read the file 'COPYING' for more information.
 *
 */

#include <gtkmm.h>
#include <glibmm/i18n.h>

#include <iostream>

#include "inkscape.h"
#include "file.h"  // sp_file_open
#include "message-context.h"
#include "shortcuts.h"

#include "helper/action.h"
#include "helper/action-context.h"

#include "object/sp-namedview.h"

#include "ui/contextmenu.h" // Shift to make room for icons
#include "ui/icon-loader.h"
#include "ui/view/view.h"
#include "ui/uxmanager.h"   // To Do: Convert to actions

// ================= Common ====================

// Sets tip
static void
select_action(SPAction *action)
{
    sp_action_get_view(action)->tipsMessageContext()->set(Inkscape::NORMAL_MESSAGE, action->tip);
}

// Clears tip
static void
deselect_action(SPAction *action)
{
    sp_action_get_view(action)->tipsMessageContext()->clear();
}

// Trigger action
static void
item_activate(Gtk::MenuItem* menuitem, SPAction* action)
{
    sp_action_perform(action, nullptr);
}

// Change label name (used in the Undo/Redo menu items).
static void
set_name(Glib::ustring const &name, Gtk::MenuItem* menuitem)
{
    if (menuitem) {
        Gtk::Widget* widget = menuitem->get_child();

        // Label is either child of menuitem
        Gtk::Label* label = dynamic_cast<Gtk::Label*>(widget);

        // Or wrapped inside a box which is a child of menuitem (if with icon).
        if (!label) {
            Gtk::Box* box = dynamic_cast<Gtk::Box*>(widget);
            if (box) {
                std::vector<Gtk::Widget*> children = box->get_children();
                for (auto child: children) {
                    label = dynamic_cast<Gtk::Label*>(child);
                    if (label) break;
                }
            }
        }

        if (label) {
            label->set_markup_with_mnemonic(name);
        } else {
            std::cerr << "set_name: could not find label!" << std::endl;
        }           
    }
}

/* Install CSS to shift icons into the space reserved for toggles (i.e. check and radio items).
 *
 * TODO: This code already exists as a C++ version in the class ContextMenu so we can simply wrap
 *       it here. In future ContextMenu and the (to be created) class for the menu bar should then
 *       be derived from one common base class.
 *
 * TODO: This code is called everytime a menu is opened. We can certainly find a way to call it once.
 */
static void
shift_icons(Gtk::Menu* menu)
{
    ContextMenu *contextmenu = static_cast<ContextMenu*>(menu);
    contextmenu->ShiftIcons();
}

// ================= MenuItem ====================

Gtk::MenuItem*
build_menu_item_from_verb(SPAction* action,
                          bool show_icon,
                          bool radio = false,
                          Gtk::RadioMenuItem::Group *group = nullptr)
{
    Gtk::MenuItem* menuitem = nullptr;

    if (radio) {
        menuitem = Gtk::manage(new Gtk::RadioMenuItem(*group));
    } else {
        menuitem = Gtk::manage(new Gtk::MenuItem());
    }

    Gtk::AccelLabel* label = Gtk::manage(new Gtk::AccelLabel(action->name, true));
    label->set_alignment(0.0, 0.5);
    label->set_accel_widget(*menuitem);
    sp_shortcut_add_accelerator((GtkWidget*)menuitem->gobj(), sp_shortcut_get_primary(action->verb));

    // If there is an image associated with the action, we can add it as an icon for the menu item.
    if (show_icon && action->image) {
        menuitem->set_name("ImageMenuItem");  // custom name to identify our "ImageMenuItems"
        Gtk::Image* image = Gtk::manage(sp_get_icon_image(action->image, Gtk::ICON_SIZE_MENU));

        // Create a box to hold icon and label as Gtk::MenuItem derives from GtkBin and can
        // only hold one child
        Gtk::Box *box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL));
        box->pack_start(*image, false, false, 0);
        box->pack_start(*label, true,  true,  0);
        menuitem->add(*box);
    } else {
        menuitem->add(*label);
    }

    menuitem->signal_activate().connect(
        sigc::bind<Gtk::MenuItem*, SPAction*>(sigc::ptr_fun(&item_activate), menuitem, action));
    menuitem->signal_select().connect(  sigc::bind<SPAction*>(sigc::ptr_fun(&select_action),   action));
    menuitem->signal_deselect().connect(sigc::bind<SPAction*>(sigc::ptr_fun(&deselect_action), action));

    action->signal_set_sensitive.connect(
        sigc::bind<0>(sigc::ptr_fun(&gtk_widget_set_sensitive), (GtkWidget*)menuitem->gobj()));
    action->signal_set_name.connect(
        sigc::bind<Gtk::MenuItem*>(sigc::ptr_fun(&set_name), menuitem));

    return menuitem;
}

// =============== CheckMenuItem ==================

static bool
getStateFromPref(SPDesktop* dt, Glib::ustring item)
{
    Glib::ustring pref_path;

    if (dt->is_focusMode()) {
        pref_path = "/focus/";
    } else if (dt->is_fullscreen()) {
        pref_path = "/fullscreen/";
    } else {
        pref_path = "/window/";
    }

    pref_path += item;
    pref_path += "/state";

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    return prefs->getBool(pref_path, false);
}

// I wonder if this can be done without hard coding names.
static void
checkitem_update(Gtk::CheckMenuItem* menuitem, SPAction* action)
{
    bool active = false;
    if (action && action->id) {
        Glib::ustring id = action->id;
        SPDesktop* dt = static_cast<SPDesktop*>(sp_action_get_view(action));

        if (id == "ToggleGrid") {
            active = dt->gridsEnabled();

        } else if (id == "EditGuidesToggleLock") {
            active = dt->namedview->lockguides;

        } else if (id == "ToggleGuides") {
            active = dt->namedview->getGuides();

        } else if (id == "ViewCmsToggle") {
            active = dt->colorProfAdjustEnabled();

        } else if (id == "ViewSplitModeToggle") {
            active = dt->splitMode();

        } else if (id == "ViewXRayToggle") {
            active = dt->xrayMode();

        } else if (id == "ToggleCommandsToolbar") {
            active = getStateFromPref(dt, "commands");

        } else if (id == "ToggleSnapToolbar") {
            active = getStateFromPref(dt, "snaptoolbox");

        } else if (id == "ToggleToolToolbar") {
            active = getStateFromPref(dt, "toppanel");

        } else if (id == "ToggleToolbox") {
            active = getStateFromPref(dt, "toolbox");

        } else if (id == "ToggleRulers") {
            active = getStateFromPref(dt, "rulers");

        } else if (id == "ToggleScrollbars") {
            active = getStateFromPref(dt, "scrollbars");

        } else if (id == "TogglePalette") {
            active = getStateFromPref(dt, "panels");  // Rename?

        } else if (id == "ToggleStatusbar") {
            active = getStateFromPref(dt, "statusbar");

        } else {
            std::cerr << "checkitem_update: unhandled item: " << id << std::endl;
        }

        menuitem->set_active(active);
    } else {
        std::cerr << "checkitem_update: unknown action" << std::endl;
    }      
}

static Gtk::CheckMenuItem*
build_menu_check_item_from_verb(SPAction* action)
{
    // This does not work for some reason!
    // Gtk::CheckMenuItem* menuitem = Gtk::manage(new Gtk::CheckMenuItem(action->name, true));
    // sp_shortcut_add_accelerator(GTK_WIDGET(menuitem->gobj()), sp_shortcut_get_primary(action->verb));

    GtkWidget *item = gtk_check_menu_item_new_with_mnemonic(action->name);
    sp_shortcut_add_accelerator(item, sp_shortcut_get_primary(action->verb));
    Gtk::CheckMenuItem* menuitem = Gtk::manage(Glib::wrap(GTK_CHECK_MENU_ITEM(item)));

    // Set initial state before connecting signals.
    checkitem_update(menuitem, action);

    menuitem->signal_toggled().connect(
      sigc::bind<Gtk::CheckMenuItem*, SPAction*>(sigc::ptr_fun(&item_activate), menuitem, action)); 
    menuitem->signal_select().connect(  sigc::bind<SPAction*>(sigc::ptr_fun(&select_action),   action));
    menuitem->signal_deselect().connect(sigc::bind<SPAction*>(sigc::ptr_fun(&deselect_action), action));

    return menuitem;
}

// ================= Tasks Submenu ==============
static void
task_activated(SPDesktop* dt, int number)
{
    Inkscape::UI::UXManager::getInstance()->setTask(dt, number);
}

// Sets tip
static void
select_task(SPDesktop* dt, Glib::ustring tip)
{
    dt->tipsMessageContext()->set(Inkscape::NORMAL_MESSAGE, tip.c_str());
}

// Clears tip
static void
deselect_task(SPDesktop* dt)
{
    dt->tipsMessageContext()->clear();
}

static void
add_tasks(Gtk::MenuShell* menu, SPDesktop* dt)
{
    const Glib::ustring data[3][2] = {
        { C_("Interface setup", "Default"), _("Default interface setup")   },
        { C_("Interface setup", "Custom"),  _("Setup for custom task")     },
        { C_("Interface setup", "Wide"),    _("Setup for widescreen work") }
    };

    int active = Inkscape::UI::UXManager::getInstance()->getDefaultTask(dt);

    Gtk::RadioMenuItem::Group group;

    for (unsigned int i = 0; i < 3; ++i) {

        Gtk::RadioMenuItem* menuitem = Gtk::manage(new Gtk::RadioMenuItem(group, data[i][0]));
        if (menuitem) {
            if (active == i) {
                menuitem->set_active();
            }

            menuitem->signal_toggled().connect(
                sigc::bind<SPDesktop*, int>(sigc::ptr_fun(&task_activated), dt, i)); 
            menuitem->signal_select().connect(
                sigc::bind<SPDesktop*, Glib::ustring>(sigc::ptr_fun(&select_task),  dt, data[i][1]));
            menuitem->signal_deselect().connect(
                sigc::bind<SPDesktop*>(sigc::ptr_fun(&deselect_task),dt));

            menu->append(*menuitem);
        }
    }
}


static void
sp_recent_open(Gtk::RecentChooser* recentchooser)
{
    Glib::ustring uri = recentchooser->get_current_uri();

    Glib::RefPtr<Gio::File> file = Gio::File::create_for_uri(uri);

    // To do: change sp_file_open to use Gio::File.
    // To do: get rid of sp_file_open
    sp_file_open(file->get_parse_name(), nullptr);
}

// =================== Main Menu ================
// Recursively build menu and submenus.
void
build_menu(Gtk::MenuShell* menu, Inkscape::XML::Node* xml, Inkscape::UI::View::View* view)
{
    if (menu == nullptr) {
        std::cerr << "build_menu: menu is nullptr" << std::endl;
        return;
    }

    if (xml == nullptr) {
        std::cerr << "build_menu: xml is nullptr" << std::endl;
        return;
    }

    // Do we want icons????
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int show_icon_pref = prefs->getInt("/theme/menuIcons", 0);
    static bool show_icon = false;
    if (show_icon_pref ==  1) {
        show_icon = true;
    } else if ( show_icon_pref == -1) {
        show_icon = false;
    }

    Gtk::RadioMenuItem::Group group;

    for (auto menu_ptr = xml; menu_ptr != nullptr; menu_ptr = menu_ptr->next()) {

        if (show_icon_pref == 0) {
            // We set according to file, value is inherited into sub-menus.
            const char *str = menu_ptr->attribute("show-icons");
            if (str) {
                Glib::ustring ustr = str;
                if (ustr == "true") {
                    show_icon = true;
                } else if (ustr == "false") {
                    show_icon = false;
                } else {
                    std::cerr << "build_menu: invalid value for 'show-icon' (use 'true' or 'false')."
                              << ustr << std::endl;
                }
            }
        }

        if (menu_ptr->name()) {
            Glib::ustring name = menu_ptr->name();

            if (name == "inkscape") {
                build_menu(menu, menu_ptr->firstChild(), view);
                continue;
            }

            if (name == "submenu") {
                Gtk::MenuItem* menuitem = nullptr;
                if (menu_ptr->attribute("_name") != nullptr) {
                    menuitem = Gtk::manage(new Gtk::MenuItem(_(menu_ptr->attribute("_name")), true));
                } else {
                    menuitem = Gtk::manage(new Gtk::MenuItem(  menu_ptr->attribute("name"),   true));
                }
                Gtk::Menu* submenu = Gtk::manage(new Gtk::Menu());
                build_menu(submenu, menu_ptr->firstChild(), view);
                menuitem->set_submenu(*submenu);
                menu->append(*menuitem);

                submenu->signal_map().connect(
                    sigc::bind<Gtk::Menu*>(sigc::ptr_fun(&shift_icons), submenu));

                continue;
            }

            if (name == "verb") {
                if (menu_ptr->attribute("verb-id") != nullptr) {
                    Glib::ustring verb_name = menu_ptr->attribute("verb-id");

                    Inkscape::Verb *verb = Inkscape::Verb::getbyid(verb_name.c_str());
                    if (verb != nullptr && verb->get_code() != SP_VERB_NONE) {

                        SPAction* action = verb->get_action(Inkscape::ActionContext(view));

                        if (menu_ptr->attribute("check") != nullptr) {

                            Gtk::CheckMenuItem* menuitem = build_menu_check_item_from_verb(action);
                            if (menuitem) {
                                menu->append(*menuitem);
                            }

                        } else if (menu_ptr->attribute("radio") != nullptr) {

                            Gtk::MenuItem* menuitem = build_menu_item_from_verb(action, show_icon, true, &group);
                            if (menuitem) {
                                if (menu_ptr->attribute("default") != nullptr) {
                                    auto radiomenuitem = dynamic_cast<Gtk::RadioMenuItem*>(menuitem);
                                    if (radiomenuitem) {
                                        radiomenuitem->set_active(true);
                                    }
                                }
                                menu->append(*menuitem);
                            }

                        } else {

                            Gtk::MenuItem* menuitem = build_menu_item_from_verb(action, show_icon);
                            if (menuitem) {
                                menu->append(*menuitem);
                            }
                        }
                    } else {
                        std::cerr << "build_menu: no verb with id: " << verb_name << std::endl;
                    }
                }
                continue;
            }

            // This is used only for wide-screen vs non-wide-screen displays.
            // The code should be rewritten to use actions like everything else here.
            if (name == "task-checkboxes") {
                add_tasks(menu, static_cast<SPDesktop*>(view));
                continue;
            }

            if (name == "recent-file-list") {

                // Filter recent files to those already opened in Inkscape.
                Glib::RefPtr<Gtk::RecentFilter> recentfilter = Gtk::RecentFilter::create();
                recentfilter->add_application(g_get_prgname());

                Gtk::RecentChooserMenu* recentchoosermenu = Gtk::manage(new Gtk::RecentChooserMenu());
                int max = Inkscape::Preferences::get()->getInt("/options/maxrecentdocuments/value");
                recentchoosermenu->set_limit(max);
                recentchoosermenu->set_sort_type(Gtk::RECENT_SORT_MRU); // Sort most recent first.
                recentchoosermenu->set_show_tips(true);
                recentchoosermenu->set_show_not_found(false);
                recentchoosermenu->add_filter(recentfilter);
                recentchoosermenu->signal_item_activated().connect(
                    sigc::bind<Gtk::RecentChooserMenu*>(sigc::ptr_fun(&sp_recent_open), recentchoosermenu));

                Gtk::MenuItem* menuitem = Gtk::manage(new Gtk::MenuItem(_("Open _Recent"), true));
                menuitem->set_submenu(*recentchoosermenu);
                menu->append(*menuitem);
                continue;
            }

            if (name == "separator") {
                Gtk::MenuItem* menuitem = Gtk::manage(new Gtk::SeparatorMenuItem());
                menu->append(*menuitem);
                continue;
            }

            // Comments and items handled elsewhere.
            if (name == "comment"      ||
                name == "filters-list" ||
                name == "effects-list" ) {
                continue;
            }

            std::cerr << "build_menu: unhandled option: " << name << std::endl;

        } else {
            std::cerr << "build_menu: xml node has no name!" << std::endl;
        }
    }

}

Gtk::MenuBar*
build_menubar(Inkscape::UI::View::View* view)
{
    Gtk::MenuBar* menubar = Gtk::manage(new Gtk::MenuBar());
    build_menu(menubar, INKSCAPE.get_menus()->parent(), view);
    return menubar;
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
