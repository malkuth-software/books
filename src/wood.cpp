#include "wood.hpp"
#include "gui_forms/window.hpp"
#include <algorithm>
#include <cmath>
namespace games {
namespace {
std::uint32_t mix32(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}
// Value noise on a lattice that wraps every `px` by `py` cells, so it tiles.
struct Noise {
    std::uint32_t seed;
    double at(int ix, int iy, int px, int py) const {
        ix = ((ix % px) + px) % px;
        iy = ((iy % py) + py) % py;
        return mix32(seed ^ mix32(static_cast<std::uint32_t>(ix) * 0x9E3779B1U + static_cast<std::uint32_t>(iy))) / 4294967295.0;
    }
    // 0..1 at (x, y) in cells.
    double value(double x, double y, int px, int py) const {
        const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
        double fx = x - ix, fy = y - iy;
        fx = fx * fx * (3 - 2 * fx);
        fy = fy * fy * (3 - 2 * fy);
        const double a = at(ix, iy, px, py), b = at(ix + 1, iy, px, py);
        const double c = at(ix, iy + 1, px, py), d = at(ix + 1, iy + 1, px, py);
        return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
    }
    // Octaves over the unit square (s, t in 0..1), `cx` by `cy` cells at the first.
    double fbm(double s, double t, int cx, int cy, int octaves) const {
        double sum = 0, amp = .5, norm = 0;
        for (int o = 0; o < octaves; ++o) {
            sum += value(s * cx, t * cy, cx, cy) * amp;
            norm += amp;
            amp *= .5;
            cx *= 2;
            cy *= 2;
        }
        return sum / norm;
    }
};
double smooth(double a, double b, double x) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}
} // namespace

std::vector<std::byte> make_wood(int w, int h, bool along_x, const WoodLook& look) {
    std::vector<std::byte> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    const Noise bend{look.seed}, fibre{look.seed * 3 + 1}, pore{look.seed * 7 + 5}, tone{look.seed * 11 + 2};
    const int rings = std::max(1, static_cast<int>(std::lround(look.rings)));
    // Knots: where, how long along the grain and across it (in the unit square).
    struct Knot {
        double s, t, len, wid;
    };
    std::vector<Knot> knots;
    for (int k = 0; k < look.knots; ++k) {
        const std::uint32_t r = mix32(look.seed * 131U + static_cast<std::uint32_t>(k) * 977U);
        knots.push_back({(r & 0xffffU) / 65535.0, ((r >> 16) & 0xffffU) / 65535.0, .045, .5 / rings});
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // s runs along the grain, t across it; both 0..1 over the picture.
            const double s = along_x ? (x + .5) / w : (y + .5) / h;
            const double t = along_x ? (y + .5) / h : (x + .5) / w;
            double r = t * rings + look.bend * (bend.fbm(s, t, 2, 3, 3) - .5) * 2;
            double knot = 0;
            for (const Knot& k : knots) {
                double ds = s - k.s, dt = t - k.t;
                ds -= std::round(ds);
                dt -= std::round(dt);
                const double d2 = (ds / k.len) * (ds / k.len) + (dt / k.wid) * (dt / k.wid);
                const double near = std::exp(-d2);
                // The grain swells round the knot; the knot itself is dark and ringed.
                r += near * 1.2 * (dt >= 0 ? 1 : -1);
                knot = std::max(knot, std::exp(-d2 * 6));
            }
            const double f = r - std::floor(r);
            const double late = smooth(.55, .78, f) * (1 - smooth(.9, 1.0, f));
            const double streak = fibre.fbm(s, t, 24, rings * 5, 2);
            const double pores = smooth(.72, .9, pore.value(s * 220, t * rings * 18, 220, rings * 18));
            const double broad = tone.fbm(s, t, 3, 2, 2);
            double k = late * .78 + (streak - .5) * .45 + pores * .22 + knot * .9;
            k = std::clamp(k, 0.0, 1.0);
            const double lift = .9 + broad * .2;
            const double cr = std::clamp(lerp(look.light.red, look.dark.red, k) * lift, 0.0, 255.0);
            const double cg = std::clamp(lerp(look.light.green, look.dark.green, k) * lift, 0.0, 255.0);
            const double cb = std::clamp(lerp(look.light.blue, look.dark.blue, k) * lift, 0.0, 255.0);
            std::byte* p = out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4;
            p[0] = static_cast<std::byte>(std::lround(cb));
            p[1] = static_cast<std::byte>(std::lround(cg));
            p[2] = static_cast<std::byte>(std::lround(cr));
            p[3] = std::byte{255};
        }
    return out;
}

void ShelfWood::make(gf::Window& window) {
    release(window);
    const double scale = std::max(1.0, window.scale());
    const int ww = static_cast<int>(std::lround(wall_w * scale)), wh = static_cast<int>(std::lround(wall_h * scale));
    const int pw = static_cast<int>(std::lround(plank_w * scale)), ph = static_cast<int>(std::lround(plank_h * scale));
    // Walnut boards on the wall; a warmer oak for the shelves.
    const WoodLook walnut{gf::Color::rgba(74, 47, 28), gf::Color::rgba(36, 21, 11), 7, 1.4, 1, 23};
    const WoodLook oak{gf::Color::rgba(168, 116, 70), gf::Color::rgba(104, 64, 34), 5, 1.1, 1, 41};
    const std::vector<std::byte> a = make_wood(ww, wh, false, walnut);
    wall = window.load_bgra32_premultiplied(static_cast<std::uint32_t>(ww), static_cast<std::uint32_t>(wh), static_cast<std::uint64_t>(ww) * 4, a).image;
    wall_pixels = {static_cast<double>(ww), static_cast<double>(wh)};
    const std::vector<std::byte> b = make_wood(pw, ph, true, oak);
    plank = window.load_bgra32_premultiplied(static_cast<std::uint32_t>(pw), static_cast<std::uint32_t>(ph), static_cast<std::uint64_t>(pw) * 4, b).image;
    plank_pixels = {static_cast<double>(pw), static_cast<double>(ph)};
}

void ShelfWood::release(gf::Window& window) {
    if (wall.value)
        static_cast<void>(window.remove_image(wall));
    if (plank.value)
        static_cast<void>(window.remove_image(plank));
    wall = {};
    plank = {};
}
} // namespace games
