#include "operations.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "gray.hpp"
#include "image_hash.hpp"
#include "orientation.hpp"
#include "sha256.hpp"

// ---------------------------------------------------------------- text

std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    std::string cur;
    bool in_word = false, quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '\\' && i + 1 < text.size()) {
                cur += text[++i];
            } else if (c == '"') {
                quoted = false;
            } else {
                cur += c;
            }
        } else if (c == '"') {
            quoted = true;
            in_word = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (in_word) words.push_back(cur);
            cur.clear();
            in_word = false;
        } else {
            cur += c;
            in_word = true;
        }
    }
    if (quoted) throw std::invalid_argument("unclosed quote in: " + text);
    if (in_word) words.push_back(cur);
    return words;
}

std::string quote_if_needed(const std::string& value) {
    const bool needs = value.empty() || value.find_first_of(" \t\"\\") != std::string::npos;
    if (!needs) return value;
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string Operation::canonical() const {
    std::string s = action;
    for (const auto& [k, v] : params) s += " " + k + "=" + quote_if_needed(v);
    return s;
}

std::string Operation::id() const {
    return to_hex(Sha256::hash("ivc-op 1\nfile " + file + "\nop " + canonical() + "\ntool " +
                               tool + "\n"));
}

Operation parse_operation(const std::string& text, const std::string& default_file) {
    if (text.find('\n') != std::string::npos)
        throw std::invalid_argument("an operation must be on one line");
    const std::vector<std::string> words = split_words(text);
    if (words.empty()) throw std::invalid_argument("empty operation");

    Operation op;
    op.action = words[0];
    if (op.action.find('=') != std::string::npos)
        throw std::invalid_argument("an operation starts with its action, e.g. 'rotate degrees=90'");
    for (std::size_t i = 1; i < words.size(); ++i) {
        const std::size_t eq = words[i].find('=');
        if (eq == std::string::npos || eq == 0)
            throw std::invalid_argument("expected key=value, got '" + words[i] + "'");
        const std::string key = words[i].substr(0, eq), value = words[i].substr(eq + 1);
        if (key == "file") op.file = value;
        else if (!op.params.emplace(key, value).second)
            throw std::invalid_argument("parameter '" + key + "' given twice");
    }
    if (op.file.empty()) op.file = default_file;  // may stay empty: the caller decides
    return op;
}

// ---------------------------------------------------------------- registry

const std::vector<ActionSpec>& known_actions() {
    static const std::vector<ActionSpec> specs = {
        // Replayable: ivc can apply these itself.
        {"rotate", "c2pa.orientation", true, true, {"degrees"},
         "degrees=90|180|270, counter-clockwise (270 = 90 clockwise)"},
        {"flip", "c2pa.orientation", true, true, {"axis"},
         "axis=horizontal (left-right) | vertical (top-bottom)"},
        {"crop", "c2pa.cropped", true, true, {"x", "y", "width", "height"},
         "x= y= width= height= in pixels"},
        {"resize", "c2pa.resized", true, true, {"width", "height"},
         "width= height= in pixels (replayed approximately)"},
        {"brightness", "c2pa.color_adjustments", true, false, {"delta"},
         "delta=-255..255, added to R, G and B"},
        {"grayscale", "c2pa.color_adjustments", true, false, {},
         "luma 0.299 R + 0.587 G + 0.114 B (replayed approximately)"},
        // Declared only: recorded, located by region=, not replayed.
        {"color", "c2pa.color_adjustments", false, false, {},
         "any other colour/contrast adjustment; optional region="},
        {"filter", "c2pa.filtered", false, false, {"name"},
         "name=<filter>; optional region="},
        {"drawing", "c2pa.drawing", false, false, {},
         "brush, paint, clone...; optional region="},
        {"ai_edit", "c2pa.edited", false, false, {},
         "edit made with a generative AI tool (C2PA digital source type "
         "compositeWithTrainedAlgorithmicMedia); optional region= model="},
        {"ai_generate", "c2pa.created", false, false, {},
         "image created by a generative AI tool (C2PA digital source type "
         "trainedAlgorithmicMedia); optional model="},
    };
    return specs;
}

const ActionSpec* find_action(const std::string& name) {
    for (const ActionSpec& s : known_actions())
        if (s.name == name) return &s;
    return nullptr;
}

namespace {

int int_param(const Operation& op, const std::string& key) {
    const auto it = op.params.find(key);
    if (it == op.params.end())
        throw std::invalid_argument(op.action + " needs " + key + "=");
    try {
        std::size_t used = 0;
        const int v = std::stoi(it->second, &used);
        if (used != it->second.size()) throw std::invalid_argument("");
        return v;
    } catch (const std::exception&) {
        throw std::invalid_argument(op.action + ": " + key + "=" + it->second +
                                    " is not a whole number");
    }
}

Orientation orientation_of(const Operation& op) {
    if (op.action == "rotate") {
        const int d = int_param(op, "degrees");
        if (d == 90) return Orientation::Rot90;
        if (d == 180) return Orientation::Rot180;
        if (d == 270) return Orientation::Rot270;
        throw std::invalid_argument("rotate: degrees must be 90, 180 or 270");
    }
    const std::string& axis = op.params.at("axis");
    if (axis == "horizontal") return Orientation::FlipH;
    if (axis == "vertical") return Orientation::FlipV;
    throw std::invalid_argument("flip: axis must be horizontal or vertical");
}

Box crop_box(const Operation& op) {
    return {int_param(op, "x"), int_param(op, "y"), int_param(op, "width"),
            int_param(op, "height")};
}

}  // namespace

std::optional<Box> operation_region(const Operation& op) {
    const auto it = op.params.find("region");
    if (it == op.params.end()) return std::nullopt;
    Box b;
    char c1 = 0, c2 = 0, c3 = 0;
    std::istringstream in(it->second);
    if (!(in >> b.x >> c1 >> b.y >> c2 >> b.width >> c3 >> b.height) || c1 != ',' ||
        c2 != ',' || c3 != ',' || b.width <= 0 || b.height <= 0 || b.x < 0 || b.y < 0)
        throw std::invalid_argument(op.action + ": region must be x,y,width,height, got '" +
                                    it->second + "'");
    return b;
}

void validate_operation(const Operation& op, int width, int height) {
    if (op.tool.find('\n') != std::string::npos)
        throw std::invalid_argument("tool must be one line");
    const ActionSpec* spec = find_action(op.action);
    for (char c : op.action)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-'))
            throw std::invalid_argument("invalid action name '" + op.action + "'");
    if (op.params.count("region")) operation_region(op);  // throws if malformed
    if (!spec) return;  // unknown actions are allowed, treated as declared only

    for (const auto& key : spec->required)
        if (!op.params.count(key)) throw std::invalid_argument(op.action + " needs " + key + "=");

    if (op.action == "rotate" || op.action == "flip") {
        orientation_of(op);
    } else if (op.action == "crop") {
        const Box b = crop_box(op);
        if (b.x < 0 || b.y < 0 || b.width <= 0 || b.height <= 0)
            throw std::invalid_argument("crop: x, y must be >= 0 and width, height > 0");
        if (width > 0 && (b.x + b.width > width || b.y + b.height > height))
            throw std::invalid_argument("crop: box goes outside the " + std::to_string(width) +
                                        "x" + std::to_string(height) + " image");
    } else if (op.action == "resize") {
        if (int_param(op, "width") <= 0 || int_param(op, "height") <= 0)
            throw std::invalid_argument("resize: width and height must be > 0");
    } else if (op.action == "brightness") {
        const int d = int_param(op, "delta");
        if (d < -255 || d > 255) throw std::invalid_argument("brightness: delta must be -255..255");
    }
}

// ---------------------------------------------------------------- replay

Image replay_operation(const Image& img, const Operation& op) {
    const ActionSpec* spec = find_action(op.action);
    if (!spec || !spec->replayable)
        throw std::invalid_argument("'" + op.action + "' cannot be replayed");
    validate_operation(op, img.width, img.height);

    if (op.action == "rotate" || op.action == "flip") return apply_orientation(img, orientation_of(op));

    if (op.action == "crop") {
        const Box b = crop_box(op);
        Image out;
        out.width = b.width;
        out.height = b.height;
        out.channels = img.channels;
        out.pixels.resize(static_cast<std::size_t>(b.width) * b.height * img.channels);
        const std::size_t row = static_cast<std::size_t>(b.width) * img.channels;
        for (int y = 0; y < b.height; ++y)
            std::copy_n(&img.pixels[img.index(b.x, b.y + y)], row, &out.pixels[out.index(0, y)]);
        return out;
    }

    if (op.action == "resize") {
        const int w = int_param(op, "width"), h = int_param(op, "height");
        Image out;
        out.width = w;
        out.height = h;
        out.channels = img.channels;
        out.pixels.resize(static_cast<std::size_t>(w) * h * img.channels);
        for (int c = 0; c < img.channels; ++c) {
            GrayImage plane;
            plane.width = img.width;
            plane.height = img.height;
            plane.values.resize(static_cast<std::size_t>(img.width) * img.height);
            for (std::size_t i = 0; i < plane.values.size(); ++i)
                plane.values[i] = img.pixels[i * img.channels + c];
            const GrayImage r = resize_area(plane, w, h);
            for (std::size_t i = 0; i < r.values.size(); ++i)
                out.pixels[i * img.channels + c] =
                    static_cast<uint8_t>(std::clamp(std::lround(r.values[i]), 0L, 255L));
        }
        return out;
    }

    Image out = img;
    const int colours = std::min(img.channels, 3);
    if (op.action == "brightness") {
        const int d = int_param(op, "delta");
        for (std::size_t p = 0; p < out.pixels.size(); p += out.channels)
            for (int c = 0; c < colours; ++c)
                out.pixels[p + c] = static_cast<uint8_t>(std::clamp(out.pixels[p + c] + d, 0, 255));
        return out;
    }

    // grayscale
    if (img.channels >= 3)
        for (std::size_t p = 0; p < out.pixels.size(); p += out.channels) {
            const auto y = static_cast<uint8_t>(std::clamp(
                std::lround(0.299 * out.pixels[p] + 0.587 * out.pixels[p + 1] +
                            0.114 * out.pixels[p + 2]),
                0L, 255L));
            out.pixels[p] = out.pixels[p + 1] = out.pixels[p + 2] = y;
        }
    return out;
}

std::optional<Box> transform_box(const Box& b, const Operation& op, int w, int h) {
    const ActionSpec* spec = find_action(op.action);
    if (!spec || !spec->geometric) return b;

    if (op.action == "rotate" || op.action == "flip") {
        switch (orientation_of(op)) {
            case Orientation::Rot90:  return Box{b.y, w - b.x - b.width, b.height, b.width};
            case Orientation::Rot180: return Box{w - b.x - b.width, h - b.y - b.height, b.width, b.height};
            case Orientation::Rot270: return Box{h - b.y - b.height, b.x, b.height, b.width};
            case Orientation::FlipH:  return Box{w - b.x - b.width, b.y, b.width, b.height};
            case Orientation::FlipV:  return Box{b.x, h - b.y - b.height, b.width, b.height};
            default:                  return b;
        }
    }
    if (op.action == "crop") {
        const Box c = crop_box(op);
        const int x0 = std::max(b.x, c.x), y0 = std::max(b.y, c.y);
        const int x1 = std::min(b.x + b.width, c.x + c.width);
        const int y1 = std::min(b.y + b.height, c.y + c.height);
        if (x1 <= x0 || y1 <= y0) return std::nullopt;
        return Box{x0 - c.x, y0 - c.y, x1 - x0, y1 - y0};
    }
    // resize: scale, rounding outwards.
    const double sx = double(int_param(op, "width")) / w, sy = double(int_param(op, "height")) / h;
    const int x0 = static_cast<int>(std::floor(b.x * sx)), y0 = static_cast<int>(std::floor(b.y * sy));
    const int x1 = static_cast<int>(std::ceil((b.x + b.width) * sx));
    const int y1 = static_cast<int>(std::ceil((b.y + b.height) * sy));
    return Box{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
}

// ---------------------------------------------------------------- checking

const char* check_name(CheckResult r) {
    switch (r) {
        case CheckResult::Verified:   return "VERIFIED";
        case CheckResult::Consistent: return "CONSISTENT";
        case CheckResult::Declared:   return "DECLARED";
        case CheckResult::Mismatch:   return "MISMATCH";
        case CheckResult::Undeclared: return "UNDECLARED";
    }
    return "?";
}

namespace {

bool inside(const Box& inner, const Box& outer, int margin) {
    return inner.x >= outer.x - margin && inner.y >= outer.y - margin &&
           inner.x + inner.width <= outer.x + outer.width + margin &&
           inner.y + inner.height <= outer.y + outer.height + margin;
}

std::string box_str(const Box& b) {
    return "at " + std::to_string(b.x) + "," + std::to_string(b.y) + " size " +
           std::to_string(b.width) + "x" + std::to_string(b.height);
}

}  // namespace

OperationCheck check_operations(const Image& before, const Image& after,
                                const std::vector<Operation>& ops) {
    OperationCheck out;
    if (ops.empty()) {
        out.result = CheckResult::Undeclared;
        out.summary = "changed, but no operations were declared";
        return out;
    }

    // Replay what can be replayed; keep the regions of what cannot.
    Image r = before;
    std::vector<Box> regions;
    bool unbounded = false;  // a non-replayable operation without a region
    int replayed = 0;
    std::vector<std::string> skipped;
    for (const Operation& op : ops) {
        const ActionSpec* spec = find_action(op.action);
        if (spec && spec->replayable) {
            std::vector<Box> moved;
            for (const Box& b : regions)
                if (auto t = transform_box(b, op, r.width, r.height)) moved.push_back(*t);
            regions = moved;
            r = replay_operation(r, op);
            ++replayed;
        } else {
            skipped.push_back(op.action);
            if (auto b = operation_region(op)) regions.push_back(*b);
            else unbounded = true;
        }
    }

    if (r.width != after.width || r.height != after.height) {
        out.result = CheckResult::Mismatch;
        out.summary = "after the declared operations the image would be " +
                      std::to_string(r.width) + "x" + std::to_string(r.height) + ", but it is " +
                      std::to_string(after.width) + "x" + std::to_string(after.height);
        return out;
    }

    if (exact_hash(r) == exact_hash(after)) {
        out.result = skipped.empty() ? CheckResult::Verified : CheckResult::Consistent;
        out.summary = skipped.empty()
                          ? "replaying " + std::to_string(replayed) +
                                " operation(s) reproduces the image exactly"
                          : "replayable operations reproduce the image exactly; the declared " +
                                std::to_string(skipped.size()) + " other(s) changed nothing visible";
        return out;
    }

    // No orientation search: an undeclared rotation must show up as a change.
    const ImageDiff d = diff_images(r, after, 0.6f, false);
    if (d.regions.empty()) {
        out.result = CheckResult::Consistent;
        out.summary = "matches the replayed operations up to invisible differences"
                      " (encoding or rounding)";
        return out;
    }

    std::vector<Box> outside;
    for (const Region& reg : d.regions) {
        bool covered = unbounded;
        for (const Box& b : regions) covered = covered || inside(reg.box, b, kRegionMargin);
        if (!covered) outside.push_back(reg.box);
    }

    if (outside.empty()) {
        out.result = CheckResult::Declared;
        out.summary = std::to_string(d.regions.size()) +
                      " visible change(s), covered by declared operations ivc cannot replay";
        if (unbounded)
            out.details.push_back("note: an operation without region= covers the whole image");
        return out;
    }

    out.result = CheckResult::Mismatch;
    out.summary = std::to_string(outside.size()) +
                  " visible change(s) not explained by the declared operations";
    for (const Box& b : outside) out.details.push_back("undeclared change " + box_str(b));
    return out;
}
