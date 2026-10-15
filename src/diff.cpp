#include "diff.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "image_hash.hpp"
#include "orientation.hpp"
#include "ssim.hpp"
#include "stb_image_write.h"

namespace {

// The RGB channels of an image, each resized to w x h by area averaging.
std::vector<GrayImage> rgb_planes(const Image& img, int w, int h) {
    std::vector<GrayImage> planes;
    for (int c = 0; c < 3; ++c) {
        GrayImage p = img.channels >= 3 ? channel_plane(img, c) : channel_plane(img, 0);
        planes.push_back(p.width == w && p.height == h ? p : resize_area(p, w, h));
    }
    return planes;
}

GrayImage luma(const std::vector<GrayImage>& rgb) {
    GrayImage y = rgb[0];
    for (std::size_t i = 0; i < y.values.size(); ++i)
        y.values[i] = 0.299f * rgb[0].values[i] + 0.587f * rgb[1].values[i] +
                      0.114f * rgb[2].values[i];
    return y;
}

// Groups map points below the threshold into 8-connected regions.
std::vector<Region> find_regions(const GrayImage& map, float threshold, int scale,
                                 int ref_w, int ref_h) {
    const int w = map.width, h = map.height;
    std::vector<int> label(static_cast<std::size_t>(w) * h, -1);
    std::vector<Region> regions;
    std::vector<int> stack;

    for (int start = 0; start < w * h; ++start) {
        if (label[start] != -1 || map.values[start] >= threshold) continue;

        // Flood fill from this point.
        const int id = static_cast<int>(regions.size());
        int x0 = w, y0 = h, x1 = -1, y1 = -1, count = 0;
        float lowest = 1.0f;
        stack.assign(1, start);
        label[start] = id;
        while (!stack.empty()) {
            const int p = stack.back();
            stack.pop_back();
            const int px = p % w, py = p / w;
            x0 = std::min(x0, px); x1 = std::max(x1, px);
            y0 = std::min(y0, py); y1 = std::max(y1, py);
            ++count;
            lowest = std::min(lowest, map.values[p]);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = px + dx, ny = py + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const int q = ny * w + nx;
                    if (label[q] == -1 && map.values[q] < threshold) {
                        label[q] = id;
                        stack.push_back(q);
                    }
                }
        }

        Region r;
        r.box.x = x0 * scale;
        r.box.y = y0 * scale;
        r.box.width = std::min((x1 + 1) * scale, ref_w) - r.box.x;
        r.box.height = std::min((y1 + 1) * scale, ref_h) - r.box.y;
        r.area = count * scale * scale;
        r.min_ssim = lowest;
        regions.push_back(r);
    }

    std::sort(regions.begin(), regions.end(),
              [](const Region& a, const Region& b) { return a.area > b.area; });
    return regions;
}

}  // namespace

ImageDiff diff_images(const Image& reference, const Image& image, float threshold) {
    ImageDiff d;
    d.reference_id = exact_hash(reference);
    d.image_id = exact_hash(image);
    d.comparison = compare_images(reference, image);

    // Turn the image back to the reference's orientation.
    const Image aligned = apply_orientation(image, inverse(d.comparison.orientation));

    // Exact, pixel-by-pixel differences.
    if (aligned.width == reference.width && aligned.height == reference.height &&
        aligned.channels == reference.channels) {
        d.exact_available = true;
        int x0 = reference.width, y0 = reference.height, x1 = -1, y1 = -1;
        for (int y = 0; y < reference.height; ++y)
            for (int x = 0; x < reference.width; ++x) {
                bool differs = false;
                for (int c = 0; c < reference.channels; ++c)
                    differs = differs || reference.at(x, y, c) != aligned.at(x, y, c);
                if (differs) {
                    ++d.exact_pixels;
                    x0 = std::min(x0, x); x1 = std::max(x1, x);
                    y0 = std::min(y0, y); y1 = std::max(y1, y);
                }
            }
        if (d.exact_pixels > 0) d.exact_box = {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
    }

    // Common size: the smaller of the two, so nothing is ever enlarged.
    const int base_w = std::min(reference.width, aligned.width);
    const int base_h = std::min(reference.height, aligned.height);

    // Very small images are compared at full size.
    const int s2 = std::min(base_w, base_h) >= 64 ? 2 : 1;
    const int s4 = s2 * 2;
    const int w2 = base_w / s2, h2 = base_h / s2;
    const int w4 = std::max(1, base_w / s4), h4 = std::max(1, base_h / s4);

    // Brightness at 1/2 size.
    const SsimMap luma_map = ssim(luma(rgb_planes(reference, w2, h2)),
                                  luma(rgb_planes(aligned, w2, h2)));

    // Each colour channel at 1/4 size; keep the lowest.
    const auto ref4 = rgb_planes(reference, w4, h4);
    const auto img4 = rgb_planes(aligned, w4, h4);
    GrayImage colour = ssim(ref4[0], img4[0]).map;
    for (int c = 1; c < 3; ++c) {
        const SsimMap s = ssim(ref4[c], img4[c]);
        for (std::size_t i = 0; i < colour.values.size(); ++i)
            colour.values[i] = std::min(colour.values[i], s.map.values[i]);
    }

    // Combine on the 1/2-size grid: the worse of the two at each point.
    d.ssim_map = luma_map.map;
    double total = 0;
    for (int y = 0; y < h2; ++y)
        for (int x = 0; x < w2; ++x) {
            const int cx = std::min(x * w4 / w2, w4 - 1), cy = std::min(y * h4 / h2, h4 - 1);
            float& v = d.ssim_map.values[static_cast<std::size_t>(y) * w2 + x];
            v = std::min(v, colour.at(cx, cy));
            total += v;
        }
    d.mean_ssim = total / (static_cast<double>(w2) * h2);

    // Map coordinates -> reference pixels.
    d.map_scale = std::max(1, static_cast<int>(std::lround(double(reference.width) / w2)));
    d.regions = find_regions(d.ssim_map, threshold, d.map_scale, reference.width,
                             reference.height);
    return d;
}

Image render_diff(const Image& reference, const ImageDiff& diff, float threshold) {
    Image out;
    out.width = reference.width;
    out.height = reference.height;
    out.channels = 3;
    out.pixels.resize(static_cast<std::size_t>(out.width) * out.height * 3);

    const GrayImage& m = diff.ssim_map;
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            float g;
            if (reference.channels >= 3)
                g = 0.299f * reference.at(x, y, 0) + 0.587f * reference.at(x, y, 1) +
                    0.114f * reference.at(x, y, 2);
            else
                g = reference.at(x, y, 0);
            g = 40 + 0.5f * g;  // dimmed grey background

            const int mx = std::min(x * m.width / out.width, m.width - 1);
            const int my = std::min(y * m.height / out.height, m.height - 1);
            const float s = m.at(mx, my);

            // Red tint grows as SSIM drops from 0.95; full red below threshold.
            float t = std::clamp((0.95f - s) / (0.95f - threshold), 0.0f, 1.0f);
            const std::size_t i = out.index(x, y);
            out.pixels[i + 0] = static_cast<uint8_t>(g + t * (255 - g));
            out.pixels[i + 1] = static_cast<uint8_t>(g * (1 - 0.7f * t));
            out.pixels[i + 2] = static_cast<uint8_t>(g * (1 - 0.7f * t));
        }

    // Outline each region in yellow, 2 pixels outside its box.
    for (const Region& r : diff.regions) {
        const int x0 = std::max(0, r.box.x - 2), y0 = std::max(0, r.box.y - 2);
        const int x1 = std::min(out.width - 1, r.box.x + r.box.width + 1);
        const int y1 = std::min(out.height - 1, r.box.y + r.box.height + 1);
        auto paint = [&](int x, int y) {
            const std::size_t i = out.index(x, y);
            out.pixels[i] = 255; out.pixels[i + 1] = 220; out.pixels[i + 2] = 0;
        };
        for (int x = x0; x <= x1; ++x) { paint(x, y0); paint(x, y1); }
        for (int y = y0; y <= y1; ++y) { paint(x0, y); paint(x1, y); }
    }
    return out;
}

void write_png(const std::string& path, const Image& img) {
    if (!stbi_write_png(path.c_str(), img.width, img.height, img.channels,
                        img.pixels.data(), img.width * img.channels))
        throw std::runtime_error("Could not write '" + path + "'");
}
