#include "raster.hpp"

#include <algorithm>
#include <cstring>

namespace pl {

void Canvas::resize(int width, int height) {
    w = std::max(1, width);
    h = std::max(1, height);
    px.assign(static_cast<size_t>(w) * h * 4, 0);
}

void Canvas::clear(Col c) {
    const std::uint8_t b = static_cast<std::uint8_t>(c.b * c.a * 255 + .5f);
    const std::uint8_t g = static_cast<std::uint8_t>(c.g * c.a * 255 + .5f);
    const std::uint8_t r = static_cast<std::uint8_t>(c.r * c.a * 255 + .5f);
    const std::uint8_t a = static_cast<std::uint8_t>(c.a * 255 + .5f);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = b; px[i + 1] = g; px[i + 2] = r; px[i + 3] = a;
    }
}

void Canvas::save() { stack_.push_back(m_); }
void Canvas::restore() {
    if (!stack_.empty()) { m_ = stack_.back(); stack_.pop_back(); }
}
void Canvas::translate(double x, double y) { m_ = m_ * Mat{1, 0, 0, 1, x, y}; }
void Canvas::scale(double sx, double sy) { m_ = m_ * Mat{sx, 0, 0, sy, 0, 0}; }
void Canvas::rotate(double t) {
    const double c = std::cos(t), s = std::sin(t);
    m_ = m_ * Mat{c, s, -s, c, 0, 0};
}

void Canvas::begin() { paths_.clear(); }
void Canvas::push_dev(double x, double y) {
    double dx, dy;
    dev(x, y, dx, dy);
    paths_.back().push_back({dx, dy});
}
void Canvas::move(double x, double y) {
    paths_.emplace_back();
    push_dev(x, y);
    cx_ = x; cy_ = y;
}
void Canvas::line(double x, double y) {
    if (paths_.empty()) { move(x, y); return; }
    push_dev(x, y);
    cx_ = x; cy_ = y;
}
static int segs_for(double devlen) { return std::clamp(static_cast<int>(devlen / 3.5) + 2, 2, 72); }
void Canvas::quad(double qx, double qy, double x, double y) {
    if (paths_.empty()) move(cx_, cy_);
    const double len = (std::hypot(qx - cx_, qy - cy_) + std::hypot(x - qx, y - qy)) * m_.scale();
    const int n = segs_for(len);
    const double x0 = cx_, y0 = cy_;
    for (int i = 1; i <= n; ++i) {
        const double t = static_cast<double>(i) / n, u = 1 - t;
        push_dev(u * u * x0 + 2 * u * t * qx + t * t * x, u * u * y0 + 2 * u * t * qy + t * t * y);
    }
    cx_ = x; cy_ = y;
}
void Canvas::cubic(double ax, double ay, double bx, double by, double x, double y) {
    if (paths_.empty()) move(cx_, cy_);
    const double len = (std::hypot(ax - cx_, ay - cy_) + std::hypot(bx - ax, by - ay) + std::hypot(x - bx, y - by)) * m_.scale();
    const int n = segs_for(len);
    const double x0 = cx_, y0 = cy_;
    for (int i = 1; i <= n; ++i) {
        const double t = static_cast<double>(i) / n, u = 1 - t;
        const double k0 = u * u * u, k1 = 3 * u * u * t, k2 = 3 * u * t * t, k3 = t * t * t;
        push_dev(k0 * x0 + k1 * ax + k2 * bx + k3 * x, k0 * y0 + k1 * ay + k2 * by + k3 * y);
    }
    cx_ = x; cy_ = y;
}
void Canvas::close() {
    if (!paths_.empty() && !paths_.back().empty()) paths_.back().push_back(paths_.back().front());
}
void Canvas::ellipse(double cx, double cy, double rx, double ry, bool reverse) {
    const double r = std::max(std::fabs(rx), std::fabs(ry)) * m_.scale();
    const int n = std::clamp(static_cast<int>(r * 1.1) + 10, 12, 160);
    paths_.emplace_back();
    for (int i = 0; i <= n; ++i) {
        const double t = (reverse ? -1.0 : 1.0) * 2 * M_PI * i / n;
        push_dev(cx + rx * std::cos(t), cy + ry * std::sin(t));
    }
}
void Canvas::rect(double x, double y, double ww, double hh) {
    move(x, y); line(x + ww, y); line(x + ww, y + hh); line(x, y + hh); close();
}
void Canvas::rrect(double x, double y, double ww, double hh, double r) {
    r = std::min(r, std::min(ww, hh) * .5);
    const double k = r * 0.5523;
    move(x + r, y);
    line(x + ww - r, y);
    cubic(x + ww - r + k, y, x + ww, y + r - k, x + ww, y + r);
    line(x + ww, y + hh - r);
    cubic(x + ww, y + hh - r + k, x + ww - r + k, y + hh, x + ww - r, y + hh);
    line(x + r, y + hh);
    cubic(x + r - k, y + hh, x, y + hh - r + k, x, y + hh - r);
    line(x, y + r);
    cubic(x, y + r - k, x + r - k, y, x + r, y);
    close();
}

void Canvas::fill(Col c) {
    Paint p;
    p.color = c;
    rasterize(paths_, p);
}
void Canvas::fill(const Paint& user) {
    Paint p = user;
    if (p.kind != Paint::solid) {  // move gradient geometry to device space
        m_.apply(user.x0, user.y0, p.x0, p.y0);
        if (p.kind == Paint::linear) m_.apply(user.x1, user.y1, p.x1, p.y1);
        else p.r = user.r * m_.scale();
    }
    rasterize(paths_, p);
}

void Canvas::orient(std::vector<Pt>& p) {
    double a = 0;
    for (size_t i = 0; i + 1 < p.size(); ++i) a += p[i].x * p[i + 1].y - p[i + 1].x * p[i].y;
    if (a < 0) std::reverse(p.begin(), p.end());
}
void Canvas::add_cap(std::vector<std::vector<Pt>>& polys, Pt q, double hw, int segments) {
    std::vector<Pt> circ;
    for (int i = 0; i <= segments; ++i) {
        const double t = 2 * M_PI * i / segments;
        circ.push_back({q.x + hw * std::cos(t), q.y + hw * std::sin(t)});
    }
    orient(circ);
    polys.push_back(std::move(circ));
}

void Canvas::stroke(Col c, double width) {
    const double hw = std::max(0.35, width * m_.scale() * .5);
    std::vector<std::vector<Pt>> polys;
    const int cn = std::clamp(static_cast<int>(hw * 1.5) + 6, 8, 40);
    for (const std::vector<Pt>& path : paths_) {
        if (path.empty()) continue;
        add_cap(polys, path.front(), hw, cn);
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            const Pt a = path[i], b = path[i + 1];
            const double dx = b.x - a.x, dy = b.y - a.y, l = std::hypot(dx, dy);
            if (l < 1e-6) continue;
            const double nx = -dy / l * hw, ny = dx / l * hw;
            std::vector<Pt> q{{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}, {a.x + nx, a.y + ny}};
            orient(q);
            polys.push_back(std::move(q));
            add_cap(polys, b, hw, cn);
        }
    }
    Paint p;
    p.color = c;
    rasterize(polys, p);
}

namespace {
void accumulate(float* acc, int stride, int H, double x0, double y0, double x1, double y1) {
    if (y0 == y1) return;
    double dir = 1;
    if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); dir = -1; }
    if (y1 <= 0 || y0 >= H) return;
    const double dxdy = (x1 - x0) / (y1 - y0);
    double x = x0;
    int ys = 0;
    if (y0 < 0) x -= y0 * dxdy; else ys = static_cast<int>(y0);
    const int ye = std::min(H, static_cast<int>(std::ceil(y1)));
    for (int y = ys; y < ye; ++y) {
        const double dy = std::min(y + 1.0, y1) - std::max(static_cast<double>(y), y0);
        const double xnext = x + dxdy * dy;
        const double d = dy * dir;
        // clamped to the accumulator's columns: float drift at the clip edges must never index outside the row
        const double xa = std::clamp(std::min(x, xnext), 0.0, static_cast<double>(stride - 2)), xb = std::clamp(std::max(x, xnext), 0.0, static_cast<double>(stride - 2));
        const double x0f = std::floor(xa);
        const int x0i = static_cast<int>(x0f);
        const double x1c = std::ceil(xb);
        const int x1i = static_cast<int>(x1c);
        float* row = acc + static_cast<size_t>(y) * stride;
        if (x1i <= x0i + 1) {
            const double xmf = 0.5 * (x + xnext) - x0f;
            row[x0i] += static_cast<float>(d - d * xmf);
            row[x0i + 1] += static_cast<float>(d * xmf);
        } else {
            const double s = 1.0 / (xb - xa);
            const double x0fr = xa - x0f;
            const double a0 = 0.5 * s * (1 - x0fr) * (1 - x0fr);
            const double x1fr = xb - x1c + 1;
            const double am = 0.5 * s * x1fr * x1fr;
            row[x0i] += static_cast<float>(d * a0);
            if (x1i == x0i + 2) {
                row[x0i + 1] += static_cast<float>(d * (1 - a0 - am));
            } else {
                const double a1 = s * (1.5 - x0fr);
                row[x0i + 1] += static_cast<float>(d * (a1 - a0));
                for (int xi = x0i + 2; xi < x1i - 1; ++xi) row[xi] += static_cast<float>(d * s);
                const double a2 = a1 + (x1i - x0i - 3) * s;
                row[x1i - 1] += static_cast<float>(d * (1 - a2 - am));
            }
            row[x1i] += static_cast<float>(d * am);
        }
        x = xnext;
    }
}

struct Lut {
    float c[256][4];  // premultiplied rgba
};
void build_lut(const Paint& p, Lut& lut) {
    std::vector<Stop> s = p.stops;
    if (s.empty()) s = {{0, p.color}, {1, p.color}};
    for (int i = 0; i < 256; ++i) {
        const float t = i / 255.f;
        Col c = s.front().c;
        if (t >= s.back().t) c = s.back().c;
        else
            for (size_t k = 0; k + 1 < s.size(); ++k)
                if (t >= s[k].t && t <= s[k + 1].t) {
                    const float span = std::max(1e-6f, s[k + 1].t - s[k].t);
                    c = mix(s[k].c, s[k + 1].c, (t - s[k].t) / span);
                    break;
                }
        lut.c[i][0] = c.r * c.a; lut.c[i][1] = c.g * c.a; lut.c[i][2] = c.b * c.a; lut.c[i][3] = c.a;
    }
}
}  // namespace

void Canvas::rasterize(const std::vector<std::vector<Pt>>& polys, const Paint& p) {
    double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
    for (const std::vector<Pt>& poly : polys)
        for (const Pt& q : poly) {
            minx = std::min(minx, q.x); maxx = std::max(maxx, q.x);
            miny = std::min(miny, q.y); maxy = std::max(maxy, q.y);
        }
    if (minx > maxx) return;
    const int ix0 = std::max(0, static_cast<int>(std::floor(minx)));
    const int iy0 = std::max(0, static_cast<int>(std::floor(miny)));
    const int ix1 = std::min(w, static_cast<int>(std::ceil(maxx)) + 1);
    const int iy1 = std::min(h, static_cast<int>(std::ceil(maxy)) + 1);
    if (ix0 >= ix1 || iy0 >= iy1) return;
    const int bw = ix1 - ix0, bh = iy1 - iy0, stride = bw + 2;
    const size_t need = static_cast<size_t>(stride) * bh;
    if (acc_.size() < need) acc_.resize(need, 0.f);
    for (const std::vector<Pt>& poly : polys) {
        const size_t n = poly.size();
        if (n < 2) continue;
        for (size_t i = 0; i < n; ++i) {
            Pt a = poly[i], b = poly[(i + 1) % n];
            a.x -= ix0; a.y -= iy0; b.x -= ix0; b.y -= iy0;
            // split at the vertical clip lines, then clamp x into [0, bw]
            double xs[2] = {0.0, static_cast<double>(bw)};
            Pt pts[4] = {a, {}, {}, b};
            int cnt = 1;
            double ts[2];
            int nt = 0;
            for (double xc : xs)
                if ((a.x - xc) * (b.x - xc) < 0) ts[nt++] = (xc - a.x) / (b.x - a.x);
            if (nt == 2 && ts[0] > ts[1]) std::swap(ts[0], ts[1]);
            for (int k = 0; k < nt; ++k) pts[cnt++] = {a.x + (b.x - a.x) * ts[k], a.y + (b.y - a.y) * ts[k]};
            pts[cnt++] = b;
            for (int k = 0; k + 1 < cnt; ++k) {
                const double x0 = std::clamp(pts[k].x, 0.0, static_cast<double>(bw));
                const double x1 = std::clamp(pts[k + 1].x, 0.0, static_cast<double>(bw));
                accumulate(acc_.data(), stride, bh, x0, pts[k].y, x1, pts[k + 1].y);
            }
        }
    }
    Lut lut;
    const bool grad = p.kind != Paint::solid;
    float sc[4] = {p.color.r * p.color.a, p.color.g * p.color.a, p.color.b * p.color.a, p.color.a};
    double ldx = 0, ldy = 0, linv = 0;
    if (grad) {
        build_lut(p, lut);
        if (p.kind == Paint::linear) {
            ldx = p.x1 - p.x0; ldy = p.y1 - p.y0;
            linv = 1.0 / std::max(1e-9, ldx * ldx + ldy * ldy);
        }
    }
    const float ga = global_alpha;
    for (int y = 0; y < bh; ++y) {
        float* row = acc_.data() + static_cast<size_t>(y) * stride;
        std::uint8_t* out = px.data() + (static_cast<size_t>(iy0 + y) * w + ix0) * 4;
        float sum = 0;
        const double py = iy0 + y + 0.5;
        for (int x = 0; x < bw; ++x, out += 4) {
            sum += row[x];
            row[x] = 0;
            float cov = std::fabs(sum);
            if (cov < 0.002f) continue;
            if (cov > 1) cov = 1;
            cov *= ga;
            const float* s = sc;
            if (grad) {
                const double pxx = ix0 + x + 0.5;
                double t;
                if (p.kind == Paint::linear) t = ((pxx - p.x0) * ldx + (py - p.y0) * ldy) * linv;
                else t = std::hypot(pxx - p.x0, py - p.y0) / std::max(1e-9, p.r);
                int ti = static_cast<int>(t * 255 + .5);
                ti = ti < 0 ? 0 : (ti > 255 ? 255 : ti);
                s = lut.c[ti];
            }
            const float r = s[0] * cov, g = s[1] * cov, b = s[2] * cov, a = s[3] * cov;
            if (additive) {
                out[0] = static_cast<std::uint8_t>(std::min(255.f, out[0] + b * 255));
                out[1] = static_cast<std::uint8_t>(std::min(255.f, out[1] + g * 255));
                out[2] = static_cast<std::uint8_t>(std::min(255.f, out[2] + r * 255));
            } else {
                const float k = 1 - a;
                out[0] = static_cast<std::uint8_t>(b * 255 + out[0] * k + .5f);
                out[1] = static_cast<std::uint8_t>(g * 255 + out[1] * k + .5f);
                out[2] = static_cast<std::uint8_t>(r * 255 + out[2] * k + .5f);
                out[3] = static_cast<std::uint8_t>(std::min(255.f, a * 255 + out[3] * k + .5f));
            }
        }
        row[bw] = 0;
        row[bw + 1] = 0;
    }
}

void Canvas::draw_mask(const Mask& m, int x, int y, Col c, int scale) {
    const float pr = c.r * c.a * global_alpha, pg = c.g * c.a * global_alpha, pb = c.b * c.a * global_alpha, pa = c.a * global_alpha;
    for (int my = 0; my < m.h * scale; ++my) {
        const int dy = y + my;
        if (dy < 0 || dy >= h) continue;
        const std::uint8_t* src = m.a.data() + static_cast<size_t>(my / scale) * m.w;
        for (int mx = 0; mx < m.w * scale; ++mx) {
            const int dx = x + mx;
            if (dx < 0 || dx >= w) continue;
            const float cov = src[mx / scale] / 255.f;
            if (cov <= 0) continue;
            std::uint8_t* out = px.data() + (static_cast<size_t>(dy) * w + dx) * 4;
            const float k = 1 - pa * cov;
            out[0] = static_cast<std::uint8_t>(pb * cov * 255 + out[0] * k + .5f);
            out[1] = static_cast<std::uint8_t>(pg * cov * 255 + out[1] * k + .5f);
            out[2] = static_cast<std::uint8_t>(pr * cov * 255 + out[2] * k + .5f);
            out[3] = static_cast<std::uint8_t>(std::min(255.f, pa * cov * 255 + out[3] * k + .5f));
        }
    }
}

void Canvas::draw_canvas(const Canvas& src, int x, int y, float opacity) {
    for (int sy = 0; sy < src.h; ++sy) {
        const int dy = y + sy;
        if (dy < 0 || dy >= h) continue;
        for (int sx = 0; sx < src.w; ++sx) {
            const int dx = x + sx;
            if (dx < 0 || dx >= w) continue;
            const std::uint8_t* s = src.px.data() + (static_cast<size_t>(sy) * src.w + sx) * 4;
            if (s[3] == 0) continue;
            std::uint8_t* o = px.data() + (static_cast<size_t>(dy) * w + dx) * 4;
            const float k = 1 - s[3] / 255.f * opacity;
            for (int c = 0; c < 4; ++c) o[c] = static_cast<std::uint8_t>(std::min(255.f, s[c] * opacity + o[c] * k + .5f));
        }
    }
}

}  // namespace pl
