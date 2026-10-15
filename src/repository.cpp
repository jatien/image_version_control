#include "repository.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "diff.hpp"
#include "image.hpp"
#include "image_hash.hpp"
#include "orientation.hpp"

namespace {

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Writes to a temporary file first, then renames it into place, so a crash
// never leaves a half-written file behind.
void write_text(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        out << text;
    }
    fs::rename(tmp, p);
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

std::string now_utc() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string author_name() {
    for (const char* var : {"IVC_AUTHOR", "USER", "USERNAME"})
        if (const char* v = std::getenv(var); v && *v) return v;
    return "unknown";
}

std::string fmt(const char* format, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, format, v);
    return buf;
}

std::string size_text(int w, int h) { return std::to_string(w) + "x" + std::to_string(h); }

std::string box_text(const Box& b) {
    return "at " + std::to_string(b.x) + "," + std::to_string(b.y) + " size " +
           size_text(b.width, b.height);
}

bool is_hex(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) || (c >= 'a' && c <= 'f');
    });
}

}  // namespace

// ---------------------------------------------------------------- helpers

std::string file_sha256(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    Sha256 h;
    char buf[1 << 16];
    while (in) {
        in.read(buf, sizeof buf);
        h.update(reinterpret_cast<const uint8_t*>(buf), static_cast<std::size_t>(in.gcount()));
    }
    return to_hex(h.finalize());
}

bool valid_branch_name(const std::string& name) {
    if (name.empty() || name == "HEAD" || name[0] == '-' || name[0] == '.' ||
        name.back() == '/' || name.find("..") != std::string::npos ||
        name.find("//") != std::string::npos)
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' ||
               c == '-' || c == '/';
    });
}

bool Repository::is_image_path(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const std::set<std::string> exts = {".png", ".jpg", ".jpeg", ".bmp", ".gif",
                                               ".tga", ".psd", ".hdr", ".pnm", ".ppm",
                                               ".pgm"};
    return exts.count(ext) > 0;
}

// ---------------------------------------------------------------- commits

std::string Commit::serialize() const {
    std::ostringstream out;
    out << "ivc-commit 1\n";
    for (const auto& p : parents) out << "parent " << p << '\n';
    out << "author " << author << '\n';
    out << "date " << date << '\n';
    for (const auto& e : edits) out << "edit " << e << '\n';
    for (const auto& [path, f] : files)
        out << "file " << f.image_id << ' ' << f.file_id << ' ' << path << '\n';
    for (const auto& c : changes) {
        out << "change " << c.kind << ' ' << c.path << '\n';
        for (const auto& d : c.details) out << "  " << d << '\n';
    }
    out << '\n' << message;
    if (message.empty() || message.back() != '\n') out << '\n';
    return out.str();
}

Commit Commit::parse(const std::string& id, const std::string& text) {
    Commit c;
    c.id = id;
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "ivc-commit 1")
        throw std::runtime_error("commit " + id + " has an unknown format");

    while (std::getline(in, line) && !line.empty()) {
        if (starts_with(line, "  ")) {
            if (c.changes.empty()) throw std::runtime_error("commit " + id + " is corrupt");
            c.changes.back().details.push_back(line.substr(2));
            continue;
        }
        const std::size_t sp = line.find(' ');
        const std::string key = line.substr(0, sp);
        const std::string rest = sp == std::string::npos ? "" : line.substr(sp + 1);

        if (key == "parent") {
            c.parents.push_back(rest);
        } else if (key == "author") {
            c.author = rest;
        } else if (key == "date") {
            c.date = rest;
        } else if (key == "edit") {
            c.edits.push_back(rest);
        } else if (key == "file") {
            // file <image id> <file id> <path, may contain spaces>
            const std::size_t a = rest.find(' ');
            const std::size_t b = rest.find(' ', a + 1);
            if (a == std::string::npos || b == std::string::npos)
                throw std::runtime_error("commit " + id + " is corrupt");
            c.files[rest.substr(b + 1)] = {rest.substr(0, a), rest.substr(a + 1, b - a - 1)};
        } else if (key == "change") {
            const std::size_t a = rest.find(' ');
            if (a == std::string::npos) throw std::runtime_error("commit " + id + " is corrupt");
            c.changes.push_back({rest.substr(0, a), rest.substr(a + 1), {}});
        }
        // Unknown keys are ignored, so later versions can add fields.
    }

    std::ostringstream msg;
    msg << in.rdbuf();
    c.message = msg.str();
    while (!c.message.empty() && c.message.back() == '\n') c.message.pop_back();
    return c;
}

// ---------------------------------------------------------------- opening

Repository Repository::init(const fs::path& dir) {
    fs::path root = fs::absolute(dir).lexically_normal();
    if (root.filename().empty()) root = root.parent_path();  // "/a/b/" -> "/a/b"
    if (fs::exists(root / ".ivc"))
        throw std::runtime_error("there is already a repository in " + root.string());
    fs::create_directories(root / ".ivc" / "objects" / "files");
    fs::create_directories(root / ".ivc" / "objects" / "commits");
    fs::create_directories(root / ".ivc" / "refs" / "heads");
    Repository repo(root);
    repo.set_head_branch("main");
    repo.write_index({});
    return repo;
}

Repository Repository::open(const fs::path& start) {
    fs::path p = fs::absolute(start);
    while (true) {
        if (fs::is_directory(p / ".ivc")) return Repository(p);
        if (p == p.root_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    throw std::runtime_error("not inside an ivc repository (run 'ivc init' first)");
}

std::string Repository::to_repo_path(const fs::path& p) const {
    const fs::path abs = fs::weakly_canonical(fs::absolute(p));
    const fs::path rel = abs.lexically_relative(fs::weakly_canonical(root_));
    const std::string s = rel.generic_string();
    if (s.empty() || s == "." || starts_with(s, ".."))
        throw std::runtime_error(p.string() + " is outside the repository");
    if (s.find('\n') != std::string::npos)
        throw std::runtime_error("file names with line breaks are not supported");
    if (starts_with(s, ".ivc/") || s == ".ivc")
        throw std::runtime_error("cannot track files inside .ivc");
    return s;
}

fs::path Repository::file_object(const std::string& file_id) const {
    return ivc() / "objects" / "files" / file_id;
}

// ---------------------------------------------------------------- index

Snapshot Repository::read_index() const {
    Snapshot s;
    std::istringstream in(read_text(ivc() / "index"));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const std::size_t a = line.find(' ');
        const std::size_t b = line.find(' ', a + 1);
        if (a == std::string::npos || b == std::string::npos)
            throw std::runtime_error("the index is corrupt");
        s[line.substr(b + 1)] = {line.substr(0, a), line.substr(a + 1, b - a - 1)};
    }
    return s;
}

void Repository::write_index(const Snapshot& s) const {
    std::ostringstream out;
    for (const auto& [path, f] : s) out << f.image_id << ' ' << f.file_id << ' ' << path << '\n';
    write_text(ivc() / "index", out.str());
}

FileEntry Repository::store_file(const fs::path& file) const {
    FileEntry e;
    e.image_id = to_hex(exact_hash(load_image(file.string())));  // also checks it decodes
    e.file_id = file_sha256(file);
    const fs::path dst = file_object(e.file_id);
    if (!fs::exists(dst)) {
        const fs::path tmp = dst.string() + ".tmp";
        fs::copy_file(file, tmp, fs::copy_options::overwrite_existing);
        fs::rename(tmp, dst);
    }
    return e;
}

// ---------------------------------------------------------------- HEAD and refs

void Repository::set_head_branch(const std::string& name) const {
    write_text(ivc() / "HEAD", "ref: refs/heads/" + name + "\n");
}

void Repository::set_head_detached(const std::string& id) const {
    write_text(ivc() / "HEAD", id + "\n");
}

std::optional<std::string> Repository::current_branch() const {
    const std::string head = trim(read_text(ivc() / "HEAD"));
    const std::string prefix = "ref: refs/heads/";
    if (starts_with(head, prefix)) return head.substr(prefix.size());
    return std::nullopt;
}

std::optional<std::string> Repository::branch_commit(const std::string& name) const {
    const fs::path p = ivc() / "refs" / "heads" / name;
    if (!fs::is_regular_file(p)) return std::nullopt;
    return trim(read_text(p));
}

void Repository::set_branch(const std::string& name, const std::string& id) const {
    write_text(ivc() / "refs" / "heads" / name, id + "\n");
}

std::optional<std::string> Repository::head_commit() const {
    if (auto branch = current_branch()) return branch_commit(*branch);
    return trim(read_text(ivc() / "HEAD"));
}

std::vector<std::string> Repository::branches() const {
    std::vector<std::string> out;
    const fs::path heads = ivc() / "refs" / "heads";
    for (const auto& e : fs::recursive_directory_iterator(heads))
        if (e.is_regular_file() && e.path().extension() != ".tmp")
            out.push_back(e.path().lexically_relative(heads).generic_string());
    std::sort(out.begin(), out.end());
    return out;
}

void Repository::create_branch(const std::string& name, const std::string& at) {
    if (!valid_branch_name(name)) throw std::runtime_error("invalid branch name '" + name + "'");
    if (branch_commit(name)) throw std::runtime_error("branch '" + name + "' already exists");
    set_branch(name, resolve(at));
}

void Repository::delete_branch(const std::string& name) {
    if (!branch_commit(name)) throw std::runtime_error("no branch named '" + name + "'");
    if (current_branch() == name)
        throw std::runtime_error("cannot delete '" + name + "', the branch you are on");
    fs::remove(ivc() / "refs" / "heads" / name);
}

// ---------------------------------------------------------------- history

Commit Repository::read_commit(const std::string& id) const {
    return Commit::parse(id, read_text(ivc() / "objects" / "commits" / id));
}

std::string Repository::write_commit(Commit& c) const {
    const std::string text = c.serialize();
    c.id = to_hex(Sha256::hash(text));
    const fs::path p = ivc() / "objects" / "commits" / c.id;
    if (!fs::exists(p)) write_text(p, text);
    return c.id;
}

std::string Repository::resolve(const std::string& revision) const {
    // Split "<base>~<n>".
    std::string base = revision;
    int back = 0;
    if (const std::size_t t = revision.rfind('~'); t != std::string::npos) {
        const std::string n = revision.substr(t + 1);
        if (n.empty() || std::all_of(n.begin(), n.end(), ::isdigit)) {
            base = revision.substr(0, t);
            back = n.empty() ? 1 : std::stoi(n);
        }
    }

    std::string id;
    if (base == "HEAD") {
        const auto h = head_commit();
        if (!h) throw std::runtime_error("there are no commits yet");
        id = *h;
    } else if (const auto b = branch_commit(base)) {
        id = *b;
    } else if (base.size() >= 4 && is_hex(base)) {
        std::vector<std::string> matches;
        for (const auto& e : fs::directory_iterator(ivc() / "objects" / "commits")) {
            const std::string name = e.path().filename().string();
            if (starts_with(name, base) && name.size() == 64) matches.push_back(name);
        }
        if (matches.size() > 1) throw std::runtime_error("'" + base + "' is ambiguous");
        if (matches.empty()) throw std::runtime_error("unknown revision '" + base + "'");
        id = matches[0];
    } else {
        throw std::runtime_error("unknown revision '" + base + "'");
    }

    for (int i = 0; i < back; ++i) {
        const Commit c = read_commit(id);
        if (c.parents.empty())
            throw std::runtime_error("'" + revision + "' goes back past the first commit");
        id = c.parents[0];
    }
    return id;
}

std::vector<std::string> Repository::log(const std::string& revision) const {
    std::vector<std::string> ids;
    if (revision == "HEAD" && !head_commit()) return ids;
    std::string id = resolve(revision);
    while (true) {
        ids.push_back(id);
        const Commit c = read_commit(id);
        if (c.parents.empty()) break;
        id = c.parents[0];
    }
    return ids;
}

Snapshot Repository::head_snapshot() const {
    const auto h = head_commit();
    return h ? read_commit(*h).files : Snapshot{};
}

// ---------------------------------------------------------------- folder

std::vector<std::string> Repository::working_images() const {
    std::vector<std::string> out;
    auto it = fs::recursive_directory_iterator(root_);
    for (; it != fs::recursive_directory_iterator(); ++it) {
        const std::string name = it->path().filename().string();
        if (it->is_directory() && !name.empty() && name[0] == '.') {
            it.disable_recursion_pending();  // skip .ivc, .git and other hidden folders
            continue;
        }
        if (it->is_regular_file() && is_image_path(it->path()))
            out.push_back(it->path().lexically_relative(root_).generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> Repository::add(const std::vector<fs::path>& paths) {
    Snapshot index = read_index();
    std::vector<std::string> done;

    auto add_file = [&](const fs::path& file) {
        const std::string rp = to_repo_path(file);
        index[rp] = store_file(file);
        done.push_back(rp);
    };

    for (const fs::path& p : paths) {
        if (fs::is_directory(p)) {
            // Every image file under the folder, plus tracked files deleted from it.
            const fs::path abs = fs::weakly_canonical(fs::absolute(p));
            std::string prefix = abs.lexically_relative(fs::weakly_canonical(root_)).generic_string();
            if (prefix == ".") prefix.clear();
            if (starts_with(prefix, "..")) throw std::runtime_error(p.string() + " is outside the repository");
            if (!prefix.empty()) prefix += "/";

            for (const std::string& rp : working_images())
                if (starts_with(rp, prefix)) add_file(root_ / rp);
            for (auto it = index.begin(); it != index.end();) {
                if (starts_with(it->first, prefix) && !fs::exists(root_ / it->first)) {
                    done.push_back(it->first);
                    it = index.erase(it);
                } else {
                    ++it;
                }
            }
        } else if (fs::is_regular_file(p)) {
            if (!is_image_path(p)) throw std::runtime_error(p.string() + " is not an image file");
            add_file(p);
        } else {
            // A tracked file that was deleted: stage the deletion.
            const std::string rp = to_repo_path(p);
            if (!index.erase(rp)) throw std::runtime_error(p.string() + " does not exist");
            done.push_back(rp);
        }
    }
    write_index(index);
    return done;
}

std::vector<std::string> Repository::remove(const std::vector<fs::path>& paths, bool keep_file) {
    Snapshot index = read_index();
    std::vector<std::string> done;
    for (const fs::path& p : paths) {
        const std::string rp = to_repo_path(p);
        if (!index.erase(rp)) throw std::runtime_error(rp + " is not tracked");
        if (!keep_file) fs::remove(root_ / rp);
        done.push_back(rp);
    }
    write_index(index);
    return done;
}

Status Repository::status() const {
    Status st;
    const Snapshot head = head_snapshot();
    const Snapshot index = read_index();

    for (const auto& [path, f] : index) {
        const auto h = head.find(path);
        if (h == head.end())
            st.staged.push_back({"new", path});
        else if (h->second != f)
            st.staged.push_back({h->second.image_id == f.image_id ? "metadata" : "modified", path});
    }
    for (const auto& [path, f] : head)
        if (!index.count(path)) st.staged.push_back({"deleted", path});

    for (const auto& [path, f] : index) {
        const fs::path wp = root_ / path;
        if (!fs::exists(wp)) {
            st.unstaged.push_back({"deleted", path});
        } else if (file_sha256(wp) != f.file_id) {
            std::string kind = "modified";
            try {
                if (to_hex(exact_hash(load_image(wp.string()))) == f.image_id) kind = "metadata";
            } catch (const std::exception&) {
            }
            st.unstaged.push_back({kind, path});
        }
    }

    for (const std::string& path : working_images())
        if (!index.count(path)) st.untracked.push_back(path);

    std::sort(st.staged.begin(), st.staged.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    return st;
}

// ---------------------------------------------------------------- committing

std::vector<Change> Repository::describe_changes(const Snapshot& before,
                                                  const Snapshot& after) const {
    std::vector<Change> changes;
    std::set<std::string> paths;
    for (const auto& kv : before) paths.insert(kv.first);
    for (const auto& kv : after) paths.insert(kv.first);

    for (const std::string& path : paths) {
        const auto b = before.find(path), a = after.find(path);

        if (b == before.end()) {
            const Image img = load_image(file_object(a->second.file_id).string());
            changes.push_back({"added", path, {"size " + size_text(img.width, img.height)}});
            continue;
        }
        if (a == after.end()) {
            changes.push_back({"deleted", path, {}});
            continue;
        }
        if (a->second == b->second) continue;
        if (a->second.image_id == b->second.image_id) {
            changes.push_back({"metadata", path, {"same pixels, file bytes differ"}});
            continue;
        }

        const Image old_img = load_image(file_object(b->second.file_id).string());
        const Image new_img = load_image(file_object(a->second.file_id).string());
        const ImageDiff d = diff_images(old_img, new_img);
        const Comparison& c = d.comparison;

        Change ch;
        ch.path = path;
        ch.kind = c.verdict == Verdict::Different ? "replaced" : "modified";
        ch.details.push_back(fmt("similarity %.3f", c.similarity));
        if (old_img.width != new_img.width || old_img.height != new_img.height)
            ch.details.push_back("size " + size_text(old_img.width, old_img.height) + " -> " +
                                 size_text(new_img.width, new_img.height));

        if (c.verdict == Verdict::Similar) {
            // What the comparison could identify as a whole-image operation.
            if (c.orientation != Orientation::Identity)
                ch.details.push_back(std::string("detected ") + orientation_name(c.orientation));
            const Image turned = apply_orientation(new_img, inverse(c.orientation));
            if (turned.width != old_img.width || turned.height != old_img.height)
                ch.details.push_back("detected resized");

            if (d.exact_available)
                ch.details.push_back(
                    "pixels " + std::to_string(d.exact_pixels) + " changed" +
                    (d.exact_pixels > 0 ? " " + box_text(d.exact_box) : ""));
            ch.details.push_back("visible " + std::to_string(d.regions.size()) + " region(s)");
            const std::size_t shown = std::min<std::size_t>(d.regions.size(), 5);
            for (std::size_t i = 0; i < shown; ++i)
                ch.details.push_back("region " + box_text(d.regions[i].box) +
                                     fmt(" min-ssim %.2f", d.regions[i].min_ssim));
        }
        changes.push_back(ch);
    }
    return changes;
}

std::string Repository::commit(const std::string& message, const std::vector<std::string>& edits) {
    if (trim(message).empty()) throw std::runtime_error("a commit needs a message (-m \"...\")");
    for (const auto& e : edits)
        if (e.find('\n') != std::string::npos) throw std::runtime_error("edit keys must be one line");

    const auto parent = head_commit();
    const Snapshot before = parent ? read_commit(*parent).files : Snapshot{};
    const Snapshot index = read_index();
    if (index == before)
        throw std::runtime_error("nothing to commit (stage changes with 'ivc add')");

    Commit c;
    if (parent) c.parents.push_back(*parent);
    c.author = author_name();
    c.date = now_utc();
    c.edits = edits;
    c.files = index;
    c.changes = describe_changes(before, index);
    c.message = trim(message);
    const std::string id = write_commit(c);

    if (auto branch = current_branch())
        set_branch(*branch, id);
    else
        set_head_detached(id);
    return id;
}

// ---------------------------------------------------------------- moving around

void Repository::checkout(const std::string& revision, bool force) {
    const auto branch = branch_commit(revision);
    const std::string target_id = branch ? *branch : resolve(revision);

    if (!force) {
        const Status st = status();
        if (!st.clean())
            throw std::runtime_error(
                "you have uncommitted changes; commit them first, or use --force to discard them");
    }

    const Snapshot current = read_index();
    const Snapshot target = read_commit(target_id).files;

    // Never silently overwrite an untracked file that has different content.
    if (!force)
        for (const auto& [path, f] : target)
            if (!current.count(path) && fs::exists(root_ / path) &&
                file_sha256(root_ / path) != f.file_id)
                throw std::runtime_error("checking out would overwrite the untracked file " +
                                         path + "; move it away or use --force");

    for (const auto& [path, f] : current)
        if (!target.count(path)) fs::remove(root_ / path);

    for (const auto& [path, f] : target) {
        const fs::path wp = root_ / path;
        if (fs::exists(wp) && file_sha256(wp) == f.file_id) continue;
        fs::create_directories(wp.parent_path());
        fs::copy_file(file_object(f.file_id), wp, fs::copy_options::overwrite_existing);
    }

    write_index(target);
    if (branch)
        set_head_branch(revision);
    else
        set_head_detached(target_id);
}

void Repository::checkout_new_branch(const std::string& name) {
    if (!valid_branch_name(name)) throw std::runtime_error("invalid branch name '" + name + "'");
    if (branch_commit(name)) throw std::runtime_error("branch '" + name + "' already exists");
    // The new branch starts at the current commit, so no file changes;
    // uncommitted changes simply come along.
    if (const auto h = head_commit()) set_branch(name, *h);
    set_head_branch(name);
}

void Repository::restore(const fs::path& path, const std::string& revision) {
    const std::string rp = to_repo_path(path);
    const Commit c = read_commit(resolve(revision));
    const auto it = c.files.find(rp);
    if (it == c.files.end())
        throw std::runtime_error(rp + " does not exist in " + revision);
    const fs::path wp = root_ / rp;
    fs::create_directories(wp.parent_path());
    fs::copy_file(file_object(it->second.file_id), wp, fs::copy_options::overwrite_existing);
}
