#include "TerrainSurface.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace flight_core;

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void near(double actual, double expected, const char* message) {
    require(std::abs(actual - expected) < 1e-9, message);
}
std::vector<std::uint32_t> indices(std::uint32_t width, std::uint32_t height, int hole = -1) {
    std::vector<std::uint32_t> result;
    for (std::uint32_t row = 0; row + 1 < height; ++row)
        for (std::uint32_t col = 0; col + 1 < width; ++col) {
            if (static_cast<int>(row * (width - 1) + col) == hole) continue;
            const auto a = row * width + col, b = a + 1, c = a + width, d = c + 1;
            result.insert(result.end(), {a, c, b, b, c, d}); // Actual renderer diagonal.
        }
    return result;
}
}

int main() {
    TerrainSurface terrain;
    require(!terrain.query(0, 0), "empty surface returns no ground");
    require(terrain.add_tile(2, 2, {{0,1,7},{1,1,7},{0,0,7},{1,0,7}}, indices(2,2)), "flat tile loads");
    for (const Vec3 p : {Vec3{.1,.2,0}, Vec3{.9,.8,0}, Vec3{.5,.5,0}, Vec3{0,0,0}, Vec3{1,1,0}}) {
        const auto hit = terrain.query(p.x, p.y);
        require(hit.has_value(), "interior, diagonal and outer corners are covered");
        near(hit->point_enu_m.z, 7, "flat height");
        near(hit->normal_enu.x, 0, "flat normal x");
        near(hit->normal_enu.y, 0, "flat normal y");
        near(hit->normal_enu.z, 1, "normal is up despite source winding");
    }
    require(!terrain.query(1.001,.5) && !terrain.query(-.001,.5), "outside extents returns no ground");
    require(!terrain.query(std::numeric_limits<double>::quiet_NaN(),0), "nonfinite query rejected");
    require(!terrain.add_tile(2,2,{{0,1,0},{1,1,0},{0,0,0},{1,0,0}},{0,1,99}), "bad index rejected");
    near(terrain.query(.5,.5)->point_enu_m.z,7,"invalid addition preserves previous tiles");
    terrain.clear();

    // Skewed, nonuniform XY grid sampled from the independent plane z=2x-3y+5.
    std::vector<Vec3> skew;
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 5; ++col) {
            const double x = 10 + col + .4 * row + .03 * col * row;
            const double y = 20 - row + .2 * col;
            skew.push_back({x, y, 2*x - 3*y + 5});
        }
    require(terrain.add_tile(5,5,skew,indices(5,5)), "skewed terrain loads");
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t col = 0; col < 4; ++col) {
            const auto a = skew[row*5+col], b = skew[row*5+col+1], c = skew[(row+1)*5+col];
            const auto p = a*.2 + b*.3 + c*.5;
            const auto hit = terrain.query(p.x,p.y);
            require(hit.has_value(), "bins find skewed cells");
            near(hit->point_enu_m.z,p.z,"sloped height follows plane");
            near(hit->normal_enu.x,-2/std::sqrt(14.),"slope normal x");
            near(hit->normal_enu.y,3/std::sqrt(14.),"slope normal y");
            near(hit->normal_enu.z,1/std::sqrt(14.),"slope unit normal z");
        }
    terrain.clear();

    // A saddle distinguishes actual triangular interpolation from bilinear height.
    require(terrain.add_tile(2,2,{{0,1,0},{1,1,0},{0,0,0},{1,0,10}},indices(2,2)), "nonplanar cell loads");
    near(terrain.query(.25,.75)->point_enu_m.z,0,"first triangle remains flat");
    near(terrain.query(.75,.25)->point_enu_m.z,5,"second triangle uses its plane");
    near(terrain.query(.5,.5)->point_enu_m.z,0,"shared diagonal has matching height");
    terrain.clear();

    std::vector<Vec3> base;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) base.push_back({double(col),3.-row,0});
    require(terrain.add_tile(4,4,base,indices(4,4,4)), "base with center hole loads");
    require(!terrain.query(1.5,1.5),"absent rendered cell stays a hole");
    require(terrain.query(.5,.5).has_value(),"neighbors around hole remain queryable");
    require(terrain.add_tile(2,2,{{1,2,30},{2,2,30},{1,1,30},{2,1,30}},indices(2,2)),"detail tile fills hole");
    near(terrain.query(1.5,1.5)->point_enu_m.z,30,"detail surface is queried inside base hole");
    require(terrain.add_tile(2,2,{{1,2,10},{2,2,10},{1,1,10},{2,1,10}},indices(2,2)),"lower overlapping tile loads");
    near(terrain.query(1.5,1.5)->point_enu_m.z,30,"highest surface wins independent of registration order");
    terrain.clear();
    require(terrain.add_tile(2,2,{{0,1,0},{1,1,0},{0,0,0},{1,0,0}},{0,2,1}),"single rendered triangle loads");
    require(terrain.query(.1,.9).has_value() && !terrain.query(.9,.1),"individual missing triangle stays absent");
    require(!terrain.add_tile(2,2,{{0,0,0},{1,0,0},{2,0,0},{3,0,0}},indices(2,2)),"XY-degenerate grid rejected");
    std::cout << "terrain_surface_checks passed\n";
}
