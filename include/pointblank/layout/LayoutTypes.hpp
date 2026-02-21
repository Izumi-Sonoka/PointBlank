#pragma once

#include <string>

namespace pblank {

/**
 * @brief Layout mode enumeration
 */
enum class LayoutMode {
    BSP,
    Monocle,
    MasterStack,
    CenteredMaster,
    DynamicGrid,
    DwindleSpiral,
    GoldenRatio,
    TabbedStacked,
    InfiniteCanvas
};

enum class LayoutCycleDirection {
    Forward,   
    Backward   
};

inline std::string layoutModeToString(LayoutMode mode) {
    switch (mode) {
        case LayoutMode::BSP: return "BSP";
        case LayoutMode::Monocle: return "Monocle";
        case LayoutMode::MasterStack: return "MasterStack";
        case LayoutMode::CenteredMaster: return "CenteredMaster";
        case LayoutMode::DynamicGrid: return "DynamicGrid";
        case LayoutMode::DwindleSpiral: return "DwindleSpiral";
        case LayoutMode::GoldenRatio: return "GoldenRatio";
        case LayoutMode::TabbedStacked: return "TabbedStacked";
        case LayoutMode::InfiniteCanvas: return "InfiniteCanvas";
        default: return "Unknown";
    }
}

} 
