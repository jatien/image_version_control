#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "diff.hpp"   // Box
#include "image.hpp"

// Edit operations: what an editor says it did to an image.
//
// Written as text, one operation per line:
//
//   rotate degrees=90 file=photo.png
//   filter name=sharpen region=10,20,64,64 file="my photo.png"
//
// i.e. an action followed by key=value parameters (values with spaces in
// double quotes). file= says which image it applies to; it may be omitted
// when only one image changed.
//
// Known actions are listed in known_actions(). Some can be REPLAYED: ivc
// applies them itself to the previous version and checks the result
// against the committed image. The others are recorded as declared but
// cannot be verified; giving them a region= lets ivc check that the changes
// stay inside it. Action names map to C2PA actions where one exists.

struct Operation {
    std::string file;                           // repository path
    std::string action;
    std::map<std::string, std::string> params;  // sorted, excludes file=
    std::string tool;                           // e.g. "GIMP 2.10.36"; may be empty

    // "action k=v k=v" with keys sorted and values quoted when needed.
    std::string canonical() const;
    // SHA-256 (hex) of file, canonical form and tool: identifies this exact edit.
    std::string id() const;
};

// Parses one operation. `default_file` is used when there is no file= (the
// file may stay empty; Repository::commit fills it in when unambiguous).
// Throws std::invalid_argument on syntax errors.
Operation parse_operation(const std::string& text, const std::string& default_file = "");

// Splits "a b=c d=\"e f\"" into ["a", "b=c", "d=e f"].
std::vector<std::string> split_words(const std::string& text);
// Quotes a value if it contains spaces, quotes or is empty.
std::string quote_if_needed(const std::string& value);

struct ActionSpec {
    std::string name;
    std::string c2pa;          // matching C2PA action ("" if none)
    bool replayable;           // ivc can apply it itself
    bool geometric;            // changes the size or position of pixels
    std::vector<std::string> required;  // required parameters
    std::string help;
};

const std::vector<ActionSpec>& known_actions();
const ActionSpec* find_action(const std::string& name);

// Checks the parameters of a known action (unknown actions are allowed and
// treated as non-replayable). Throws std::invalid_argument with a clear
// message. `width` and `height` are the image size before the operation,
// or 0 to skip bounds checks.
void validate_operation(const Operation& op, int width = 0, int height = 0);

// The region= parameter, if any.
std::optional<Box> operation_region(const Operation& op);

// Applies a replayable operation. Throws if the action is not replayable.
Image replay_operation(const Image& img, const Operation& op);

// Where a box ends up after a geometric operation on an image of size w x h
// (nothing returned if the box is cropped away).
std::optional<Box> transform_box(const Box& b, const Operation& op, int w, int h);

// ---- checking an edit against the declared operations ---------------------

enum class CheckResult {
    Verified,    // replaying the operations reproduces the image exactly
    Consistent,  // same as the replayed result except invisible differences
    Declared,    // visible changes, all covered by declared (non-replayable) operations
    Mismatch,    // the image does not match what was declared
    Undeclared,  // changed, but no operations were declared
};

const char* check_name(CheckResult r);

struct OperationCheck {
    CheckResult result = CheckResult::Undeclared;
    std::string summary;               // one line
    std::vector<std::string> details;  // e.g. undeclared regions
};

// Applies the replayable operations to `before`, skips the others (keeping
// their regions), and compares the result with `after` using the exact hash
// and the SSIM difference.
OperationCheck check_operations(const Image& before, const Image& after,
                                const std::vector<Operation>& ops);

// How far (pixels) a visible change may extend beyond a declared region and
// still count as inside it: SSIM spreads a change over its neighbourhood.
constexpr int kRegionMargin = 24;
