#pragma once

#include "FlightCore.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace flight_core {

struct TerrainHit {
    Vec3 point_enu_m;
    Vec3 normal_enu; // Unit triangle normal, positive Up; not a smoothed shading normal.
};

// Read-only after loading. All coordinates use one render ENU frame in metres.
// Each rendered triangle must use three corners of one row-major grid cell.
// Missing triangles remain holes. Query selects the highest actual surface at XY.
class TerrainSurface {
    struct Tile {
        std::size_t width = 0, nx = 0, ny = 0;
        Vec3 low, high;
        std::vector<Vec3> vertices;
        std::vector<std::uint8_t> triangles; // Bits name the omitted corner of each actual triangle.
        std::vector<std::uint32_t> offsets, cells;
    };
    std::vector<Tile> tiles_;

    static Vec3 cross(Vec3 a, Vec3 b) {
        return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
    }
    static std::size_t bin_index(double value, double low, double high, std::size_t count) {
        return std::min(static_cast<std::size_t>((value - low) / (high - low) * count), count - 1);
    }
    static std::array<std::size_t, 4> cell_corners(const Tile& tile, std::size_t cell) {
        const auto a = (cell / tile.nx) * tile.width + cell % tile.nx;
        return {a, a + 1, a + tile.width, a + tile.width + 1};
    }
    static std::array<std::size_t, 4> bin_range(const Tile& tile, std::size_t cell) {
        const auto corners = cell_corners(tile, cell);
        auto low = tile.vertices[corners[0]], high = low;
        for (const auto i : corners) {
            const auto v = tile.vertices[i];
            low.x = std::min(low.x, v.x); high.x = std::max(high.x, v.x);
            low.y = std::min(low.y, v.y); high.y = std::max(high.y, v.y);
        }
        return {bin_index(low.x, tile.low.x, tile.high.x, tile.nx), bin_index(high.x, tile.low.x, tile.high.x, tile.nx),
                bin_index(low.y, tile.low.y, tile.high.y, tile.ny), bin_index(high.y, tile.low.y, tile.high.y, tile.ny)};
    }

public:
    bool add_tile(std::size_t width, std::size_t height, std::vector<Vec3> vertices_enu_m,
                  const std::vector<std::uint32_t>& triangle_indices) {
        if (width < 2 || height < 2 || width > 513 || height > 513 || tiles_.size() >= 64
            || vertices_enu_m.size() != width * height || triangle_indices.empty()
            || triangle_indices.size() % 3 != 0) return false;
        Tile tile;
        tile.width = width;
        tile.nx = width - 1;
        tile.ny = height - 1;
        tile.vertices = std::move(vertices_enu_m);
        tile.low = tile.high = tile.vertices.front();
        for (const auto v : tile.vertices) {
            if (!finite(v)) return false;
            tile.low.x = std::min(tile.low.x, v.x); tile.high.x = std::max(tile.high.x, v.x);
            tile.low.y = std::min(tile.low.y, v.y); tile.high.y = std::max(tile.high.y, v.y);
        }
        if (!(tile.high.x > tile.low.x) || !(tile.high.y > tile.low.y)
            || !std::isfinite(tile.high.x - tile.low.x) || !std::isfinite(tile.high.y - tile.low.y)) return false;
        tile.triangles.resize(tile.nx * tile.ny);
        for (std::size_t i = 0; i < triangle_indices.size(); i += 3) {
            const std::array<std::uint32_t, 3> indices{triangle_indices[i], triangle_indices[i+1], triangle_indices[i+2]};
            std::size_t row = height, col = width, max_row = 0, max_col = 0;
            for (const auto index : indices) {
                if (index >= tile.vertices.size()) return false;
                row = std::min(row, index / width); col = std::min(col, index % width);
                max_row = std::max(max_row, index / width); max_col = std::max(max_col, index % width);
            }
            if (max_row != row + 1 || max_col != col + 1) return false;
            unsigned corners = 0;
            for (const auto index : indices) corners |= 1u << ((index / width - row) * 2 + index % width - col);
            const unsigned missing = 15u ^ corners;
            if (missing == 0 || (missing & (missing - 1)) != 0) return false;
            const auto a = tile.vertices[indices[0]], b = tile.vertices[indices[1]], c = tile.vertices[indices[2]];
            const auto n = cross(b - a, c - a);
            const double xy_scale = std::max((b.x-a.x)*(b.x-a.x) + (b.y-a.y)*(b.y-a.y),
                                             (c.x-a.x)*(c.x-a.x) + (c.y-a.y)*(c.y-a.y));
            if (!finite(n) || !std::isfinite(std::hypot(n.x, std::hypot(n.y, n.z)))
                || !std::isfinite(xy_scale) || std::abs(n.z) <= xy_scale * 1e-12) return false;
            tile.triangles[row * tile.nx + col] |= static_cast<std::uint8_t>(missing);
        }

        // A compressed bin -> cell index avoids scanning the country mesh per query.
        // Bin assignment uses actual XY extents, so geodetic skew/curvature need no inverse approximation.
        tile.offsets.resize(tile.nx * tile.ny + 1);
        std::size_t references = 0;
        for (std::size_t cell = 0; cell < tile.triangles.size(); ++cell) {
            if (!tile.triangles[cell]) continue;
            const auto range = bin_range(tile, cell);
            references += (range[1] - range[0] + 1) * (range[3] - range[2] + 1);
            if (references > tile.triangles.size() * 64) return false; // Reject pathological, non-local grids.
            for (auto y = range[2]; y <= range[3]; ++y)
                for (auto x = range[0]; x <= range[1]; ++x) ++tile.offsets[y * tile.nx + x + 1];
        }
        for (std::size_t i = 1; i < tile.offsets.size(); ++i) tile.offsets[i] += tile.offsets[i-1];
        tile.cells.resize(references);
        auto cursor = tile.offsets;
        for (std::size_t cell = 0; cell < tile.triangles.size(); ++cell) {
            if (!tile.triangles[cell]) continue;
            const auto range = bin_range(tile, cell);
            for (auto y = range[2]; y <= range[3]; ++y)
                for (auto x = range[0]; x <= range[1]; ++x)
                    tile.cells[cursor[y * tile.nx + x]++] = static_cast<std::uint32_t>(cell);
        }
        tiles_.push_back(std::move(tile)); // Invalid input leaves existing terrain unchanged.
        return true;
    }

    std::optional<TerrainHit> query(double x, double y) const {
        if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
        std::optional<TerrainHit> result;
        for (const auto& tile : tiles_) {
            if (x < tile.low.x || x > tile.high.x || y < tile.low.y || y > tile.high.y) continue;
            const auto bin = bin_index(y, tile.low.y, tile.high.y, tile.ny) * tile.nx
                + bin_index(x, tile.low.x, tile.high.x, tile.nx);
            for (auto i = tile.offsets[bin]; i < tile.offsets[bin+1]; ++i) {
                const auto cell = tile.cells[i];
                const auto corners = cell_corners(tile, cell);
                for (unsigned missing = 0; missing < 4; ++missing) {
                    if (!(tile.triangles[cell] & (1u << missing))) continue;
                    std::array<Vec3, 3> v;
                    unsigned next = 0;
                    for (unsigned corner = 0; corner < 4; ++corner)
                        if (corner != missing) v[next++] = tile.vertices[corners[corner]];
                    const auto ab = v[1] - v[0], ac = v[2] - v[0];
                    auto normal = cross(ab, ac);
                    const double dx = x - v[0].x, dy = y - v[0].y;
                    const double b = (dx * ac.y - dy * ac.x) / normal.z;
                    const double c = (ab.x * dy - ab.y * dx) / normal.z;
                    constexpr double edge_roundoff = 1e-10; // Dimensionless barycentric tolerance only.
                    if (!std::isfinite(b) || !std::isfinite(c)
                        || b < -edge_roundoff || c < -edge_roundoff || b + c > 1 + edge_roundoff) continue;
                    const double z = v[0].z + b * ab.z + c * ac.z;
                    if (!std::isfinite(z) || (result && z <= result->point_enu_m.z)) continue;
                    if (normal.z < 0) normal = normal * -1;
                    const double length = std::hypot(normal.x, std::hypot(normal.y, normal.z));
                    result = TerrainHit{{x, y, z}, normal * (1 / length)};
                }
            }
        }
        return result;
    }

    void clear() { tiles_.clear(); }
};

} // namespace flight_core
