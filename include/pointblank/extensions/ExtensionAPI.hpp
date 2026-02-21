#pragma once

/**
 * @file ExtensionAPI.hpp
 * @brief Core Extension API with ABI Stability and Symbol Versioning
 * 
 * This file defines the stable ABI for Point Blank extensions. All extensions
 * must implement the versioned interface to ensure runtime compatibility.
 * 
 * ABI Version: 2.0.0
 * Symbol Versioning: PB_API_2.0
 * 
 * @author Point Blank Systems Engineering Team
 * @version 2.0.0
 */

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <atomic>
#include <chrono>

#include <X11/Xlib.h>

#ifdef None
    #undef None
#endif
#ifdef Success
    #undef Success
#endif

#define PB_API_VERSION_MAJOR 2
#define PB_API_VERSION_MINOR 0
#define PB_API_VERSION_PATCH 0

#ifdef __GNUC__
    #define PB_API_EXPORT __attribute__((visibility("default")))
    #define PB_API_LOCAL  __attribute__((visibility("hidden")))
    #define PB_API_VERSIONED_SYMBOL(sym, ver) __asm__(".symver " #sym "," #sym "@" #ver)
    #define PB_API_DEFAULT_SYMBOL(sym, ver) __asm__(".symver " #sym "," #sym "@@" #ver)
#else
    #define PB_API_EXPORT
    #define PB_API_LOCAL
    #define PB_API_VERSIONED_SYMBOL(sym, ver)
    #define PB_API_DEFAULT_SYMBOL(sym, ver)
#endif

#define PB_API_VERSION_CHECK(major, minor, patch) \
    (PB_API_VERSION_MAJOR > (major) || \
     (PB_API_VERSION_MAJOR == (major) && PB_API_VERSION_MINOR > (minor)) || \
     (PB_API_VERSION_MAJOR == (major) && PB_API_VERSION_MINOR == (minor) && PB_API_VERSION_PATCH >= (patch)))

namespace pblank {
namespace api {
namespace v2 {

using Timestamp = std::chrono::steady_clock::time_point;
using Nanoseconds = std::chrono::nanoseconds;
using Microseconds = std::chrono::microseconds;

template<typename T>
struct alignas(64) CacheAligned {
    T value;
    char padding[64 - sizeof(T) % 64];
    
    CacheAligned() : value{} {}
    explicit CacheAligned(const T& v) : value(v) {}
    explicit CacheAligned(T&& v) : value(std::move(v)) {}
    
    operator T() const { return value; }
    CacheAligned& operator=(const T& v) { value = v; return *this; }
    CacheAligned& operator=(T&& v) { value = std::move(v); return *this; }
};

using AtomicFlag = std::atomic<bool>;
using AtomicCounter = std::atomic<uint64_t>;

struct alignas(16) Rect16 {
    int16_t x, y;
    uint16_t width, height;
    
    constexpr int area() const { return static_cast<int>(width) * height; }
    constexpr bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

struct WindowHandle {
    uint64_t x11_window;      
    uint32_t workspace_id;    
    uint32_t flags;           
    
    static constexpr uint32_t FLAG_FLOATING   = 1 << 0;
    static constexpr uint32_t FLAG_FULLSCREEN = 1 << 1;
    static constexpr uint32_t FLAG_HIDDEN     = 1 << 2;
    static constexpr uint32_t FLAG_URGENT     = 1 << 3;
};

enum class EventType : uint32_t {
    WindowMap         = 0x0001,
    WindowUnmap       = 0x0002,
    WindowDestroy     = 0x0004,
    WindowFocus       = 0x0008,
    WindowMove        = 0x0010,
    WindowResize      = 0x0020,
    WorkspaceSwitch   = 0x0040,
    LayoutChange      = 0x0080,
    ConfigReload      = 0x0100,
    PreRender         = 0x0200,
    PostRender        = 0x0400,
    All               = 0xFFFFFFFF
};

struct EventMask {
    uint32_t mask;
    
    constexpr EventMask() : mask(0) {}
    constexpr explicit EventMask(uint32_t m) : mask(m) {}
    constexpr EventMask(EventType t) : mask(static_cast<uint32_t>(t)) {}
    
    constexpr bool has(EventType t) const { return mask & static_cast<uint32_t>(t); }
    constexpr void set(EventType t) { mask |= static_cast<uint32_t>(t); }
    constexpr void clear(EventType t) { mask &= ~static_cast<uint32_t>(t); }
    
    constexpr EventMask operator|(EventMask other) const { return EventMask(mask | other.mask); }
    constexpr EventMask operator&(EventMask other) const { return EventMask(mask & other.mask); }
};

enum class ExtensionCapability : uint64_t {
    None              = 0,
    LayoutProvider    = 1 << 0,   
    EventFilter       = 1 << 1,   
    Renderer          = 1 << 2,   
    Compositor        = 1 << 3,   
    InputHandler      = 1 << 4,   
    ConfigProvider    = 1 << 5,   
    Performance       = 1 << 6,   
    All               = ~0ULL
};

enum class ExtensionPriority : int32_t {
    Lowest    = -1000,
    Low       = -500,
    Normal    = 0,
    High      = 500,
    Highest   = 1000,
    Critical  = 10000   
};

enum class Result : int32_t {
    Success           = 0,
    InvalidArgument   = -1,
    NotSupported      = -2,
    OutOfMemory       = -3,
    InvalidState      = -4,
    PermissionDenied  = -5,
    VersionMismatch   = -6,
    SymbolNotFound    = -7,
    InitializationFailed = -8,
    ShutdownFailed    = -9
};

struct ExtensionInfo {
    
    char name[64];
    char version[32];
    char author[64];
    char description[256];
    
    uint32_t api_version_major;
    uint32_t api_version_minor;
    uint32_t api_version_patch;
    
    uint64_t capabilities;
    int32_t priority;
    uint32_t reserved[4];
    
    uint64_t api_checksum;
};

struct ExtensionContext {
    
    Display* display;
    Window root;
    int screen;
    
    const WindowHandle* focused_window;
    uint32_t current_workspace;
    uint32_t workspace_count;
    
    Timestamp frame_start;
    Nanoseconds frame_budget;
    AtomicCounter* frame_counter;
    
    void* reserved[8];
};

struct LayoutContext {
    Rect16 screen_bounds;
    uint32_t window_count;
    const WindowHandle* windows;
    void* layout_data;  
};

struct LayoutOutput {
    Rect16* window_rects;   
    uint32_t count;         
    uint32_t capacity;      
};

class IExtension_v2 {
public:
    
    static constexpr uint32_t VERSION_MAJOR = PB_API_VERSION_MAJOR;
    static constexpr uint32_t VERSION_MINOR = PB_API_VERSION_MINOR;
    static constexpr uint32_t VERSION_PATCH = PB_API_VERSION_PATCH;
    
    virtual ~IExtension_v2() = default;
    
    virtual const ExtensionInfo* getInfo() const = 0;
    
    virtual Result initialize(const ExtensionContext* context) = 0;
    
    virtual Result shutdown() = 0;
    
    virtual EventMask getEventMask() const { return EventMask(); }
    
    virtual bool onWindowMap(const WindowHandle* window) { return true; }
    
    virtual bool onWindowUnmap(const WindowHandle* window) { return true; }
    
    virtual bool onWindowDestroy(const WindowHandle* window) { return true; }
    
    virtual bool onWindowFocus(const WindowHandle* old_win, const WindowHandle* new_win) { return true; }
    
    virtual bool onWindowMove(const WindowHandle* window, int16_t x, int16_t y) { return true; }
    
    virtual bool onWindowResize(const WindowHandle* window, uint16_t w, uint16_t h) { return true; }
    
    virtual bool onWorkspaceSwitch(uint32_t old_ws, uint32_t new_ws) { return true; }
    
    virtual bool onLayoutChange(uint32_t workspace, const char* layout_name) { return true; }
    
    virtual bool onConfigReload() { return true; }
    
    virtual bool hasLayoutProvider() const { return false; }
    
    virtual const char* getLayoutName() const { return nullptr; }
    
    virtual Result calculateLayout(const LayoutContext* ctx, LayoutOutput* output) {
        return Result::NotSupported;
    }
    
    virtual void onPreRender() {}
    
    virtual void onPostRender() {}
    
    virtual Nanoseconds getAverageProcessingTime() const { return Nanoseconds(0); }
    
    virtual bool isHealthy() const { return true; }
};

using CreateExtensionFunc_v2 = IExtension_v2* (*)();

using DestroyExtensionFunc_v2 = void (*)(IExtension_v2*);

using GetExtensionInfoFunc = const ExtensionInfo* (*)();

constexpr uint64_t computeAPIChecksum() {
    
    uint64_t hash = 14695981039346656037ULL; 
    hash ^= PB_API_VERSION_MAJOR;
    hash *= 1099511628211ULL;
    hash ^= PB_API_VERSION_MINOR;
    hash *= 1099511628211ULL;
    hash ^= PB_API_VERSION_PATCH;
    hash *= 1099511628211ULL;
    hash ^= sizeof(ExtensionInfo);
    hash *= 1099511628211ULL;
    hash ^= sizeof(ExtensionContext);
    hash *= 1099511628211ULL;
    hash ^= sizeof(WindowHandle);
    hash *= 1099511628211ULL;
    return hash;
}

constexpr uint64_t API_CHECKSUM = computeAPIChecksum();

} 
} 

using IExtension = api::v2::IExtension_v2;
using ExtensionInfo = api::v2::ExtensionInfo;
using ExtensionContext = api::v2::ExtensionContext;
using ExtensionCapability = api::v2::ExtensionCapability;
using ExtensionPriority = api::v2::ExtensionPriority;
using EventMask = api::v2::EventMask;
using EventType = api::v2::EventType;
using Result = api::v2::Result;
using WindowHandle = api::v2::WindowHandle;
using LayoutContext = api::v2::LayoutContext;
using LayoutOutput = api::v2::LayoutOutput;
using Timestamp = api::v2::Timestamp;
using Nanoseconds = api::v2::Nanoseconds;

} 

#define PB_DECLARE_EXTENSION(ClassName) \
    extern "C" { \
        PB_API_EXPORT pblank::api::v2::IExtension_v2* createExtension_v2() { \
            return new ClassName(); \
        } \
        PB_API_EXPORT void destroyExtension_v2(pblank::api::v2::IExtension_v2* ext) { \
            delete ext; \
        } \
        PB_API_EXPORT const pblank::api::v2::ExtensionInfo* getExtensionInfo() { \
            static ClassName instance; \
            return instance.getInfo(); \
        } \
    }

#define PB_DEFINE_EXTENSION_INFO(name, version, author, desc, caps, priority) \
    pblank::api::v2::ExtensionInfo { \
        name, version, author, desc, \
        pblank::api::v2::PB_API_VERSION_MAJOR, \
        pblank::api::v2::PB_API_VERSION_MINOR, \
        pblank::api::v2::PB_API_VERSION_PATCH, \
        static_cast<uint64_t>(caps), \
        static_cast<int32_t>(priority), \
        {0, 0, 0, 0}, \
        pblank::api::v2::API_CHECKSUM \
    }
