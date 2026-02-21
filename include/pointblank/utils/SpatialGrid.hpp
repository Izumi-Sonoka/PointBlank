#pragma once

/**
 * @file SpatialGrid.hpp
 * @brief Spatial partitioning system for infinite canvas visibility management
 * 
 * Divides the 32-bit virtual world into chunks (2000x2000 pixel cells).
 * Implements a spatial hash grid for O(1) visibility queries.
 * Only windows in visible chunks (current + adjacent) are mapped in X11.
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <X11/Xlib.h>
#include "pointblank/utils/Camera.hpp"

namespace pblank {

constexpr int CHUNK_SIZE = 2000;

struct ChunkCoord {
    int x;
    int y;
    
    bool operator==(const ChunkCoord& other) const {
        return x == other.x && y == other.y;
    }
    
    bool operator!=(const ChunkCoord& other) const {
        return !(*this == other);
    }
    
    std::vector<ChunkCoord> getNeighbors() const {
        return {
            { x - 1, y - 1 }, { x, y - 1 }, { x + 1, y - 1 },
            { x - 1, y     },                 { x + 1, y     },
            { x - 1, y + 1 }, { x, y + 1 }, { x + 1, y + 1 }
        };
    }
    
    std::vector<ChunkCoord> getVisibleSet() const {
        std::vector<ChunkCoord> result;
        result.reserve(9);
        result.push_back(*this);
        auto neighbors = getNeighbors();
        result.insert(result.end(), neighbors.begin(), neighbors.end());
        return result;
    }
    
    struct Hash {
        size_t operator()(const ChunkCoord& c) const {
            
            return std::hash<int64_t>()(
                (static_cast<int64_t>(c.x) << 32) | 
                static_cast<int64_t>(static_cast<uint32_t>(c.y))
            );
        }
    };
};

class SpatialGrid;

inline ChunkCoord toChunkCoord(int64_t virtual_x, int64_t virtual_y) {
    
    int cx = static_cast<int>(virtual_x >= 0 ? virtual_x / CHUNK_SIZE : 
                          (virtual_x - CHUNK_SIZE + 1) / CHUNK_SIZE);
    int cy = static_cast<int>(virtual_y >= 0 ? virtual_y / CHUNK_SIZE : 
                          (virtual_y - CHUNK_SIZE + 1) / CHUNK_SIZE);
    return { cx, cy };
}

struct WindowEntry {
    Window window;
    int64_t virtual_x;
    int64_t virtual_y;
    unsigned int width;
    unsigned int height;
    
    VirtualRect getVirtualRect() const {
        return { virtual_x, virtual_y, width, height };
    }
    
    ChunkCoord getPrimaryChunk() const;
    
    std::vector<ChunkCoord> getIntersectingChunks() const;
};

class SpatialGrid {
public:
    SpatialGrid() = default;
    
    void addWindow(Window window, int64_t virtual_x, int64_t virtual_y,
                   unsigned int width, unsigned int height);
    
    void removeWindow(Window window);
    
    void updateWindow(Window window, int64_t virtual_x, int64_t virtual_y,
                      unsigned int width, unsigned int height);
    
    std::unordered_set<Window> getVisibleWindows(const Camera& camera) const;
    
    std::unordered_set<Window> getMappableWindows(const Camera& camera) const;
    
    std::unordered_set<Window> getWindowsInChunk(const ChunkCoord& chunk) const;
    
    std::vector<ChunkCoord> getIntersectingChunks(int64_t x, int64_t y,
                                                   unsigned int width, 
                                                   unsigned int height) const;
    
    static ChunkCoord toChunkCoord(int64_t virtual_x, int64_t virtual_y) {
        
        int cx = static_cast<int>(virtual_x >= 0 ? virtual_x / CHUNK_SIZE : 
                              (virtual_x - CHUNK_SIZE + 1) / CHUNK_SIZE);
        int cy = static_cast<int>(virtual_y >= 0 ? virtual_y / CHUNK_SIZE : 
                              (virtual_y - CHUNK_SIZE + 1) / CHUNK_SIZE);
        return { cx, cy };
    }
    
    static VirtualRect getChunkBounds(const ChunkCoord& chunk) {
        return {
            static_cast<int64_t>(chunk.x) * CHUNK_SIZE,
            static_cast<int64_t>(chunk.y) * CHUNK_SIZE,
            static_cast<unsigned int>(CHUNK_SIZE),
            static_cast<unsigned int>(CHUNK_SIZE)
        };
    }
    
    std::vector<ChunkCoord> getVisibleChunks(const Camera& camera) const;
    
    std::vector<ChunkCoord> getLoadableChunks(const Camera& camera) const;
    
    const WindowEntry* getWindowEntry(Window window) const;
    
    bool hasWindow(Window window) const {
        return windows_.count(window) > 0;
    }
    
    size_t getWindowCount() const {
        return windows_.size();
    }
    
    size_t getChunkCount() const {
        return chunks_.size();
    }
    
    void clear() {
        chunks_.clear();
        windows_.clear();
        window_chunks_.clear();
    }
    
    const std::unordered_map<Window, WindowEntry>& getAllWindows() const {
        return windows_;
    }
    
    Window findNearestWindow(int64_t virtual_x, int64_t virtual_y) const;
    
    std::vector<Window> findWindowsInRadius(int64_t virtual_x, int64_t virtual_y,
                                            int64_t radius) const;

private:
    
    std::unordered_map<ChunkCoord, std::unordered_set<Window>, ChunkCoord::Hash> chunks_;
    
    std::unordered_map<Window, WindowEntry> windows_;
    
    std::unordered_map<Window, std::unordered_set<ChunkCoord, ChunkCoord::Hash>> window_chunks_;
    
    void addToChunk(Window window, const ChunkCoord& chunk);
    
    void removeFromChunk(Window window, const ChunkCoord& chunk);
};

} 