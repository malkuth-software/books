#pragma once
// A small anti-aliased vector rasterizer: paths (lines, quadratic and cubic
// curves, ellipses), affine transforms, solid/linear/radial paints, strokes,
// and BGRA premultiplied output that a GUI.Forms LiveSurface can publish as-is.
// Coverage uses signed-area accumulation, so same-winding overlaps union and
// reversed sub-paths cut holes.
#include <cmath>
#include <cstdint>
#include <vector>

namespace pl {

struct Col {
    float r = 0, g = 0, b = 0, a = 1;  // straight alpha, 0..1
};
inline Col rgb(int r, int g, int b, float a = 1.f) { return {r / 255.f, g / 255.f, b / 255.f, a}; }
inline Col hex(unsigned v, float a = 1.f) { return rgb((v >> 16) & 255, (v >> 8) & 255, v & 255, a); }
inline Col mix(Col x, Col y, float t) {
    return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
}
inline Col alpha(Col c, float a) { return {c.r, c.g, c.b, c.a * a}; }
inline Col shade(Col c, float k) {  // k>1 lighter toward white, k<1 darker
    if (k >= 1) return mix(c, {1, 1, 1, c.a}, std::fmin(1.f, k - 1));
    return {c.r * k, c.g * k, c.b * k, c.a};
}

struct Mat {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;  // x' = a x + c y + e, y' = b x + d y + f
    void apply(double x, double y, double& ox, double& oy) const {
        ox = a * x + c * y + e;
        oy = b * x + d * y + f;
    }
    Mat operator*(const Mat& m) const {  // this ∘ m (apply m first)
        return {a * m.a + c * m.b, b * m.a + d * m.b, a * m.c + c * m.d, b * m.c + d * m.d,
                a * m.e + c * m.f + e, b * m.e + d * m.f + f};
    }
    double scale() const { return std::sqrt(std::fabs(a * d - b * c)); }
};

struct Stop {
    float t;
    Col c;
};
struct Paint {
    enum Kind { solid, linear, radial } kind = solid;
    Col color{};
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0, r = 1;  // in user space at fill time
    std::vector<Stop> stops;
    static Paint lin(double x0, double y0, double x1, double y1, std::vector<Stop> s) {
        Paint p; p.kind = linear; p.x0 = x0; p.y0 = y0; p.x1 = x1; p.y1 = y1; p.stops = std::move(s); return p;
    }
    static Paint rad(double cx, double cy, double r, std::vector<Stop> s) {
        Paint p; p.kind = radial; p.x0 = cx; p.y0 = cy; p.r = r; p.stops = std::move(s); return p;
    }
};

struct Mask {  // 8-bit alpha image (text glyph runs etc.)
    int w = 0, h = 0;
    std::vector<std::uint8_t> a;
};

class Canvas {
public:
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;  // BGRA premultiplied, row-major, stride w*4
    float global_alpha = 1.f;
    bool additive = false;

    void resize(int width, int height);
    void clear(Col c);

    // transform stack
    void save();
    void restore();
    void translate(double x, double y);
    void scale(double sx, double sy);
    void rotate(double radians);
    void set_transform(const Mat& m) { m_ = m; }
    const Mat& transform() const { return m_; }

    // path construction (user space)
    void begin();
    void move(double x, double y);
    void line(double x, double y);
    void quad(double cx, double cy, double x, double y);
    void cubic(double c1x, double c1y, double c2x, double c2y, double x, double y);
    void close();
    void ellipse(double cx, double cy, double rx, double ry, bool reverse = false);
    void circle(double cx, double cy, double r) { ellipse(cx, cy, r, r); }
    void rect(double x, double y, double ww, double hh);
    void rrect(double x, double y, double ww, double hh, double r);

    void fill(Col c);
    void fill(const Paint& p);
    void stroke(Col c, double width);  // round joins and caps

    // convenience one-shots
    void fill_ellipse(double cx, double cy, double rx, double ry, Col c) { begin(); ellipse(cx, cy, rx, ry); fill(c); }
    void fill_circle(double cx, double cy, double r, Col c) { fill_ellipse(cx, cy, r, r, c); }
    void fill_rect(double x, double y, double ww, double hh, Col c) { begin(); rect(x, y, ww, hh); fill(c); }
    void stroke_line(double x0, double y0, double x1, double y1, Col c, double width) {
        begin(); move(x0, y0); line(x1, y1); stroke(c, width);
    }

    // device-space mask draw (text). scale>1 gives nearest-neighbour pixel art.
    void draw_mask(const Mask& m, int x, int y, Col c, int scale = 1);
    // device-space copy of another canvas with opacity
    void draw_canvas(const Canvas& src, int x, int y, float opacity = 1.f);

private:
    struct Pt { double x, y; };
    // Makes a polygon wind counter-clockwise, so stroke pieces add up instead of cancelling.
    static void orient(std::vector<Pt>& p);
    // A round cap or joint: a circle of radius hw at q, with `segments` sides.
    static void add_cap(std::vector<std::vector<Pt>>& polys, Pt q, double hw, int segments);
    std::vector<std::vector<Pt>> paths_;  // device space
    Mat m_{};
    std::vector<Mat> stack_;
    std::vector<float> acc_;
    double cx_ = 0, cy_ = 0;  // current point (user)
    void dev(double x, double y, double& ox, double& oy) const { m_.apply(x, y, ox, oy); }
    void push_dev(double x, double y);
    void rasterize(const std::vector<std::vector<Pt>>& polys, const Paint& p);
};

}  // namespace pl
