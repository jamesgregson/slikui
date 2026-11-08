// slikui/widgets/data.hpp — Plot (+ histogram), TreeView, Table,
// PropertyGrid, Vec2Pad: the data-display family for graphics-demo panels.
//
// All widgets follow the house pattern: fluent setters, theme tokens, cached
// text layouts, mouse-capture drags. Series/rows are copied by value — apps
// push new data with the setters each frame or on change.
#pragma once

#include "slikui/context.hpp"
#include "slikui/style/anim.hpp"
#include "slikui/widgets/containers.hpp"
#include "slikui/widgets/env.hpp"
#include "slikui/widgets/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace slikui {

// ---------------------------------------------------------------------------
// Plot — line series with autoscale, grid + tick labels, optional area fill,
// markers, and a bars mode (= histogram for frame-time-style data).
// ---------------------------------------------------------------------------
class Plot : public Widget {
public:
    explicit Plot(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    // xy data in data space; x must be increasing for fill/bars to read well.
    Plot& add_series(std::vector<Vec2> pts, Color color) {
        series_.push_back({std::move(pts), color});
        data_version_++;
        return *this;
    }
    // Single y-series with x = 0..n-1 (replaces existing series).
    Plot& set_series(std::vector<float> ys, Color color) {
        series_.clear();
        std::vector<Vec2> pts;
        pts.reserve(ys.size());
        for (size_t i = 0; i < ys.size(); ++i) pts.push_back({float(i), ys[i]});
        series_.push_back({std::move(pts), color});
        data_version_++;
        return *this;
    }
    Plot& clear_series() {
        series_.clear();
        data_version_++;
        return *this;
    }

    // Fixed ranges; unset axes autoscale over the data (with a small margin).
    Plot& set_x_range(float min, float max) { xr_ = {min, max}; data_version_++; return *this; }
    Plot& set_y_range(float min, float max) { yr_ = {min, max}; data_version_++; return *this; }
    // Bars mode draws series[0] as columns from the y baseline (histogram).
    Plot& set_bars(bool on) { bars_ = on; return *this; }
    // Fill the area under series[0] down to the baseline.
    Plot& set_fill(bool on) { fill_ = on; return *this; }
    Plot& set_markers(bool on) { markers_ = on; return *this; }

    Size measure(const Constraints& c) override { return c.clamp({150.f, 90.f}); }
    bool expands_main() const override { return true; }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        refresh_scale();
        refresh_tick_labels(env);

        env.list.rect(bounds, t.surface, t.radius);
        env.list.hollow_rect(bounds, t.border, t.border_width, t.radius);

        // Plot area: leave room for y labels on the left, x labels below.
        Rect plot{bounds.x + 30, bounds.y + 6,
                  std::fmax(bounds.w - 38, 10.f), std::fmax(bounds.h - 26, 10.f)};

        env.list.push_clip(bounds);
        // Grid + y tick labels.
        for (int i = 0; i <= 3; ++i) {
            float fy = float(i) / 3.f;
            float gy = plot.y + fy * plot.h;
            env.list.line({plot.x, gy}, {plot.right(), gy}, t.surface_alt, 1);
            if (i < 3 && size_t(i) < ylabels_.size()) {
                const auto& lab = ylabels_[size_t(i)];
                float inv = 1.f / ui_scale();
                float lx = std::fmax(bounds.x + 2, plot.x - 4 - lab.size.w * inv);
                paint_text(env, lab, {lx, gy - lab.size.h * inv * .5f}, t.text_dim, inv);
            }
        }

        if (!series_.empty()) {
            const auto& s = series_[0];
            float base_y = value_to_y(std::fmax(0.f, ylo_), plot);
            if (fill_ && s.pts.size() >= 2) {
                float dx = plot.w / std::fmax(float(s.pts.size() - 1), 1.f);
                for (size_t i = 0; i < s.pts.size(); ++i) {
                    float x = point_to_x(s.pts[i], plot);
                    float y = value_to_y(s.pts[i].y, plot);
                    env.list.line({x, y}, {x, base_y}, s.color.faded(.35f), dx + 1.f);
                }
            }
            if (bars_) {
                float n = float(s.pts.size());
                float bw = plot.w / std::fmax(n, 1.f);
                for (size_t i = 0; i < s.pts.size(); ++i) {
                    float x = plot.x + (float(i) + 0.5f) * bw;
                    float y = value_to_y(s.pts[i].y, plot);
                    env.list.rect({x - bw * .8f * .5f, y, bw * .8f,
                                   std::fmax(base_y - y, 0.f)}, s.color, 1);
                }
            } else {
                // Map to screen space and draw as one polyline.
                scratch_.clear();
                for (const auto& p : s.pts) scratch_.push_back({point_to_x(p, plot),
                                                               value_to_y(p.y, plot)});
                if (scratch_.size() >= 2)
                    env.list.polyline(scratch_.data(), scratch_.size(), s.color, 1.6f);
                if (markers_)
                    for (const auto& p : scratch_) env.list.circle(p, 2.5f, s.color);
            }
            // Further series are always lines.
            for (size_t k = 1; k < series_.size(); ++k) {
                const auto& s2 = series_[k];
                scratch_.clear();
                for (const auto& p : s2.pts) scratch_.push_back({point_to_x(p, plot),
                                                                 value_to_y(p.y, plot)});
                if (scratch_.size() >= 2)
                    env.list.polyline(scratch_.data(), scratch_.size(), s2.color, 1.6f);
            }
        }
        env.list.pop_clip();
    }

    const char* widget_type() const override { return "Plot"; }

private:
    struct SeriesData {
        std::vector<Vec2> pts;
        Color color;
    };

    void refresh_scale() const {
        if (scale_valid_ && scale_version_ == data_version_) return;
        float xlo = xr_.x >= 0 || xr_.y > xr_.x ? xr_.x : 0.f; // unset = {0,0}
        bool have_x = xr_.y > xr_.x, have_y = yr_.y > yr_.x;
        float xmin = have_x ? xr_.x : 0.f, xmax = have_x ? xr_.y : 1.f;
        float ymin = have_y ? yr_.x : 0.f, ymax = have_y ? yr_.y : 1.f;
        bool any = false;
        for (const auto& s : series_)
            for (const auto& p : s.pts) {
                if (!any) {
                    if (!have_x) { xmin = xmax = p.x; }
                    if (!have_y) { ymin = ymax = p.y; }
                    any = true;
                }
                if (!have_x) { xmin = std::fmin(xmin, p.x); xmax = std::fmax(xmax, p.x); }
                if (!have_y) { ymin = std::fmin(ymin, p.y); ymax = std::fmax(ymax, p.y); }
            }
        if (!have_y && any) {
            float pad = std::fmax((ymax - ymin) * .08f, 1e-3f);
            ymin = std::fmin(ymin - pad, 0.f); // include the baseline when free
            ymax += pad;
        }
        if (!have_x && any) xmax += (xmax - xmin) > 0 ? (xmax - xmin) * .01f : 1.f;
        if (xmax <= xmin) xmax = xmin + 1.f;
        if (ymax <= ymin) ymax = ymin + 1.f;
        xlo_ = xmin;
        ylo_ = ymin;
        xscale_ = (xmax - xmin);
        yscale_ = (ymax - ymin);
        scale_valid_ = true;
        scale_version_ = data_version_;
        labels_valid_ = false;
    }

    void refresh_tick_labels(const PaintEnv& env) const {
        if (labels_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        ylabels_.clear();
        for (int i = 0; i < 3; ++i) {
            float v = ylo_ + yscale_ * (1.f - float(i) / 3.f);
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.4g", double(v));
            ylabels_.push_back(ctx->font().layout(buf, t.s(t.small_font_px)));
        }
        labels_valid_ = true;
    }

    float point_to_x(Vec2 p, const Rect& plot) const {
        float f = (p.x - xlo_) / xscale_;
        return plot.x + clamp01(f) * plot.w;
    }
    float value_to_y(float v, const Rect& plot) const {
        float f = (v - ylo_) / yscale_;
        return plot.y + (1.f - clamp01(f)) * plot.h;
    }

    std::vector<SeriesData> series_;
    Vec2 xr_{0, 0}, yr_{0, 0}; // y.x >= y.y means "unset"
    bool bars_ = false, fill_ = false, markers_ = false;
    mutable float xlo_ = 0, ylo_ = 0, xscale_ = 1, yscale_ = 1;
    mutable std::uint64_t data_version_ = 0, scale_version_ = 0;
    mutable bool scale_valid_ = false, labels_valid_ = false;
    mutable std::vector<text::TextLayout> ylabels_;
    mutable std::vector<Vec2> scratch_; // paint-time point mapping
};

// ---------------------------------------------------------------------------
// Vec2Pad — 2D pad with a draggable dot (light direction, UV offsets,
// camera nudge). Value is in data space; default range 0..1 per axis.
// ---------------------------------------------------------------------------
class Vec2Pad : public Widget {
public:
    explicit Vec2Pad(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    Vec2Pad& set_value(Vec2 v, bool fire_callback = true) {
        v = {std::clamp(v.x, min_.x, max_.x), std::clamp(v.y, min_.y, max_.y)};
        if (v == value_) return *this;
        value_ = v;
        if (fire_callback && on_change_) on_change_(value_);
        return *this;
    }
    Vec2 value() const { return value_; }
    Vec2Pad& set_range(Vec2 min, Vec2 max) {
        min_ = min;
        max_ = max;
        set_value(value_, false);
        return *this;
    }
    Vec2Pad& on_change(std::function<void(Vec2)> f) { on_change_ = std::move(f); return *this; }

    Size measure(const Constraints& c) override { return c.clamp({120.f, 120.f}); }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        env.list.rect(bounds, t.surface, t.radius);
        env.list.hollow_rect(bounds, t.border, t.border_width, t.radius);
        Vec2 p = to_screen(value_);
        env.list.line({bounds.x, p.y}, {bounds.right(), p.y}, t.surface_alt, 1);
        env.list.line({p.x, bounds.y}, {p.x, bounds.bottom()}, t.surface_alt, 1);
        env.list.circle(p, 7, Color::white());
        env.list.circle(p, 5, t.accent);
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MousePress:
            if (enabled && bounds.contains(e.pos)) {
                ctx->capture_mouse(this);
                dragging_ = true;
                set_value(to_value(e.pos));
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            if (dragging_) {
                set_value(to_value(e.pos));
                return true;
            }
            return false;
        case Event::Type::MouseRelease:
            if (dragging_) {
                dragging_ = false;
                ctx->release_mouse(this);
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    const char* widget_type() const override { return "Vec2Pad"; }

    bool stretch_cross() const override { return false; }

private:
    Vec2 to_screen(Vec2 v) const {
        float sx = max_.x - min_.x, sy = max_.y - min_.y;
        float fx = sx > 0 ? (v.x - min_.x) / sx : 0.f;
        float fy = sy > 0 ? (v.y - min_.y) / sy : 0.f;
        return {bounds.x + clamp01(fx) * (bounds.w - 12) + 6,
                bounds.y + (1.f - clamp01(fy)) * (bounds.h - 12) + 6};
    }
    Vec2 to_value(Vec2 p) const {
        float sx = max_.x - min_.x, sy = max_.y - min_.y;
        float fx = bounds.w > 12 ? (p.x - bounds.x - 6) / (bounds.w - 12) : 0.f;
        float fy = bounds.h > 12 ? (p.y - bounds.y - 6) / (bounds.h - 12) : 0.f;
        return {min_.x + clamp01(fx) * sx, min_.y + (1.f - clamp01(fy)) * sy};
    }

    Vec2 value_{0.5f, 0.5f};
    Vec2 min_{0, 0}, max_{1, 1};
    bool dragging_ = false;
    std::function<void(Vec2)> on_change_;
};

// ---------------------------------------------------------------------------
// TreeView — indented hierarchy with expand arrows and selection. Rows are a
// flat (label, depth) list; expansion state lives here, descendants of a
// collapsed row are hidden. Apps mutate rows and call set_rows again.
// ---------------------------------------------------------------------------
class TreeView : public Widget {
public:
    struct Row {
        std::string label;
        int depth = 0;
        bool expandable = false;
    };

    explicit TreeView(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    TreeView& set_rows(std::vector<Row> rows) {
        rows_ = std::move(rows);
        expanded_.assign(rows_.size(), true);
        selected_ = -1;
        visible_valid_ = false;
        return *this;
    }
    TreeView& set_expanded(size_t index, bool open) {
        if (index < expanded_.size()) {
            expanded_[index] = open;
            visible_valid_ = false;
        }
        return *this;
    }
    bool expanded(size_t index) const {
        return index < expanded_.size() && expanded_[index];
    }
    TreeView& set_selected(long index) { selected_ = index; return *this; }
    long selected() const { return selected_; }
    size_t visible_count() const {
        refresh_visible();
        return visible_.size();
    }
    TreeView& on_select(std::function<void(size_t)> f) { on_select_ = std::move(f); return *this; }

    Size measure(const Constraints& c) override {
        refresh_metrics();
        refresh_visible();
        return c.clamp({natural_w_ * ui_scale_inv(),
                        float(visible_.size()) * row_height()});
    }

    void paint(const PaintEnv& env) const override {
        refresh_metrics();
        refresh_visible();
        const Theme& t = env.theme;
        float inv = ui_scale_inv();
        float y = bounds.y;
        for (size_t vi = 0; vi < visible_.size(); ++vi) {
            size_t i = visible_[vi];
            const Row& r = rows_[i];
            Rect row{bounds.x, y, bounds.w, row_height()};
            if (long(i) == selected_) env.list.rect(row, t.accent.faded(.25f), 4);
            else if (long(vi) == hovered_ && enabled)
                env.list.rect(row, t.surface_alt, 4);

            float indent = bounds.x + 6 + float(r.depth) * 16.f;
            if (r.expandable) {
                // chevron right (closed) / down (open)
                float cy = row.y + row_height() * .5f;
                Color mark = t.text_dim;
                if (expanded_[i]) {
                    env.list.line({indent, cy - 2}, {indent + 4, cy + 2}, mark, 2);
                    env.list.line({indent + 4, cy + 2}, {indent, cy + 6}, mark, 2);
                } else {
                    env.list.line({indent, cy - 4}, {indent + 4, cy}, mark, 2);
                    env.list.line({indent + 4, cy}, {indent, cy + 4}, mark, 2);
                }
            }
            float tx = indent + (r.expandable ? 16.f : 4.f);
            float text_h = lays_[i].size.h * inv;
            for (const auto& pg : lays_[i].glyphs) {
                const text::Glyph& g = *pg.glyph;
                if (g.uv.empty()) continue;
                env.list.glyph({tx + pg.pos.x * inv,
                                row.y + (row_height() - text_h) * .5f + pg.pos.y * inv,
                                g.size.w * inv, g.size.h * inv},
                               g.tex_id, g.uv, t.text);
            }
            y += row_height();
        }
    }

    bool on_event(const Event& e) override {
        if (e.type == Event::Type::MouseMove) {
            hovered_ = bounds.contains(e.pos) ? long(row_at(e.pos.y)) : -1;
            return false;
        }
        if (e.type == Event::Type::MousePress && enabled && bounds.contains(e.pos)) {
            long vi = row_at(e.pos.y);
            if (vi < 0) return false;
            size_t i = visible_[size_t(vi)];
            float chev_x = bounds.x + 6 + float(rows_[i].depth) * 16.f;
            if (rows_[i].expandable && e.pos.x < chev_x + 14.f) {
                expanded_[i] = !expanded_[i];
                visible_valid_ = false;
                return true;
            }
            selected_ = long(i);
            if (on_select_) on_select_(i);
            return true;
        }
        return false;
    }

    const char* widget_type() const override { return "TreeView"; }

private:
    float row_height() const { return 24.f; }
    float ui_scale_inv() const { return 1.f / ui_scale(); }
    long row_at(float y) const {
        long r = long(std::floor((y - bounds.y) / row_height()));
        return (r >= 0 && r < long(visible_.size())) ? r : -1;
    }

    void refresh_metrics() const {
        if (metrics_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        lays_.assign(rows_.size(), text::TextLayout{});
        natural_w_ = 40.f + (rows_.empty() ? 0.f
                                           : float(rows_.back().depth) * 16.f);
        for (size_t i = 0; i < rows_.size(); ++i) {
            lays_[i] = ctx->font().layout(rows_[i].label, t.s(t.base_font_px));
            natural_w_ = std::fmax(natural_w_,
                                   6.f + float(rows_[i].depth) * 16.f +
                                       (rows_[i].expandable ? 16.f : 4.f) +
                                       lays_[i].size.w);
        }
        metrics_valid_ = true;
    }

    // Rows whose every ancestor is expanded, in display order.
    void refresh_visible() const {
        if (visible_valid_) return;
        visible_.clear();
        std::vector<bool> open_stack; // per-depth expansion of ancestors
        for (size_t i = 0; i < rows_.size(); ++i) {
            int d = rows_[i].depth;
            if (size_t(d) > open_stack.size()) open_stack.resize(size_t(d), true);
            bool ancestors_open = true;
            for (int k = 0; k < d && ancestors_open; ++k)
                ancestors_open = open_stack[size_t(k)];
            open_stack.resize(size_t(d));
            open_stack.push_back(expanded_[i]);
            if (ancestors_open) visible_.push_back(i);
        }
        visible_valid_ = true;
    }

    std::vector<Row> rows_;
    std::vector<bool> expanded_;
    mutable std::vector<size_t> visible_;
    mutable std::vector<text::TextLayout> lays_;
    mutable float natural_w_ = 0;
    mutable bool metrics_valid_ = false, visible_valid_ = false;
    long selected_ = -1;
    long hovered_ = -1;
    std::function<void(size_t)> on_select_;
};

// ---------------------------------------------------------------------------
// Table — columns + header + selectable rows. Fixed column widths (0 = 90);
// cell text is clipped to its column. Non-virtualized by design for now.
// ---------------------------------------------------------------------------
class Table : public Widget {
public:
    struct ColumnSpec {
        std::string title;
        float width = 0;      // <=0 → default width
        bool right_align = false;
    };

    explicit Table(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    Table& set_columns(std::vector<ColumnSpec> cols) {
        cols_ = std::move(cols);
        metrics_valid_ = false;
        return *this;
    }
    Table& set_rows(std::vector<std::vector<std::string>> rows) {
        rows_ = std::move(rows);
        selected_ = -1;
        metrics_valid_ = false;
        return *this;
    }
    Table& set_selected(long index) { selected_ = index; return *this; }
    long selected() const { return selected_; }
    Table& on_select(std::function<void(size_t)> f) { on_select_ = std::move(f); return *this; }

    Size measure(const Constraints& c) override {
        refresh_metrics();
        float h = header_height() + float(rows_.size()) * row_height();
        return c.clamp({grid_w_, h});
    }

    void paint(const PaintEnv& env) const override {
        refresh_metrics();
        const Theme& t = env.theme;
        float inv = ui_scale_inv();

        // Header strip.
        env.list.rect({bounds.x, bounds.y, grid_w_, header_height()}, t.surface_alt, 4);
        env.list.line({bounds.x, bounds.y + header_height()},
                      {bounds.x + grid_w_, bounds.y + header_height()}, t.border, 1);
        paint_row(env, head_lays_, bounds.y, t.text_dim, true);

        float y = bounds.y + header_height();
        for (size_t r = 0; r < rows_.size(); ++r) {
            if (long(r) == selected_) {
                env.list.rect({bounds.x, y, grid_w_, row_height()},
                              t.accent.faded(.25f), 0);
            } else if (long(r) == hovered_ && enabled) {
                env.list.rect({bounds.x, y, grid_w_, row_height()}, t.surface_alt, 0);
            }
            paint_row(env, cell_lays_[r], y, t.text, false);
            y += row_height();
        }
    }

    bool on_event(const Event& e) override {
        if (e.type == Event::Type::MouseMove) {
            hovered_ = bounds.contains(e.pos) ? row_at(e.pos.y) : -1;
            return false;
        }
        if (e.type == Event::Type::MousePress && enabled && bounds.contains(e.pos)) {
            long r = row_at(e.pos.y);
            if (r >= 0) {
                selected_ = r;
                if (on_select_) on_select_(size_t(r));
                return true;
            }
        }
        return false;
    }

    const char* widget_type() const override { return "Table"; }

private:
    float row_height() const { return 24.f; }
    float header_height() const { return 24.f; }
    long row_at(float y) const {
        long r = long(std::floor((y - bounds.y - header_height()) / row_height()));
        return (r >= 0 && r < long(rows_.size())) ? r : -1;
    }
    float ui_scale_inv() const { return 1.f / ui_scale(); }

    void paint_row(const PaintEnv& env, const std::vector<text::TextLayout>& lays,
                   float y, Color color, bool header) const {
        float inv = ui_scale_inv();
        float x = bounds.x;
        for (size_t k = 0; k < lays.size(); ++k) {
            const auto& lay = lays[k];
            float col_w = cols_[k].width > 0 ? cols_[k].width : 90.f;
            float tx = cols_[k].right_align && !header
                           ? x + col_w - 8 - lay.size.w * inv
                           : x + 8;
            if (tx + lay.size.w * inv > x + col_w - 2) {
                env.list.push_clip({x, y, col_w, row_height() + (header ? 0 : 0)});
                paint_cells(env, lay, tx, y, color, inv);
                env.list.pop_clip();
            } else {
                paint_cells(env, lay, tx, y, color, inv);
            }
            x += col_w;
        }
    }
    void paint_cells(const PaintEnv& env, const text::TextLayout& lay, float tx,
                     float y, Color color, float inv) const {
        float text_h = lay.size.h * inv;
        float cy = y + (row_height() - text_h) * .5f;
        for (const auto& pg : lay.glyphs) {
            const text::Glyph& g = *pg.glyph;
            if (g.uv.empty()) continue;
            env.list.glyph({tx + pg.pos.x * inv, cy + pg.pos.y * inv,
                            g.size.w * inv, g.size.h * inv},
                           g.tex_id, g.uv, color);
        }
    }

    void refresh_metrics() const {
        if (metrics_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        float base = t.s(t.base_font_px), small = t.s(t.small_font_px);
        head_lays_.clear();
        grid_w_ = 0;
        for (const auto& col : cols_) {
            head_lays_.push_back(ctx->font().layout(col.title, small));
            grid_w_ += col.width > 0 ? col.width : 90.f;
        }
        cell_lays_.assign(rows_.size(), {});
        for (size_t r = 0; r < rows_.size(); ++r) {
            cell_lays_[r].reserve(rows_[r].size());
            for (const auto& cell : rows_[r])
                cell_lays_[r].push_back(ctx->font().layout(cell, base));
        }
        metrics_valid_ = true;
    }

    std::vector<ColumnSpec> cols_;
    std::vector<std::vector<std::string>> rows_;
    mutable std::vector<text::TextLayout> head_lays_;
    mutable std::vector<std::vector<text::TextLayout>> cell_lays_;
    mutable float grid_w_ = 0;
    mutable bool metrics_valid_ = false;
    long selected_ = -1, hovered_ = -1;
    std::function<void(size_t)> on_select_;
};

// ---------------------------------------------------------------------------
// PropertyGrid — the CG inspector: label|editor rows with collapsible
// sections. Rows keep a fixed label column and give the editor the rest.
// ---------------------------------------------------------------------------
class PropertyRow : public Widget {
public:
    PropertyRow(std::string label, float label_w = 110.f, std::string widget_id = {})
        : Widget(std::move(widget_id)), label_w_(label_w) {
        Widget::add(std::make_unique<Label>(std::move(label)));
    }

    Label& label() { return *static_cast<Label*>(children[0].get()); }
    Widget& editor() { return *children[1]; }

    Size measure(const Constraints& c) override {
        Size lab = children[0]->measure(Constraints::loose({label_w_, kInfinite}));
        Size s{label_w_, lab.h};
        if (children.size() > 1) {
            Size ed = children[1]->measure(Constraints::loose({kInfinite, kInfinite}));
            s.w += 8 + ed.w;
            s.h = std::fmax(s.h, ed.h);
        }
        return c.clamp(s);
    }

    void arrange(const Rect& area) override {
        bounds = area;
        children[0]->arrange({area.x, area.y, label_w_, area.h});
        if (children.size() > 1)
            children[1]->arrange({area.x + label_w_ + 8, area.y,
                                  std::fmax(area.w - label_w_ - 8, 0.f), area.h});
    }

    const char* widget_type() const override { return "PropertyRow"; }

    bool stretch_cross() const override { return false; }

private:
    float label_w_;
};

class PropertyGrid : public Column {
public:
    using Column::Column;

    // Returns the row; add the editor into it: `row.add<Slider>(0.f, 1.f)`.
    PropertyRow& add_property(std::string label, float label_w = 110.f) {
        return add<PropertyRow>(std::move(label), label_w);
    }
    // Collapsible section: returns the nested grid to fill.
    PropertyGrid& add_section(std::string title, bool open = false) {
        auto& exp = add<Expander>(std::move(title));
        exp.set_open(open);
        return exp.add<PropertyGrid>();
    }

    const char* widget_type() const override { return "PropertyGrid"; }
};

} // namespace slikui
