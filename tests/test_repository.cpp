// Tests for the version control (Repository).
// Each test works in a fresh folder under the build directory.
// Run with:   ctest --test-dir build --output-on-failure

#include <fstream>
#include <iostream>
#include <string>

#include "diff.hpp"
#include "image.hpp"
#include "orientation.hpp"
#include "operations.hpp"
#include "repository.hpp"
#include "signing.hpp"

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << '\n';
    if (!ok) ++failures;
}

template <typename F>
static void check_throws(F f, const std::string& what) {
    bool threw = false;
    try {
        f();
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, what);
}

static const fs::path samples = SAMPLES_DIR;

static fs::path fresh_dir(const std::string& name) {
    const fs::path d = fs::path(BINARY_DIR) / "repo_tests" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

static void put(const fs::path& dir, const std::string& name, const std::string& sample) {
    fs::create_directories((dir / name).parent_path());
    fs::copy_file(samples / sample, dir / name, fs::copy_options::overwrite_existing);
}

static bool same_bytes(const fs::path& a, const fs::path& b) {
    return file_sha256(a) == file_sha256(b);
}

static bool has_detail(const Change& c, const std::string& text) {
    for (const auto& d : c.details)
        if (d.find(text) != std::string::npos) return true;
    return false;
}

static void history_and_going_back() {
    std::cout << "Commits, history and going back\n";
    const fs::path dir = fresh_dir("history");
    Repository repo = Repository::init(dir);
    check(repo.current_branch() == "main" && !repo.head_commit(), "new repository: on main, no commits");
    check_throws([&] { repo.commit("nothing"); }, "committing with nothing staged fails");

    put(dir, "photo.png", "00_original.png");
    put(dir, "album/sunset.png", "09_unrelated.png");
    check(repo.status().untracked.size() == 2, "two untracked images found, including in a subfolder");
    repo.add({dir});
    const std::string c1 = repo.commit("first");
    const Commit first = repo.read_commit(c1);
    check(first.files.size() == 2 && first.changes.size() == 2 &&
              first.changes[0].kind == "added" && first.changes[1].kind == "added",
          "first commit: both files added");

    // Edit, recording the editor's tool key.
    put(dir, "photo.png", "03_small_region_edit.png");
    check(repo.status().unstaged.size() == 1, "edited file shows as changed but not staged");
    repo.add({dir / "photo.png"});
    Operation brush = parse_operation("drawing tool=brush region=164,160,16,16");
    brush.tool = "GIMP 2.10";
    const std::string c2 = repo.commit("remove window", {brush});
    const Commit second = repo.read_commit(c2);
    check(second.parents == std::vector<std::string>{c1}, "second commit's parent is the first");
    check(second.operations.size() == 1 && second.operations[0].file == "photo.png" &&
              second.operations[0].tool == "GIMP 2.10" &&
              [&] { Operation b = brush; b.file = "photo.png"; return b.id(); }() ==
                  second.operations[0].id(),
          "declared operation recorded (file filled in, tool, same ID)");
    check(has_detail(second.changes[0], "check DECLARED"),
          "the drawing inside its region is checked as DECLARED");
    check(second.changes.size() == 1 && second.changes[0].kind == "modified" &&
              has_detail(second.changes[0], "pixels 256 changed at 164,160 size 16x16") &&
              has_detail(second.changes[0], "visible 1 region(s)"),
          "change describes the 256 pixels and 1 visible region");

    // A pure rotation is identified as such.
    write_png((dir / "photo.png").string(),
              apply_orientation(load_image((samples / "03_small_region_edit.png").string()),
                                Orientation::Rot90));
    repo.add({dir / "photo.png"});
    const Commit third = repo.read_commit(repo.commit("rotate", {parse_operation("rotate degrees=90")}));
    check(has_detail(third.changes[0], "detected rotated 90") &&
              has_detail(third.changes[0], "pixels 0 changed"),
          "rotation detected: 'rotated 90', 0 pixels changed");
    check(has_detail(third.changes[0], "check VERIFIED"), "the declared rotation is VERIFIED by replay");

    // Going back restores the exact original bytes; going forward again works.
    repo.checkout("HEAD~2");
    check(!repo.current_branch() && repo.head_commit() == c1, "checkout HEAD~2: detached at first commit");
    check(same_bytes(dir / "photo.png", samples / "00_original.png"),
          "photo.png is byte-identical to the original");
    check(repo.status().clean(), "status is clean after going back");
    repo.checkout("main");
    check(repo.current_branch() == "main" && repo.head_commit() == third.id,
          "checkout main: back on the latest commit");

    // History.
    const auto ids = repo.log();
    check(ids.size() == 3 && ids[0] == third.id && ids[2] == c1, "log lists 3 commits, newest first");
    check(repo.resolve(c1.substr(0, 6)) == c1, "a commit can be named by a short prefix");
    check(repo.resolve("main~1") == c2, "main~1 is the second commit");
    check_throws([&] { repo.resolve("HEAD~5"); }, "going back past the first commit fails");
}

static void branches_and_safety() {
    std::cout << "Branches and safety\n";
    const fs::path dir = fresh_dir("branches");
    Repository repo = Repository::init(dir);
    put(dir, "photo.png", "00_original.png");
    repo.add({dir});
    const std::string base = repo.commit("base");

    repo.checkout_new_branch("edit");
    put(dir, "photo.png", "03_small_region_edit.png");
    repo.add({dir});
    const std::string on_edit = repo.commit("edit on branch");

    repo.checkout("main");
    check(repo.head_commit() == base && same_bytes(dir / "photo.png", samples / "00_original.png"),
          "main still has the original");
    repo.checkout("edit");
    check(repo.head_commit() == on_edit &&
              same_bytes(dir / "photo.png", samples / "03_small_region_edit.png"),
          "edit branch has the edited photo");
    check(repo.branches() == std::vector<std::string>({"edit", "main"}), "both branches listed");

    // Uncommitted changes block moving.
    put(dir, "photo.png", "04_brightness_plus5.png");
    check_throws([&] { repo.checkout("main"); }, "checkout refuses with uncommitted changes");
    repo.restore(dir / "photo.png");
    check(repo.status().clean(), "restore brings back the committed file");

    // Untracked file in the way.
    repo.checkout("main");
    repo.checkout_new_branch("extra");
    put(dir, "new.png", "09_unrelated.png");
    repo.add({dir / "new.png"});
    repo.commit("add new.png");
    repo.checkout("main");
    check(!fs::exists(dir / "new.png"), "switching to main removes a file main does not have");
    put(dir, "new.png", "05_resized_half.png");  // untracked, different content
    check_throws([&] { repo.checkout("extra"); }, "checkout refuses to overwrite an untracked file");
    repo.checkout("extra", true);
    check(same_bytes(dir / "new.png", samples / "09_unrelated.png"), "--force overwrites it");

    check_throws([&] { repo.delete_branch("extra"); }, "cannot delete the current branch");
    repo.delete_branch("edit");
    check(repo.branches() == std::vector<std::string>({"extra", "main"}), "branch deleted");
    check(!valid_branch_name("bad name") && !valid_branch_name("..x") &&
              !valid_branch_name("HEAD") && valid_branch_name("feature/sky-2"),
          "branch name validation");
}

static void files_and_format() {
    std::cout << "Files and commit format\n";
    const fs::path dir = fresh_dir("format");
    Repository repo = Repository::init(dir);
    put(dir, "my photo.png", "00_original.png");  // space in the name
    repo.add({dir});
    repo.commit("with spaces");

    // Same pixels, different file bytes.
    put(dir, "my photo.png", "01_identical_pixels.png");
    check(repo.status().unstaged.size() == 1 && repo.status().unstaged[0].first == "metadata",
          "metadata-only change recognised (same pixels)");
    repo.add({dir});
    const Commit m = repo.read_commit(repo.commit("metadata"));
    check(m.changes.size() == 1 && m.changes[0].kind == "metadata", "committed as 'metadata'");
    check(m.files.count("my photo.png") == 1, "file names with spaces are kept");

    // Deleting a file and staging it with add on the folder.
    fs::remove(dir / "my photo.png");
    repo.add({dir});
    const Commit d = repo.read_commit(repo.commit("delete"));
    check(d.files.empty() && d.changes.size() == 1 && d.changes[0].kind == "deleted",
          "deletion staged by 'add' on the folder");

    // Serialization round trip.
    Commit c;
    c.parents = {"aa", "bb"};
    c.author = "Javi";
    c.date = "2026-10-15T21:00:00Z";
    c.edits = {"rotate 90", "crop 10 10 200 200"};
    c.files["a b/c.png"] = {"img", "file"};
    c.changes = {{"modified", "a b/c.png", {"similarity 0.996", "detected rotated 90"}}};
    c.message = "line one\nline two";
    const Commit back = Commit::parse("id", c.serialize());
    check(back.parents == c.parents && back.author == c.author && back.edits == c.edits &&
              back.files == c.files && back.changes.size() == 1 &&
              back.changes[0].details == c.changes[0].details && back.message == c.message,
          "commit text round trip (parents, edits, files, details, multi-line message)");

    check_throws([&] { repo.add({dir / ".." / "outside.png"}); }, "files outside the repository are rejected");
    check_throws([] { Repository::open(fs::path(BINARY_DIR) / "repo_tests"); },
                 "open fails outside a repository");
}

static void signing_and_verify() {
    std::cout << "Signed commits and verify\n";
    const fs::path dir = fresh_dir("verify");
    Repository repo = Repository::init(dir);
    const SigningKey javi = key_from_seed(std::array<uint8_t, 32>{1, 2, 3}, "Javi");
    const SigningKey other = key_from_seed(std::array<uint8_t, 32>{9, 9, 9}, "Other");
    repo.trust_key(javi.public_hex(), "Javi");

    put(dir, "photo.png", "00_original.png");
    repo.add({dir});
    const std::string c1 = repo.commit("original", {}, &javi);

    check_throws([&] { repo.commit("x", {parse_operation("rotate degrees=90")}, &javi); },
                 "an operation for an unchanged image is rejected (nothing to commit)");
    put(dir, "photo.png", "10_rotated_90.png");
    repo.add({dir});
    check_throws([&] { repo.commit("bad", {parse_operation("rotate degrees=45")}); },
                 "an invalid operation is rejected");
    check_throws([&] { repo.commit("bad", {parse_operation("rotate degrees=90 file=other.png")}); },
                 "an operation for an image that did not change is rejected");
    const std::string c2 = repo.commit("rotate", {parse_operation("rotate degrees=90")}, &javi);

    const Commit signed_commit = repo.read_commit(c2);
    check(signed_commit.signer_key == javi.public_hex() && signed_commit.signature.size() == 128,
          "commit carries the signer and a 64-byte signature");
    check(verify_signature(javi.public_hex(), signed_commit.signature, signed_commit.signed_payload()),
          "the signature covers the stored commit text");

    auto all_problems = [&](const std::vector<CommitReport>& rs) {
        std::size_t n = 0;
        for (const auto& r : rs) n += r.problems.size();
        return n;
    };
    auto has = [](const std::vector<std::string>& v, const std::string& text) {
        for (const auto& s : v)
            if (s.find(text) != std::string::npos) return true;
        return false;
    };

    auto reports = repo.verify();
    check(reports.size() == 2 && all_problems(reports) == 0, "clean history: no problems");
    check(has(reports[0].notes, "signed by Javi (trusted key)") &&
              has(reports[0].notes, "VERIFIED"),
          "verify: trusted signature and VERIFIED rotation");

    // A dishonest edit: claims brightness +5 but the window was painted over.
    put(dir, "photo.png", "00_original.png");
    repo.add({dir});
    repo.commit("back", {parse_operation("rotate degrees=270")}, &javi);
    put(dir, "photo.png", "03_small_region_edit.png");
    repo.add({dir});
    repo.commit("brighter", {parse_operation("brightness delta=0")}, &javi);
    reports = repo.verify();
    check(has(reports[0].problems, "MISMATCH") && has(reports[0].problems, "undeclared change"),
          "a signed but false claim is still a MISMATCH");

    // Untrusted signer.
    put(dir, "photo.png", "00_original.png");
    repo.add({dir});
    repo.commit("revert", {}, &other);
    reports = repo.verify();
    check(has(reports[0].warnings, "not trusted"), "a signature by an untrusted key is flagged");
    check(has(reports[0].warnings, "UNDECLARED"), "a change without operations is flagged");

    // Tampering with a stored commit.
    const fs::path commit_file = dir / ".ivc" / "objects" / "commits" / c2;
    std::string text;
    {
        std::ifstream in(commit_file);
        text.assign(std::istreambuf_iterator<char>(in), {});
    }
    const std::string original_text = text;
    text.replace(text.rfind("rotate"), 6, "rotatE");
    {
        std::ofstream out(commit_file, std::ios::trunc);
        out << text;
    }
    reports = repo.verify();
    const CommitReport& r2 = reports[reports.size() - 2];
    check(has(r2.problems, "altered") && has(r2.problems, "signature is INVALID"),
          "editing a stored commit breaks its ID and its signature");
    {
        std::ofstream out(commit_file, std::ios::trunc);
        out << original_text;
    }

    // Tampering with a stored file.
    const std::string fid = repo.read_commit(c1).files.at("photo.png").file_id;
    fs::copy_file(samples / "09_unrelated.png", repo.file_object(fid), fs::copy_options::overwrite_existing);
    reports = repo.verify();
    check(has(reports.back().problems, "stored file was altered"), "replacing a stored file is detected");
}

int main() {
    history_and_going_back();
    branches_and_safety();
    files_and_format();
    signing_and_verify();

    if (failures == 0) {
        std::cout << "\nAll tests passed.\n";
        return 0;
    }
    std::cout << '\n' << failures << " test(s) FAILED.\n";
    return 1;
}
