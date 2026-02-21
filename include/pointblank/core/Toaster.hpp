#pragma once

#include <memory>
#include <string>
#include <queue>
#include <chrono>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

namespace pblank {

/**
 * @brief Notification severity levels
 */
enum class NotificationLevel {
    LevelError,      
    LevelSuccess,    
    LevelInfo,       
    LevelWarning     
};

struct Notification {
    std::string message;
    NotificationLevel level;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::milliseconds duration;
    bool sent_dbus{false};
    bool persistent{false};  
    bool is_config_error{false};  
};

class Toaster {
public:
    
    explicit Toaster(Display* display, Window root);
    ~Toaster();

    Toaster(const Toaster&) = delete;
    Toaster& operator=(const Toaster&) = delete;
    Toaster(Toaster&&) = delete;
    Toaster& operator=(Toaster&&) = delete;

    bool initialize();

    void error(const std::string& message);

    void success(const std::string& message);

    void info(const std::string& message);

    void warning(const std::string& message);
    
    void configError(const std::string& message);
    
    void clearConfigErrors();

    void update();

    Window getWindow() const { return window_; }

private:
    Display* display_;
    Window root_;
    Window window_;
    Colormap colormap_{0};  
    bool has_argb_{false};  
    
    cairo_surface_t* surface_{nullptr};
    cairo_t* cairo_{nullptr};
    
    std::queue<Notification> notifications_;
    static constexpr size_t MAX_VISIBLE_NOTIFICATIONS = 3;
    static constexpr int NOTIFICATION_WIDTH = 280;
    static constexpr int NOTIFICATION_HEIGHT = 50;
    static constexpr int NOTIFICATION_SPACING = 8;
    static constexpr int NOTIFICATION_PADDING = 10;
    
    static constexpr int TOASTER_MARGIN_RIGHT = 15;
    static constexpr int TOASTER_MARGIN_TOP = 15;
    int screen_width_{0};
    int toaster_x_{0};
    
    enum class AnimationState { Hidden, SlidingIn, Visible, SlidingOut };
    AnimationState animation_state_{AnimationState::Hidden};
    std::chrono::steady_clock::time_point animation_start_time_;
    static constexpr int ANIMATION_DURATION_MS = 200;
    
    static constexpr int CONFIG_ERROR_WIDTH = 400;
    static constexpr int CONFIG_ERROR_HEIGHT = 60;
    std::queue<Notification> config_errors_;  
    bool has_config_errors_{false};
    
    bool dbus_initialized_{false};
    
    void notify(const std::string& message, NotificationLevel level);
    void createWindow();
    void render();
    void renderNotification(const Notification& notif, int y_offset);
    void renderConfigErrors();
    void cleanupExpired();
    
    bool initializeDBus();
    void sendDBusNotification(const Notification& notif);
    
    struct Color {
        double r, g, b, a;
    };
    Color getColorForLevel(NotificationLevel level) const;
    
    void cleanupCairo();
};

class DBusConnection {
public:
    DBusConnection();
    ~DBusConnection();
    
    bool isValid() const { return connection_ != nullptr; }
    
    bool sendNotification(
        const std::string& summary,
        const std::string& body,
        const std::string& icon,
        int timeout_ms = 5000
    );
    
private:
    void* connection_; 
    void cleanup();
};

} 
