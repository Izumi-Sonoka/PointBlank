#include "pointblank/core/Toaster.hpp"
#include <X11/Xatom.h>
#include <cmath>
#include <gio/gio.h>
#include <algorithm>

namespace pblank {

Toaster::Toaster(Display* display, Window root)
    : display_(display), root_(root), window_(None) {}

Toaster::~Toaster() {
    cleanupCairo();
    if (window_ != None) {
        XDestroyWindow(display_, window_);
    }
    
    if (colormap_ != 0 && has_argb_) {
        XFreeColormap(display_, colormap_);
    }
}

bool Toaster::initialize() {
    createWindow();
    
    if (!initializeDBus()) {
        
        dbus_initialized_ = false;
    }
    
    return true;
}

void Toaster::createWindow() {
    int screen = DefaultScreen(display_);
    
    
    screen_width_ = DisplayWidth(display_, screen);
    
    
    int total_height = MAX_VISIBLE_NOTIFICATIONS * (NOTIFICATION_HEIGHT + NOTIFICATION_SPACING);
    
    
    toaster_x_ = screen_width_ - NOTIFICATION_WIDTH - TOASTER_MARGIN_RIGHT;
    int toaster_y = TOASTER_MARGIN_TOP;
    
    
    XVisualInfo vinfo_template;
    vinfo_template.screen = screen;
    vinfo_template.depth = 32;
    vinfo_template.c_class = TrueColor;
    
    int nitems = 0;
    XVisualInfo* vinfo = XGetVisualInfo(display_, VisualScreenMask | VisualDepthMask | VisualClassMask,
                                        &vinfo_template, &nitems);
    
    Visual* visual = nullptr;
    
    if (vinfo && nitems > 0) {
        
        visual = vinfo[0].visual;
        colormap_ = XCreateColormap(display_, root_, visual, AllocNone);
        has_argb_ = true;
        XFree(vinfo);
    } else {
        
        visual = DefaultVisual(display_, screen);
        colormap_ = DefaultColormap(display_, screen);
        has_argb_ = false;
    }
    
    
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = 0; 
    attrs.border_pixel = 0;
    attrs.colormap = colormap_;
    attrs.event_mask = ExposureMask;
    attrs.backing_store = NotUseful;  
    
    
    
    unsigned long attr_mask = CWOverrideRedirect | CWBackPixel | CWBorderPixel | 
                              CWColormap | CWEventMask | CWBackingStore;
    
    window_ = XCreateWindow(
        display_, root_,
        toaster_x_, toaster_y,
        NOTIFICATION_WIDTH, total_height,
        0, 
        has_argb_ ? 32 : CopyFromParent, 
        InputOutput, 
        visual,
        attr_mask,
        &attrs
    );
    
    
    
    if (has_argb_) {
        
        XSetWindowBackgroundPixmap(display_, window_, None);
        
        
        Atom opacity_atom = XInternAtom(display_, "_NET_WM_WINDOW_OPACITY", False);
        unsigned long opacity = 0xffffffff; 
        XChangeProperty(display_, window_, opacity_atom, XA_CARDINAL, 32,
                       PropModeReplace, reinterpret_cast<unsigned char*>(&opacity), 1);
    }
    
    
    Atom window_type = XInternAtom(display_, "_NET_WM_WINDOW_TYPE", False);
    Atom notification_type = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    XChangeProperty(display_, window_, window_type, XA_ATOM, 32,
                   PropModeReplace, reinterpret_cast<unsigned char*>(&notification_type), 1);
    
    
    Atom state = XInternAtom(display_, "_NET_WM_STATE", False);
    Atom above = XInternAtom(display_, "_NET_WM_STATE_ABOVE", False);
    Atom sticky = XInternAtom(display_, "_NET_WM_STATE_STICKY", False);
    
    
    Atom states[] = {above, sticky};
    XChangeProperty(display_, window_, state, XA_ATOM, 32,
                   PropModeReplace, reinterpret_cast<unsigned char*>(states), 2);
    
    
    Atom wm_layer = XInternAtom(display_, "_WIN_LAYER", False);
    long layer = 6; 
    XChangeProperty(display_, window_, wm_layer, XA_CARDINAL, 32,
                   PropModeReplace, reinterpret_cast<unsigned char*>(&layer), 1);
    
    
    surface_ = cairo_xlib_surface_create(display_, window_, visual,
                                         NOTIFICATION_WIDTH, total_height);
    cairo_ = cairo_create(surface_);
    
    
    cairo_surface_set_fallback_resolution(surface_, 96.0, 96.0);
    
    
    
}

void Toaster::notify(const std::string& message, NotificationLevel level) {
    Notification notif{
        message,
        level,
        std::chrono::steady_clock::now(),
        std::chrono::milliseconds(1500) 
    };
    
    notifications_.push(notif);
    
    
    while (notifications_.size() > MAX_VISIBLE_NOTIFICATIONS) {
        notifications_.pop();
    }
    
    
    
    render();
    
    
    XMapWindow(display_, window_);
    XRaiseWindow(display_, window_);  
    
    
    XSync(display_, False);
}

void Toaster::error(const std::string& message) {
    notify(message, NotificationLevel::LevelError);
}

void Toaster::success(const std::string& message) {
    notify(message, NotificationLevel::LevelSuccess);
}

void Toaster::info(const std::string& message) {
    notify(message, NotificationLevel::LevelInfo);
}

void Toaster::warning(const std::string& message) {
    notify(message, NotificationLevel::LevelWarning);
}

void Toaster::configError(const std::string& message) {
    Notification notif{
        message,
        NotificationLevel::LevelError,
        std::chrono::steady_clock::now(),
        std::chrono::milliseconds(0),  
        false,  
        true,   
        true    
    };
    
    config_errors_.push(notif);
    has_config_errors_ = true;
    
    
    render();
    
    
    XMapWindow(display_, window_);
    XRaiseWindow(display_, window_);
    XSync(display_, False);
}

void Toaster::clearConfigErrors() {
    
    while (!config_errors_.empty()) {
        config_errors_.pop();
    }
    has_config_errors_ = false;
    
    
    if (!notifications_.empty()) {
        render();
    }
}

void Toaster::update() {
    cleanupExpired();
    
    
    if (has_config_errors_ && !config_errors_.empty()) {
        renderConfigErrors();
    }
    
    
    if (dbus_initialized_) {
        std::queue<Notification> temp_queue;
        while (!notifications_.empty()) {
            auto& notif = notifications_.front();
            
            if (!notif.sent_dbus) {
                sendDBusNotification(notif);
                notif.sent_dbus = true;
            }
            
            temp_queue.push(notif);
            notifications_.pop();
        }
        notifications_ = std::move(temp_queue);
    }
    
    if (!notifications_.empty()) {
        render();
    }
}

void Toaster::render() {
    if (!cairo_) return;
    
    
    XRaiseWindow(display_, window_);
    
    
    int total_height = MAX_VISIBLE_NOTIFICATIONS * (NOTIFICATION_HEIGHT + NOTIFICATION_SPACING);
    
    
    
    cairo_surface_t* offscreen = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 
                                                            NOTIFICATION_WIDTH, total_height);
    cairo_t* offscreen_cr = cairo_create(offscreen);
    
    
    cairo_set_operator(offscreen_cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(offscreen_cr);
    cairo_set_operator(offscreen_cr, CAIRO_OPERATOR_OVER);
    
    
    std::queue<Notification> temp_queue = notifications_;
    int y_offset = 0;
    
    while (!temp_queue.empty()) {
        
        const Notification& notif = temp_queue.front();
        Color color = getColorForLevel(notif.level);
        
        
        double x = 0;
        double y = y_offset;
        double width = NOTIFICATION_WIDTH;
        double height = NOTIFICATION_HEIGHT;
        double radius = 6.0;
        
        
        cairo_new_sub_path(offscreen_cr);
        cairo_arc(offscreen_cr, x + width - radius, y + radius, radius, -M_PI/2, 0);
        cairo_arc(offscreen_cr, x + width - radius, y + height - radius, radius, 0, M_PI/2);
        cairo_arc(offscreen_cr, x + radius, y + height - radius, radius, M_PI/2, M_PI);
        cairo_arc(offscreen_cr, x + radius, y + radius, radius, M_PI, 3*M_PI/2);
        cairo_close_path(offscreen_cr);
        
        
        cairo_set_source_rgba(offscreen_cr, 0.15, 0.15, 0.15, 0.9);
        cairo_fill_preserve(offscreen_cr);
        
        
        cairo_set_source_rgba(offscreen_cr, color.r, color.g, color.b, color.a);
        cairo_set_line_width(offscreen_cr, 2.0);
        cairo_stroke(offscreen_cr);
        
        
        cairo_rectangle(offscreen_cr, x, y + radius, 3, height - 2 * radius);
        cairo_set_source_rgba(offscreen_cr, color.r, color.g, color.b, color.a);
        cairo_fill(offscreen_cr);
        
        
        cairo_select_font_face(offscreen_cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(offscreen_cr, 11.0);
        cairo_set_source_rgba(offscreen_cr, 1.0, 1.0, 1.0, 1.0);
        
        
        cairo_text_extents_t extents;
        std::string display_text = notif.message;
        int max_text_width = NOTIFICATION_WIDTH - NOTIFICATION_PADDING * 2 - 15;
        cairo_text_extents(offscreen_cr, display_text.c_str(), &extents);
        while (extents.width > max_text_width && display_text.length() > 3) {
            display_text = display_text.substr(0, display_text.length() - 4) + "...";
            cairo_text_extents(offscreen_cr, display_text.c_str(), &extents);
        }
        
        double text_x = x + NOTIFICATION_PADDING + 5;
        double text_y = y + height / 2 + extents.height / 2;
        
        cairo_move_to(offscreen_cr, text_x, text_y);
        cairo_show_text(offscreen_cr, display_text.c_str());
        
        
        double icon_x = width - NOTIFICATION_PADDING - 8;
        double icon_y = y + height / 2;
        double icon_radius = 4.0;
        
        cairo_arc(offscreen_cr, icon_x, icon_y, icon_radius, 0, 2 * M_PI);
        cairo_set_source_rgba(offscreen_cr, color.r, color.g, color.b, color.a);
        cairo_fill(offscreen_cr);
        
        y_offset += NOTIFICATION_HEIGHT + NOTIFICATION_SPACING;
        temp_queue.pop();
    }
    
    
    cairo_set_operator(cairo_, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cairo_, offscreen, 0, 0);
    cairo_paint(cairo_);
    
    
    cairo_destroy(offscreen_cr);
    cairo_surface_destroy(offscreen);
    
    
    cairo_surface_flush(surface_);
    
    
    
    XSync(display_, False);
}

void Toaster::renderConfigErrors() {
    if (!cairo_ || config_errors_.empty()) return;
    
    
    int screen = DefaultScreen(display_);
    int screen_width = DisplayWidth(display_, screen);
    
    (void)0;  
    
    
    int window_width = CONFIG_ERROR_WIDTH;
    int window_height = CONFIG_ERROR_HEIGHT;
    int x = (screen_width - window_width) / 2;
    int y = 50;  
    
    
    XMoveResizeWindow(display_, window_, x, y, window_width, window_height);
    
    
    cleanupCairo();
    
    Visual* visual = has_argb_ ? 
        reinterpret_cast<Visual*>(cairo_xlib_surface_get_visual(surface_)) : 
        DefaultVisual(display_, screen);
    
    surface_ = cairo_xlib_surface_create(display_, window_, visual, window_width, window_height);
    cairo_ = cairo_create(surface_);
    
    
    cairo_surface_t* offscreen = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 
                                                            window_width, window_height);
    cairo_t* offscreen_cr = cairo_create(offscreen);
    
    
    cairo_set_operator(offscreen_cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(offscreen_cr);
    cairo_set_operator(offscreen_cr, CAIRO_OPERATOR_OVER);
    
    
    std::queue<Notification> temp = config_errors_;
    int y_offset = 0;
    
    while (!temp.empty()) {
        const Notification& notif = temp.front();
        
        [[maybe_unused]] Color color = getColorForLevel(notif.level);
        
        double radius = 8.0;
        
        
        cairo_new_sub_path(offscreen_cr);
        cairo_arc(offscreen_cr, window_width - radius, y_offset + radius, radius, -M_PI/2, 0);
        cairo_arc(offscreen_cr, window_width - radius, y_offset + window_height - radius, radius, 0, M_PI/2);
        cairo_arc(offscreen_cr, radius, y_offset + window_height - radius, radius, M_PI/2, M_PI);
        cairo_arc(offscreen_cr, radius, y_offset + radius, radius, M_PI, 3*M_PI/2);
        cairo_close_path(offscreen_cr);
        
        
        cairo_set_source_rgba(offscreen_cr, 0.2, 0.0, 0.0, 0.95);
        cairo_fill_preserve(offscreen_cr);
        
        
        cairo_set_source_rgba(offscreen_cr, 1.0, 0.0, 0.0, 1.0);
        cairo_set_line_width(offscreen_cr, 2.0);
        cairo_stroke(offscreen_cr);
        
        
        double icon_x = 20;
        double icon_y = y_offset + window_height / 2;
        double icon_size = 12;
        
        cairo_set_source_rgba(offscreen_cr, 1.0, 0.3, 0.3, 1.0);
        cairo_set_line_width(offscreen_cr, 2.0);
        cairo_move_to(offscreen_cr, icon_x - icon_size/2, icon_y - icon_size/2);
        cairo_line_to(offscreen_cr, icon_x + icon_size/2, icon_y + icon_size/2);
        cairo_move_to(offscreen_cr, icon_x + icon_size/2, icon_y - icon_size/2);
        cairo_line_to(offscreen_cr, icon_x - icon_size/2, icon_y + icon_size/2);
        cairo_stroke(offscreen_cr);
        
        
        cairo_select_font_face(offscreen_cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(offscreen_cr, 12.0);
        cairo_set_source_rgba(offscreen_cr, 1.0, 1.0, 1.0, 1.0);
        
        
        std::string display_text = notif.message;
        cairo_text_extents_t extents;
        int max_text_width = window_width - 50;
        cairo_text_extents(offscreen_cr, display_text.c_str(), &extents);
        while (extents.width > max_text_width && display_text.length() > 3) {
            display_text = display_text.substr(0, display_text.length() - 4) + "...";
            cairo_text_extents(offscreen_cr, display_text.c_str(), &extents);
        }
        
        double text_x = 40;
        double text_y = y_offset + window_height / 2 + extents.height / 2;
        cairo_move_to(offscreen_cr, text_x, text_y);
        cairo_show_text(offscreen_cr, display_text.c_str());
        
        temp.pop();
    }
    
    
    cairo_set_operator(cairo_, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cairo_, offscreen, 0, 0);
    cairo_paint(cairo_);
    
    cairo_destroy(offscreen_cr);
    cairo_surface_destroy(offscreen);
    
    cairo_surface_flush(surface_);
    XSync(display_, False);
}

void Toaster::renderNotification(const Notification& notif, int y_offset) {
    Color color = getColorForLevel(notif.level);
    
    
    double x = 0;
    double y = y_offset;
    double width = NOTIFICATION_WIDTH;
    double height = NOTIFICATION_HEIGHT;
    double radius = 6.0;
    
    
    cairo_new_sub_path(cairo_);
    cairo_arc(cairo_, x + width - radius, y + radius, radius, -M_PI/2, 0);
    cairo_arc(cairo_, x + width - radius, y + height - radius, radius, 0, M_PI/2);
    cairo_arc(cairo_, x + radius, y + height - radius, radius, M_PI/2, M_PI);
    cairo_arc(cairo_, x + radius, y + radius, radius, M_PI, 3*M_PI/2);
    cairo_close_path(cairo_);
    
    
    cairo_set_source_rgba(cairo_, 0.15, 0.15, 0.15, 0.9);
    cairo_fill_preserve(cairo_);
    
    
    cairo_set_source_rgba(cairo_, color.r, color.g, color.b, color.a);
    cairo_set_line_width(cairo_, 2.0);
    cairo_stroke(cairo_);
    
    
    cairo_rectangle(cairo_, x, y + radius, 3, height - 2 * radius);
    cairo_set_source_rgba(cairo_, color.r, color.g, color.b, color.a);
    cairo_fill(cairo_);
    
    
    cairo_select_font_face(cairo_, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cairo_, 11.0);
    cairo_set_source_rgba(cairo_, 1.0, 1.0, 1.0, 1.0);
    
    
    cairo_text_extents_t extents;
    std::string display_text = notif.message;
    
    
    int max_text_width = NOTIFICATION_WIDTH - NOTIFICATION_PADDING * 2 - 15;
    cairo_text_extents(cairo_, display_text.c_str(), &extents);
    while (extents.width > max_text_width && display_text.length() > 3) {
        display_text = display_text.substr(0, display_text.length() - 4) + "...";
        cairo_text_extents(cairo_, display_text.c_str(), &extents);
    }
    
    double text_x = x + NOTIFICATION_PADDING + 5;
    double text_y = y + height / 2 + extents.height / 2;
    
    cairo_move_to(cairo_, text_x, text_y);
    cairo_show_text(cairo_, display_text.c_str());
    
    
    double icon_x = width - NOTIFICATION_PADDING - 8;
    double icon_y = y + height / 2;
    double icon_radius = 4.0;
    
    cairo_arc(cairo_, icon_x, icon_y, icon_radius, 0, 2 * M_PI);
    cairo_set_source_rgba(cairo_, color.r, color.g, color.b, color.a);
    cairo_fill(cairo_);
}

void Toaster::cleanupExpired() {
    auto now = std::chrono::steady_clock::now();
    
    std::queue<Notification> new_queue;
    bool changed = false;
    
    while (!notifications_.empty()) {
        auto& notif = notifications_.front();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - notif.created_at);
        
        
        if (notif.persistent || elapsed < notif.duration) {
            new_queue.push(notif);
        } else {
            changed = true;
        }
        
        notifications_.pop();
    }
    
    notifications_ = std::move(new_queue);
    
    if (changed) {
        if (notifications_.empty() && !has_config_errors_) {
            
            XUnmapWindow(display_, window_);
            XFlush(display_);
        } else {
            
            render();
        }
    }
}

Toaster::Color Toaster::getColorForLevel(NotificationLevel level) const {
    switch (level) {
        case NotificationLevel::LevelError:
            return {1.0, 0.0, 0.0, 1.0}; 
        case NotificationLevel::LevelSuccess:
            return {0.0, 1.0, 0.0, 1.0}; 
        case NotificationLevel::LevelWarning:
            return {1.0, 1.0, 0.0, 1.0}; 
        case NotificationLevel::LevelInfo:
            return {0.0, 0.5, 1.0, 1.0}; 
        default:
            return {1.0, 1.0, 1.0, 1.0}; 
    }
}

bool Toaster::initializeDBus() {
    
    static bool glib_initialized = false;
    if (!glib_initialized) {
        #if !GLIB_CHECK_VERSION(2, 36, 0)
        g_type_init(); 
        #endif
        glib_initialized = true;
    }
    
    dbus_initialized_ = true;
    return true;
}

void Toaster::sendDBusNotification(const Notification& notif) {
    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    
    if (!connection) {
        if (error) {
            g_error_free(error);
        }
        return;
    }
    
    
    const char* icon;
    
    switch (notif.level) {
        case NotificationLevel::LevelError:
            icon = "dialog-error";
            break;
        case NotificationLevel::LevelSuccess:
            icon = "dialog-information";
            break;
        case NotificationLevel::LevelWarning:
            icon = "dialog-warning";
            break;
        case NotificationLevel::LevelInfo:
        default:
            icon = "dialog-information";
            break;
    }
    
    
    GVariantBuilder actions_builder;
    g_variant_builder_init(&actions_builder, G_VARIANT_TYPE("as"));
    GVariant* actions_variant = g_variant_builder_end(&actions_builder);
    
    
    GVariantBuilder hints_builder;
    g_variant_builder_init(&hints_builder, G_VARIANT_TYPE("a{sv}"));
    
    
    guchar urgency_byte = (notif.level == NotificationLevel::LevelError) ? 2 : 1;
    g_variant_builder_add(&hints_builder, "{sv}", "urgency", 
                         g_variant_new_byte(urgency_byte));
    
    GVariant* hints_variant = g_variant_builder_end(&hints_builder);
    
    
    
    GVariant* parameters = g_variant_new(
        "(susss@as@a{sv}i)",
        "Point Blank",       
        (guint32)0,          
        icon,                
        "Point Blank",       
        notif.message.c_str(), 
        actions_variant,     
        hints_variant,       
        1500                 
    );
    
    
    g_dbus_connection_call(
        connection,
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        "Notify",
        parameters,
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        nullptr,
        nullptr
    );
    
    g_object_unref(connection);
}

void Toaster::cleanupCairo() {
    if (cairo_) {
        cairo_destroy(cairo_);
        cairo_ = nullptr;
    }
    
    if (surface_) {
        cairo_surface_destroy(surface_);
        surface_ = nullptr;
    }
}



DBusConnection::DBusConnection() : connection_(nullptr) {
    GError* error = nullptr;
    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    
    if (error) {
        g_error_free(error);
    } else {
        connection_ = conn;
    }
}

DBusConnection::~DBusConnection() {
    cleanup();
}

void DBusConnection::cleanup() {
    if (connection_) {
        g_object_unref(static_cast<GDBusConnection*>(connection_));
        connection_ = nullptr;
    }
}

bool DBusConnection::sendNotification(
    const std::string& summary,
    const std::string& body,
    const std::string& icon,
    int timeout_ms)
{
    if (!connection_) return false;

    
    GVariantBuilder actions_builder;
    g_variant_builder_init(&actions_builder, G_VARIANT_TYPE("as"));
    GVariant* actions_variant = g_variant_ref_sink(g_variant_builder_end(&actions_builder));

    
    GVariantBuilder hints_builder;
    g_variant_builder_init(&hints_builder, G_VARIANT_TYPE("a{sv}"));
    GVariant* hints_variant = g_variant_ref_sink(g_variant_builder_end(&hints_builder));

    
    
    GVariant* parameters = g_variant_new(
        "(susssasa{sv}i)",
        "Point Blank",
        (guint32)0,
        icon.c_str(),
        summary.c_str(),
        body.c_str(),
        actions_variant,
        hints_variant,
        (gint32)timeout_ms
    );

    GError* error = nullptr;
    
    g_dbus_connection_call_sync(
        static_cast<GDBusConnection*>(connection_),
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        "Notify",
        parameters,
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        &error
    );

    if (error) {
        g_error_free(error);
        return false;
    }
    return true;
}

} 
