/**
 * @file ExampleExtension.cpp
 * @brief Example Extension Implementation for Point Blank Window Manager
 * 
 * This file demonstrates how to create a custom extension for Point Blank
 * using the v2.0 Extension API with ABI stability guarantees.
 * 
 * Features demonstrated:
 * - Extension lifecycle (initialize/shutdown)
 * - Event subscription and handling
 * - Custom layout provider implementation
 * - Performance monitoring integration
 * 
 * Build command:
 * g++ -shared -fPIC -O3 -o example_extension.so ExampleExtension.cpp
 * 
 * @author Point Blank Extension Template
 * @version 1.0.0
 */


#include "../ExtensionAPI.hpp"

#include <iostream>
#include <cstring>
#include <chrono>
#include <algorithm>





/**
 * @brief Example extension demonstrating the v2.0 API
 * 
 * This extension provides:
 * 1. Window event logging (for debugging)
 * 2. A custom "columns" layout algorithm
 * 3. Performance metrics tracking
 */
class ExampleExtension : public pblank::IExtension {
public:
    
    
    
    
    const pblank::ExtensionInfo* getInfo() const override {
        static pblank::ExtensionInfo info = 
            PB_DEFINE_EXTENSION_INFO(
                "ExampleExtension",           
                "1.0.0",                      
                "Point Blank Team",           
                "Example extension demonstrating the v2.0 API with "
                "event handling and custom layout support",  
                pblank::ExtensionCapability::LayoutProvider | 
                pblank::ExtensionCapability::Performance,    
                pblank::ExtensionPriority::Normal            
            );
        return &info;
    }
    
    
    
    
    
    pblank::Result initialize(const pblank::ExtensionContext* ctx) override {
        if (!ctx || !ctx->display) {
            return pblank::Result::InvalidArgument;
        }
        
        
        display_ = ctx->display;
        root_ = ctx->root;
        screen_ = ctx->screen;
        
        
        init_time_ = std::chrono::steady_clock::now();
        events_handled_ = 0;
        
        std::cout << "[ExampleExtension] Initialized successfully" << std::endl;
        std::cout << "[ExampleExtension] API Version: " 
                  << PB_API_VERSION_MAJOR << "."
                  << PB_API_VERSION_MINOR << "."
                  << PB_API_VERSION_PATCH << std::endl;
        
        return pblank::Result::Success;
    }
    
    pblank::Result shutdown() override {
        std::cout << "[ExampleExtension] Shutting down..." << std::endl;
        std::cout << "[ExampleExtension] Total events handled: " 
                  << events_handled_ << std::endl;
        
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - init_time_).count();
        std::cout << "[ExampleExtension] Uptime: " << uptime << " seconds" << std::endl;
        
        return pblank::Result::Success;
    }
    
    
    
    
    
    pblank::EventMask getEventMask() const override {
        pblank::EventMask mask;
        
        
        mask.set(pblank::EventType::WindowMap);
        mask.set(pblank::EventType::WindowUnmap);
        mask.set(pblank::EventType::WindowFocus);
        mask.set(pblank::EventType::WorkspaceSwitch);
        mask.set(pblank::EventType::LayoutChange);
        
        return mask;
    }
    
    
    
    
    
    bool onWindowMap(const pblank::WindowHandle* window) override {
        ++events_handled_;
        
        
        std::cout << "[ExampleExtension] Window mapped: " 
                  << window->x11_window 
                  << " on workspace " << window->workspace_id
                  << std::endl;
        
        
        return true;
    }
    
    bool onWindowUnmap(const pblank::WindowHandle* window) override {
        ++events_handled_;
        
        std::cout << "[ExampleExtension] Window unmapped: " 
                  << window->x11_window << std::endl;
        
        return true;
    }
    
    bool onWindowFocus(const pblank::WindowHandle* old_win, 
                       const pblank::WindowHandle* new_win) override {
        ++events_handled_;
        
        if (new_win) {
            std::cout << "[ExampleExtension] Focus changed to window: " 
                      << new_win->x11_window << std::endl;
        }
        
        return true;
    }
    
    bool onWorkspaceSwitch(uint32_t old_ws, uint32_t new_ws) override {
        ++events_handled_;
        
        std::cout << "[ExampleExtension] Workspace switched: " 
                  << old_ws << " -> " << new_ws << std::endl;
        
        return true;
    }
    
    bool onLayoutChange(uint32_t workspace, const char* layout_name) override {
        ++events_handled_;
        
        std::cout << "[ExampleExtension] Layout changed on workspace " 
                  << workspace << ": " << layout_name << std::endl;
        
        return true;
    }
    
    
    
    
    
    bool hasLayoutProvider() const override {
        return true;  
    }
    
    const char* getLayoutName() const override {
        return "columns";  
    }
    
    pblank::Result calculateLayout(const pblank::LayoutContext* ctx,
                                   pblank::LayoutOutput* output) override {
        if (!ctx || !output || !ctx->windows || ctx->window_count == 0) {
            return pblank::Result::InvalidArgument;
        }
        
        
        if (output->capacity < ctx->window_count) {
            return pblank::Result::InvalidArgument;
        }
        
        
        
        
        uint32_t count = ctx->window_count;
        int16_t screen_w = ctx->screen_bounds.width;
        int16_t screen_h = ctx->screen_bounds.height;
        int16_t x = ctx->screen_bounds.x;
        int16_t y = ctx->screen_bounds.y;
        
        
        uint16_t col_width = screen_w / count;
        
        
        constexpr uint16_t GAP = 10;
        
        for (uint32_t i = 0; i < count; ++i) {
            pblank::api::v2::Rect16& rect = output->window_rects[i];
            
            rect.x = x + i * col_width + GAP / 2;
            rect.y = y + GAP / 2;
            rect.width = col_width - GAP;
            rect.height = screen_h - GAP;
            
            
            if (i == count - 1) {
                rect.width = screen_w - (rect.x - x) - GAP / 2;
            }
        }
        
        output->count = count;
        
        return pblank::Result::Success;
    }
    
    
    
    
    
    pblank::Nanoseconds getAverageProcessingTime() const override {
        if (events_handled_ == 0) {
            return pblank::Nanoseconds(0);
        }
        
        
        auto total_ns = total_processing_time_ns_.load();
        return pblank::Nanoseconds(total_ns / events_handled_);
    }
    
    bool isHealthy() const override {
        
        
        auto avg_time = getAverageProcessingTime();
        return avg_time.count() < 1000000;  
    }
    
private:
    
    Display* display_{nullptr};
    Window root_{0};
    int screen_{0};
    
    
    std::chrono::steady_clock::time_point init_time_;
    std::atomic<uint64_t> events_handled_{0};
    std::atomic<uint64_t> total_processing_time_ns_{0};
};






PB_DECLARE_EXTENSION(ExampleExtension)





/*
 * The PB_DECLARE_EXTENSION macro expands to the following:
 * 
 * extern "C" {
 *     PB_API_EXPORT pblank::api::v2::IExtension_v2* createExtension_v2() {
 *         return new ExampleExtension();
 *     }
 *     
 *     PB_API_EXPORT void destroyExtension_v2(pblank::api::v2::IExtension_v2* ext) {
 *         delete ext;
 *     }
 *     
 *     PB_API_EXPORT const pblank::api::v2::ExtensionInfo* getExtensionInfo() {
 *         static ExampleExtension instance;
 *         return instance.getInfo();
 *     }
 * }
 */





/*
 * To build this extension as a shared library:
 * 
 * # Debug build (with symbols)
 * g++ -shared -fPIC -g -o example_extension.so ExampleExtension.cpp
 * 
 * # Release build (optimized)
 * g++ -shared -fPIC -O3 -march=native -o example_extension.so ExampleExtension.cpp
 * 
 * # With LTO (link-time optimization)
 * g++ -shared -fPIC -O3 -flto -o example_extension.so ExampleExtension.cpp
 * 
 * Installation:
 * cp example_extension.so ~/.config/pblank/extensions/user/
 * 
 * Then add to your config:
 * #import example_extension
 * 
 * For built-in extensions, use #include and place in:
 * ~/.config/pblank/extensions/pb/
 */
