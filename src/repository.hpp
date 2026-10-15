#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

// A git-like repository for images.
//
// On disk, everything lives in a hidden .ivc/ folder at the repository root:
//
//   .ivc/HEAD                    "ref: refs/heads/main" (on a branch) or a
//                                commit ID (detached: looking at an old version)
//   .ivc/refs/heads/<branch>     the commit ID at the tip of each branch
//   .ivc/index                   the staging area: the files the next commit
//                                will contain, one per line
//   .ivc/objects/files/<id>      the original bytes of every stored file,
//                                named by the SHA-256 of those bytes
//   .ivc/objects/commits/<id>    every commit, a readable text file named by
//                                the SHA-256 of its own text
//
// Two IDs per image file:
//   file ID   SHA-256 of the file's bytes: used to store and restore it exactly
//   image ID  SHA-256 of its decoded pixels (exact_hash): the image's identity,
//             the same whatever the file format or metadata
//
// Commit format (see Commit::serialize):
//
//   ivc-commit 1
//   parent <commit id>              zero (first commit) or more
//   author <name>
//   date <YYYY-MM-DDTHH:MM:SSZ>
//   edit <tool key> <parameters>    zero or more, written by an editor
//   file <image id> <file id> <path>
//   change <kind> <path>            added / modified / replaced / metadata / deleted
//     <detail>                      indented: similarity, detected rotation...
//
//   <message>

namespace fs = std::filesystem;

struct FileEntry {
    std::string image_id;  // SHA-256 of the pixels, hex
    std::string file_id;   // SHA-256 of the file bytes, hex

    bool operator==(const FileEntry& o) const {
        return image_id == o.image_id && file_id == o.file_id;
    }
    bool operator!=(const FileEntry& o) const { return !(*this == o); }
};

// The tracked files at one moment: path (relative, with '/') -> entry.
using Snapshot = std::map<std::string, FileEntry>;

// What happened to one file in a commit.
struct Change {
    std::string kind;   // added, modified, replaced, metadata, deleted
    std::string path;
    std::vector<std::string> details;  // e.g. "similarity 0.996", "detected rotated 90"
};

struct Commit {
    std::string id;                     // filled in when written or read
    std::vector<std::string> parents;
    std::string author;
    std::string date;
    std::vector<std::string> edits;     // "<tool key> <parameters>"
    Snapshot files;
    std::vector<Change> changes;
    std::string message;

    std::string serialize() const;      // the text that is stored and hashed
    static Commit parse(const std::string& id, const std::string& text);
};

struct Status {
    // Staged for the next commit (index vs last commit): kind, path.
    std::vector<std::pair<std::string, std::string>> staged;
    // Changed in the folder but not staged (folder vs index): kind, path.
    std::vector<std::pair<std::string, std::string>> unstaged;
    // Image files in the folder that are not tracked.
    std::vector<std::string> untracked;

    bool clean() const { return staged.empty() && unstaged.empty(); }
};

class Repository {
public:
    // Creates .ivc/ in `dir`. Throws if a repository already exists there.
    static Repository init(const fs::path& dir);
    // Finds the repository containing `start` (searching upwards).
    static Repository open(const fs::path& start = fs::current_path());

    const fs::path& root() const { return root_; }

    // ---- everyday operations ------------------------------------------
    // Stages files (or every image file in a folder) for the next commit.
    std::vector<std::string> add(const std::vector<fs::path>& paths);
    // Stops tracking files; deletes them from the folder unless keep_file.
    std::vector<std::string> remove(const std::vector<fs::path>& paths, bool keep_file);
    Status status() const;
    // Records the staged files as a new commit and moves the branch to it.
    // Throws if nothing changed since the last commit.
    std::string commit(const std::string& message, const std::vector<std::string>& edits = {});
    // Moves to a branch or to any commit (detached), updating the folder.
    // Refuses if there are uncommitted changes, unless force.
    void checkout(const std::string& revision, bool force = false);
    // Creates a branch at the current commit and switches to it (works in an
    // empty repository too). Files and uncommitted changes stay as they are.
    void checkout_new_branch(const std::string& name);
    // Writes one file's content from a commit into the folder (HEAD by default).
    void restore(const fs::path& path, const std::string& revision = "HEAD");

    // ---- branches --------------------------------------------------------
    std::vector<std::string> branches() const;
    std::optional<std::string> current_branch() const;  // empty when detached
    void create_branch(const std::string& name, const std::string& at = "HEAD");
    void delete_branch(const std::string& name);

    // ---- history ---------------------------------------------------------
    std::optional<std::string> head_commit() const;     // empty before the first commit
    // Commit IDs from `revision` back to the first commit, following first parents.
    std::vector<std::string> log(const std::string& revision = "HEAD") const;
    Commit read_commit(const std::string& id) const;
    // Accepts a branch name, a commit ID or a unique prefix of one (at least
    // 4 characters), HEAD, and <revision>~<n> (n commits back).
    std::string resolve(const std::string& revision) const;

    // ---- objects and paths ---------------------------------------------
    Snapshot read_index() const;
    fs::path file_object(const std::string& file_id) const;
    // Repository-relative path with '/' separators. Throws if outside.
    std::string to_repo_path(const fs::path& p) const;
    static bool is_image_path(const fs::path& p);

private:
    explicit Repository(fs::path root) : root_(std::move(root)) {}

    fs::path ivc() const { return root_ / ".ivc"; }
    void write_index(const Snapshot& s) const;
    FileEntry store_file(const fs::path& file) const;   // copies bytes into objects
    std::string write_commit(Commit& c) const;
    Snapshot head_snapshot() const;
    std::vector<std::string> working_images() const;     // all image files in the folder
    void set_head_branch(const std::string& name) const;
    void set_head_detached(const std::string& id) const;
    std::optional<std::string> branch_commit(const std::string& name) const;
    void set_branch(const std::string& name, const std::string& id) const;
    std::vector<Change> describe_changes(const Snapshot& before, const Snapshot& after) const;

    fs::path root_;
};

// SHA-256 of a file's bytes, hex.
std::string file_sha256(const fs::path& p);
// Valid branch name: letters, digits, '.', '_', '-', '/', not starting with
// '-' or '.', no "..", not "HEAD".
bool valid_branch_name(const std::string& name);
