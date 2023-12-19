#include "FloatingToolbox.h"

#include <algorithm>  // for max
#include <memory>     // for allocator

#include <gdk/gdk.h>      // for GdkRectangle, GDK_LEAVE_...
#include <glib-object.h>  // for G_CALLBACK, g_signal_con...

#include "control/Control.h"                 // for Control
#include "control/ToolEnums.h"               // for TOOL_FLOATING_TOOLBOX
#include "control/settings/ButtonConfig.h"   // for ButtonConfig
#include "control/settings/Settings.h"       // for Settings
#include "control/settings/SettingsEnums.h"  // for BUTTON_COUNT

#include "MainWindow.h"          // for MainWindow
#include "ToolbarDefinitions.h"  // for ToolbarEntryDefintion


FloatingToolbox::FloatingToolbox(MainWindow* theMainWindow, GtkOverlay* overlay)
        :
        mainWindow(theMainWindow),
        floatingToolbox(theMainWindow->get("floatingToolbox")),
        overlay(overlay, xoj::util::ref),
        boxContents(theMainWindow->get("boxContents")),
        floatingToolboxX(200),
        floatingToolboxY(200),
        floatingToolboxState(recalcSize)
{
    gtk_overlay_add_overlay(overlay, this->floatingToolbox);
    gtk_overlay_set_overlay_pass_through(overlay, this->floatingToolbox, true);
    gtk_widget_add_events(this->floatingToolbox, GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(this->floatingToolbox, "leave-notify-event", G_CALLBACK(handleLeaveFloatingToolbox), this);
    // position overlay widgets
    g_signal_connect(overlay, "get-child-position", G_CALLBACK(this->getOverlayPosition), this);
}


FloatingToolbox::~FloatingToolbox() = default;


void FloatingToolbox::show(int x, int y) {
    // (x, y) are in the gtk window's coordinates.
    // However, we actually show the toolbox in the overlay's coordinate system.
    gtk_widget_translate_coordinates(gtk_widget_get_toplevel(this->floatingToolbox), GTK_WIDGET(overlay.get()), x, y,
                                     &floatingToolboxX, &floatingToolboxY);

    this->show();
}


/****
 * floatingToolboxActivated
 *  True if the user has:
 *    assigned a mouse or stylus button to bring up the floatingToolbox;
 *    or enabled tapAction and Show FloatingToolbox( prefs->DrawingArea->ActionOnToolTap );
 *    or put tools in the FloatingToolbox.
 *
 */
auto FloatingToolbox::floatingToolboxActivated() -> bool {
    Settings* settings = this->mainWindow->getControl()->getSettings();
    ButtonConfig* cfg = nullptr;

    // check if any buttons assigned to bring up toolbox
    for (unsigned int id = 0; id < BUTTON_COUNT; id++) {
        cfg = settings->getButtonConfig(id);

        if (cfg->getAction() == TOOL_FLOATING_TOOLBOX) {
            return true;
        }
    }

    // check if user can show Floating Menu with tap.
    if (settings->getDoActionOnStrokeFiltered() && settings->getStrokeFilterEnabled()) {
        return true;
    }

    if (this->countWidgets() > 0)  // FloatingToolbox contains something
    {
        return true;
    }

    return false;
}


auto FloatingToolbox::countWidgets() -> int {
    int count = 0;

    for (int index = TBFloatFirst; index <= TBFloatLast; index++) {
        const char* guiName = TOOLBAR_DEFINITIONS[index].guiName;
        GtkToolbar* toolbar1 = GTK_TOOLBAR(this->mainWindow->get(guiName));
        count += gtk_toolbar_get_n_items(toolbar1);
    }

    return count;
}


void FloatingToolbox::showForConfiguration() {
    if (this->floatingToolboxActivated())  // Do not show if not being used - at least while experimental.
    {
        gint wx = 0, wy = 0;
        gtk_widget_translate_coordinates(this->boxContents, gtk_widget_get_toplevel(boxContents), 0, 0, &wx, &wy);
        this->floatingToolboxX = wx + 40;  // when configuration state these are
        this->floatingToolboxY = wy + 40;  // topleft coordinates( otherwise center).
        this->floatingToolboxState = configuration;
        this->show();
    }
}


void FloatingToolbox::show() {
    gtk_widget_hide(this->floatingToolbox);  // force showing in new position
    gtk_widget_show_all(this->floatingToolbox);

    if (this->floatingToolboxState != configuration) {
        gtk_widget_hide(this->mainWindow->get("labelFloatingToolbox"));
    }

    if (this->floatingToolboxState == configuration || countWidgets() > 0) {
        gtk_widget_hide(this->mainWindow->get("showIfEmpty"));
    }
}


void FloatingToolbox::hide() {
    if (this->floatingToolboxState == configuration) {
        this->floatingToolboxState = recalcSize;
    }

    gtk_widget_hide(this->floatingToolbox);
}


void FloatingToolbox::flagRecalculateSizeRequired() { this->floatingToolboxState = recalcSize; }


/**
 * getOverlayPosition - this is how we position the widget in the overlay under the mouse
 *
 * The requested location is communicated via the FloatingToolbox member variables:
 * ->floatingToolbox,		so we can operate on the right widget
 * ->floatingToolboxState,	are we configuring, resizing or just moving
 * ->floatingToolboxX,		where to display
 * ->floatingToolboxY.
 *
 */
auto FloatingToolbox::getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                         FloatingToolbox* self) -> gboolean {
    if (widget == self->floatingToolbox) {
        gtk_widget_get_allocation(widget, allocation);  // get existing width and height

        if (self->floatingToolboxState != noChange ||
            allocation->height < 2)  // if recalcSize or configuration or initiation.
        {
            GtkRequisition natural;
            gtk_widget_get_preferred_size(widget, nullptr, &natural);
            allocation->width = natural.width;
            allocation->height = natural.height;
        }

        switch (self->floatingToolboxState) {
            case recalcSize:  // fallthrough 		note: recalc done above
            case noChange:
                // start centered on x,y
                allocation->x = self->floatingToolboxX - allocation->width / 2;
                allocation->y = self->floatingToolboxY - allocation->height / 2;
                self->floatingToolboxState = noChange;
                break;

            case configuration:
                allocation->x = self->floatingToolboxX;
                allocation->y = self->floatingToolboxY;
                allocation->width = std::max(allocation->width + 32, 50);  // always room for one more...
                allocation->height = std::max(allocation->height, 50);
                break;
        }

        // Ensure the floating toolbox stays within the window

        GtkAllocation visibleRect;
        gtk_widget_get_allocation(GTK_WIDGET(overlay), &visibleRect);

        // Offscreen on left side?
        const int leftBound = visibleRect.x;
        if (allocation->x < leftBound) {
            allocation->x = leftBound;
        }

        // Offscreen on top side?
        const int topBound = visibleRect.y;
        if (allocation->y < topBound) {
            allocation->y = topBound;
        }

        // Offscreen on right side?
        const int rightBound = visibleRect.width;
        const int rightCurrent = allocation->x + allocation->width;
        if (rightCurrent > rightBound) {
            allocation->x -= rightCurrent - rightBound;
        }

        // Offscreen on bottom side?
        const int bottomBound = visibleRect.height;
        const int bottomCurrent = allocation->y + allocation->height;
        if (bottomCurrent > bottomBound) {
            allocation->y -= bottomCurrent - bottomBound;
        }

        return true;
    }

    return false;
}


void FloatingToolbox::handleLeaveFloatingToolbox(GtkWidget* floatingToolbox, GdkEvent* event, FloatingToolbox* self) {
    if (floatingToolbox == self->floatingToolbox) {
        if (self->floatingToolboxState != configuration) {
            self->hide();
        }
    }
}
