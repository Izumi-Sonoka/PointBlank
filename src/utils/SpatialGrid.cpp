/**
 * @file SpatialGrid.cpp
 * @brief Implementation of spatial partitioning system
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include "pointblank/utils/SpatialGrid.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace pblank {

// ============================================================================
// WindowEntry Implementation
// ============================================================================

ChunkCoord WindowEntry::getPrimaryChunk() const {
    return SpatialGrid::toChunkCoord(virtual_x, virtual_y);
}

std::vector<ChunkCoord> WindowEntry::getIntersectingChunks() const {
    std::vector<ChunkCoord> result;
    
    // Get chunk range
    ChunkCoord min_chunk = SpatialGrid::toChunkCoord(virtual_x, virtual_y);
    ChunkCoord max_chunk = SpatialGrid::toChunkCoord(virtual_x + width - 1, virtual_y + height - 1);
    
    // Iterate over all chunks in range
    for (int cx = min_chunk.x; cx <= max_chunk.x; ++cx) {
        for (int cy = min_chunk.y; cy <= max_chunk.y; ++cy) {
            result.push_back({ cx, cy });
        }
    }
    
    return result;
}

// ============================================================================
// SpatialGrid Implementation
// ============================================================================

void SpatialGrid::addWindow(Window window, int64_t virtual_x, int64_t virtual_y,
                            unsigned int width, unsigned int height) {
    // Remove if already exists
    if (hasWindow(window)) {
        removeWindow(window);
    }
    
    // Create entry
    WindowEntry entry{ window, virtual_x, virtual_y, width, height };
    windows_[window] = entry;
    
    // Add to all intersecting chunks
    auto intersecting = getIntersectingChunks(virtual_x, virtual_y, width, height);
    for (const auto& chunk : intersecting) {
        addToChunk(window, chunk);
    }
}

void SpatialGrid::removeWindow(Window window) {
    auto it = windows_.find(window);
    if (it == windows_.end()) return;
    
    // Remove from all chunks
    auto chunk_it = window_chunks_.find(window);
    if (chunk_it != window_chunks_.end()) {
        for (const auto& chunk : chunk_it->second) {
            removeFromChunk(window, chunk);
        }
        window_chunks_.erase(chunk_it);
    }
    
    windows_.erase(it);
}

void SpatialGrid::updateWindow(Window window, int64_t virtual_x, int64_t virtual_y,
                               unsigned int width, unsigned int height) {
    // Re-add with new position
    addWindow(window, virtual_x, virtual_y, width, height);
}

std::unordered_set<Window> SpatialGrid::getVisibleWindows(const Camera& camera) const {
    std::unordered_set<Window> result;
    
    // Get visible chunks
    auto visible_chunks = getVisibleChunks(camera);
    
    // Collect windows from visible chunks
    for (const auto& chunk : visible_chunks) {
        auto it = chunks_.find(chunk);
        if (it != chunks_.end()) {
            for (Window win : it->second) {
                // Verify window is actually visible (not just in a visible chunk)
                auto entry_it = windows_.find(win);
                if (entry_it != windows_.end()) {
                    if (camera.isVisible(entry_it->second.getVirtualRect())) {
                        result.insert(win);
                    }
                }
            }
        }
    }
    
    return result;
}

std::unordered_set<Window> SpatialGrid::getMappableWindows(const Camera& camera) const {
    std::unordered_set<Window> result;
    
    // Get loadable chunks (visible + adjacent)
    auto loadable_chunks = getLoadableChunks(camera);
    
    // Collect windows from loadable chunks
    for (const auto& chunk : loadable_chunks) {
        auto it = chunks_.find(chunk);
        if (it != chunks_.end()) {
            result.insert(it->second.begin(), it->second.end());
        }
    }
    
    return result;
}

std::unordered_set<Window> SpatialGrid::getWindowsInChunk(const ChunkCoord& chunk) const {
    auto it = chunks_.find(chunk);
    if (it != chunks_.end()) {
        return it->second;
    }
    return {};
}

std::vector<ChunkCoord> SpatialGrid::getIntersectingChunks(int64_t x, int64_t y,
                                                           unsigned int width,
                                                           unsigned int height) const {
    std::vector<ChunkCoord> result;
    
    // Get chunk range
    ChunkCoord min_chunk = toChunkCoord(x, y);
    ChunkCoord max_chunk = toChunkCoord(x + width - 1, y + height - 1);
    
    // Iterate over all chunks in range
    for (int cx = min_chunk.x; cx <= max_chunk.x; ++cx) {
        for (int cy = min_chunk.y; cy <= max_chunk.y; ++cy) {
            result.push_back({ cx, cy });
        }
    }
    
    return result;
}

std::vector<ChunkCoord> SpatialGrid::getVisibleChunks(const Camera& camera) const {
    std::vector<ChunkCoord> result;
    
    // Get visible bounds
    VirtualRect visible = camera.getVisibleBounds();
    
    // Get chunk range
    ChunkCoord min_chunk = toChunkCoord(visible.x, visible.y);
    ChunkCoord max_chunk = toChunkCoord(visible.x + visible.width - 1, 
                                         visible.y + visible.height - 1);
    
    // Iterate over all visible chunks
    for (int cx = min_chunk.x; cx <= max_chunk.x; ++cx) {
        for (int cy = min_chunk.y; cy <= max_chunk.y; ++cy) {
            result.push_back({ cx, cy });
        }
    }
    
    return result;
}

std::vector<ChunkCoord> SpatialGrid::getLoadableChunks(const Camera& camera) const {
    std::unordered_set<ChunkCoord, ChunkCoord::Hash> result;
    
    // Get visible chunks
    auto visible = getVisibleChunks(camera);
    
    // Add visible chunks and their neighbors
    for (const auto& chunk : visible) {
        auto neighbors = chunk.getVisibleSet();
        result.insert(neighbors.begin(), neighbors.end());
    }
    
    return std::vector<ChunkCoord>(result.begin(), result.end());
}

const WindowEntry* SpatialGrid::getWindowEntry(Window window) const {
    auto it = windows_.find(window);
    if (it != windows_.end()) {
        return &it->second;
    }
    return nullptr;
}

Window SpatialGrid::findNearestWindow(int64_t virtual_x, int64_t virtual_y) const {
    if (windows_.empty()) return 0;  // None
    
    Window nearest = 0;
    int64_t min_distance = std::numeric_limits<int64_t>::max();
    
    for (const auto& [win, entry] : windows_) {
        // Calculate distance to window center
        int64_t cx = entry.virtual_x + entry.width / 2;
        int64_t cy = entry.virtual_y + entry.height / 2;
        int64_t dx = std::abs(cx - virtual_x);
        int64_t dy = std::abs(cy - virtual_y);
        int64_t distance = dx + dy;  // Manhattan distance
        
        if (distance < min_distance) {
            min_distance = distance;
            nearest = win;
        }
    }
    
    return nearest;
}

std::vector<Window> SpatialGrid::findWindowsInRadius(int64_t virtual_x, int64_t virtual_y,
                                                      int64_t radius) const {
    std::vector<Window> result;
    
    // Get chunks that might contain windows in radius
    ChunkCoord center_chunk = toChunkCoord(virtual_x, virtual_y);
    int chunk_radius = static_cast<int>(radius / CHUNK_SIZE) + 1;
    
    for (int dx = -chunk_radius; dx <= chunk_radius; ++dx) {
        for (int dy = -chunk_radius; dy <= chunk_radius; ++dy) {
            ChunkCoord chunk{ center_chunk.x + dx, center_chunk.y + dy };
            auto it = chunks_.find(chunk);
            if (it != chunks_.end()) {
                for (Window win : it->second) {
                    auto entry_it = windows_.find(win);
                    if (entry_it != windows_.end()) {
                        const auto& entry = entry_it->second;
                        int64_t cx = entry.virtual_x + entry.width / 2;
                        int64_t cy = entry.virtual_y + entry.height / 2;
                        int64_t dist_x = std::abs(cx - virtual_x);
                        int64_t dist_y = std::abs(cy - virtual_y);
                        
                        // Check Euclidean distance
                        if (dist_x * dist_x + dist_y * dist_y <= radius * radius) {
                            result.push_back(win);
                        }
                    }
                }
            }
        }
    }
    
    return result;
}

void SpatialGrid::addToChunk(Window window, const ChunkCoord& chunk) {
    chunks_[chunk].insert(window);
    window_chunks_[window].insert(chunk);
}

void SpatialGrid::removeFromChunk(Window window, const ChunkCoord& chunk) {
    auto it = chunks_.find(chunk);
    if (it != chunks_.end()) {
        it->second.erase(window);
        if (it->second.empty()) {
            chunks_.erase(it);
        }
    }
}

} // namespace pblank