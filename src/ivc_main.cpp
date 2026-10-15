// ivc: version control for images, used like git.
// Run "ivc help" for the list of commands.

#include <cstring>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "diff.hpp"
#include "image.hpp"
#include "orientation.hpp"
#include <fstream>

#include "operations.hpp"
#include "repository.hpp"
#include "signing.hpp"

namespace {

const char* kHelp = R"(ivc - version control for images

Start
  ivc init [folder]                 create a repository (default: here)

Record changes
  ivc status                        what changed since the last commit
  ivc add <file|folder>...          stage image files for the next commit
  ivc rm [--keep] <file>...         stop tracking files (--keep: leave them on disk)
  ivc commit -m "message" [--op "<operation>"]... [--ops-file <file>]
             [--tool "<editor and version>"] [--no-sign]
                                    record the staged files, with the edit
                                    operations that produced them (see below);
                                    signed automatically if you have a key

History and differences
  ivc log [revision]                list commits, newest first
  ivc show [revision]               one commit in full: files and changes
  ivc diff <file> [rev [rev]] [-o diff.png]
                                    how a file differs: last commit vs folder,
                                    a commit vs folder, or two commits

Go back and forth
  ivc checkout <branch|revision>    move to a branch or to any older commit
  ivc checkout -b <new-branch>      create a branch here and switch to it
  ivc checkout --force ...          discard uncommitted changes while moving
  ivc restore <file> [--from rev]   bring back one file from a commit
  ivc branch                        list branches
  ivc branch <name> [revision]      create a branch
  ivc branch -d <name>              delete a branch

Edit operations and verification
  ivc operations                    list the known operations
  ivc verify [--strict] [revision]  check the history: commits and files not
                                    altered, signatures, and every declared
                                    operation against the pixels; --strict also
                                    fails unsigned or untrusted commits
  ivc key generate "<name>"         create your signing key (Ed25519)
  ivc key show                      print your public key
  ivc trust <public-key> "<name>"   trust a key's signatures in this repository
  ivc trust --me                    trust your own key
  ivc trust                         list trusted keys

An operation is an action and key=value parameters, e.g.
  --op "rotate degrees=90"   --op "filter name=sharpen region=10,10,64,64"
add file=<path> when more than one image changed. An ops file has one
operation per line ('#' comments; a line "tool <name>" sets the tool for
the lines after it).

Revisions: a branch name, a commit ID or its first 4+ characters, HEAD
(the current commit), and <revision>~<n> for n commits back, e.g. HEAD~2.
)";

std::string short_id(const std::string& id) { return id.substr(0, 8); }

// Branch names (and HEAD) pointing at each commit, for log decorations.
std::map<std::string, std::vector<std::string>> labels(const Repository& repo) {
    std::map<std::string, std::vector<std::string>> out;
    const auto current = repo.current_branch();
    for (const std::string& b : repo.branches()) {
        const std::string id = repo.resolve(b);
        out[id].push_back(current == b ? "HEAD -> " + b : b);
    }
    if (!current)
        if (const auto h = repo.head_commit()) out[*h].insert(out[*h].begin(), "HEAD");
    return out;
}

void print_change_summary(const Change& c) {
    std::cout << "    " << c.kind << "  " << c.path;
    // Only the one-line facts; regions are listed by "ivc show".
    bool first = true;
    for (const std::string& d : c.details) {
        if (d.rfind("region ", 0) == 0 || d.rfind("check-detail ", 0) == 0) continue;
        std::cout << (first ? "   (" : ", ") << d;
        first = false;
    }
    if (!first) std::cout << ')';
    std::cout << '\n';
}

void print_commit_header(const Repository& repo, const Commit& c,
                         const std::map<std::string, std::vector<std::string>>& lab) {
    std::cout << "commit " << short_id(c.id);
    if (auto it = lab.find(c.id); it != lab.end()) {
        std::cout << " (";
        for (std::size_t i = 0; i < it->second.size(); ++i)
            std::cout << (i ? ", " : "") << it->second[i];
        std::cout << ')';
    }
    std::cout << '\n';
    if (c.parents.size() > 1) {
        std::cout << "merge  ";
        for (const auto& p : c.parents) std::cout << ' ' << short_id(p);
        std::cout << '\n';
    }
    std::cout << "author  " << c.author << "\ndate    " << c.date << '\n';
    if (!c.signer_key.empty())
        std::cout << "signed  " << c.signer_name << " (" << c.signer_key.substr(0, 16) << "...)\n";
    std::cout << "\n    " << c.message << "\n\n";
    (void)repo;
}

int cmd_init(const std::vector<std::string>& args) {
    const Repository repo = Repository::init(args.empty() ? fs::path(".") : fs::path(args[0]));
    std::cout << "Created an empty repository in " << repo.root().string()
              << "\nNext: ivc add <images>, then ivc commit -m \"first version\"\n";
    return 0;
}

int cmd_add(Repository& repo, const std::vector<std::string>& args) {
    if (args.empty()) throw std::runtime_error("usage: ivc add <file|folder>...");
    std::vector<fs::path> paths(args.begin(), args.end());
    const auto done = repo.add(paths);
    for (const auto& p : done) std::cout << "staged  " << p << '\n';
    if (done.empty()) std::cout << "no image files found\n";
    return 0;
}

int cmd_rm(Repository& repo, std::vector<std::string> args) {
    bool keep = false;
    std::vector<fs::path> paths;
    for (const auto& a : args) {
        if (a == "--keep") keep = true;
        else paths.emplace_back(a);
    }
    if (paths.empty()) throw std::runtime_error("usage: ivc rm [--keep] <file>...");
    for (const auto& p : repo.remove(paths, keep))
        std::cout << "removed " << p << (keep ? " (file kept on disk)" : "") << '\n';
    return 0;
}

int cmd_status(Repository& repo) {
    const auto branch = repo.current_branch();
    const auto head = repo.head_commit();
    if (branch)
        std::cout << "On branch " << *branch << (head ? "" : " (no commits yet)") << '\n';
    else
        std::cout << "Detached at commit " << short_id(*head)
                  << " (looking at an older version; 'ivc checkout -b <name>' to keep working here)\n";

    const Status st = repo.status();
    if (!st.staged.empty()) {
        std::cout << "\nStaged for the next commit:\n";
        for (const auto& [k, p] : st.staged) std::cout << "    " << k << "  " << p << '\n';
    }
    if (!st.unstaged.empty()) {
        std::cout << "\nChanged but not staged (use 'ivc add'):\n";
        for (const auto& [k, p] : st.unstaged) std::cout << "    " << k << "  " << p << '\n';
    }
    if (!st.untracked.empty()) {
        std::cout << "\nUntracked images (use 'ivc add' to track):\n";
        for (const auto& p : st.untracked) std::cout << "    " << p << '\n';
    }
    if (st.clean() && st.untracked.empty()) std::cout << "\nNothing changed.\n";
    return 0;
}

void print_operation(const Operation& op) {
    std::cout << "    op  " << op.canonical() << "   (" << op.file;
    if (!op.tool.empty()) std::cout << ", " << op.tool;
    std::cout << ")\n";
}

std::vector<Operation> read_ops_file(const std::string& path, const std::string& default_tool) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::vector<Operation> ops;
    std::string line, tool = default_tool;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        line = line.substr(first);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("tool ", 0) == 0) {
            tool = line.substr(5);
            continue;
        }
        try {
            Operation op = parse_operation(line);
            op.tool = tool;
            ops.push_back(op);
        } catch (const std::invalid_argument& e) {
            throw std::runtime_error(path + ":" + std::to_string(n) + ": " + e.what());
        }
    }
    return ops;
}

int cmd_commit(Repository& repo, const std::vector<std::string>& args) {
    std::string message, tool;
    std::vector<std::string> op_texts, ops_files;
    bool sign = true;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "-m" || args[i] == "--message") && i + 1 < args.size())
            message = args[++i];
        else if (args[i] == "--op" && i + 1 < args.size())
            op_texts.push_back(args[++i]);
        else if (args[i] == "--ops-file" && i + 1 < args.size())
            ops_files.push_back(args[++i]);
        else if (args[i] == "--tool" && i + 1 < args.size())
            tool = args[++i];
        else if (args[i] == "--no-sign")
            sign = false;
        else
            throw std::runtime_error(
                "usage: ivc commit -m \"message\" [--op \"<operation>\"]... [--ops-file <file>] "
                "[--tool \"<editor>\"] [--no-sign]");
    }

    std::vector<Operation> ops;
    for (const auto& t : op_texts) {
        try {
            Operation op = parse_operation(t);
            op.tool = tool;
            ops.push_back(op);
        } catch (const std::invalid_argument& e) {
            throw std::runtime_error(std::string("--op \"") + t + "\": " + e.what());
        }
    }
    for (const auto& f : ops_files)
        for (Operation& op : read_ops_file(f, tool)) ops.push_back(op);
    // file= is written relative to where you are; the repository wants it from its root.
    for (Operation& op : ops)
        if (!op.file.empty()) op.file = repo.to_repo_path(op.file);

    std::optional<SigningKey> key;
    if (sign) key = load_signing_key();

    const std::string id = repo.commit(message, ops, key ? &*key : nullptr);
    const Commit c = repo.read_commit(id);
    const auto branch = repo.current_branch();
    std::cout << '[' << (branch ? *branch : "detached") << ' ' << short_id(id) << "] "
              << c.message << (c.signature.empty() ? "  (not signed)" : "  (signed)") << '\n';
    for (const Operation& op : c.operations) print_operation(op);
    for (const Change& ch : c.changes) print_change_summary(ch);
    return 0;
}

int cmd_log(Repository& repo, const std::vector<std::string>& args) {
    const auto ids = repo.log(args.empty() ? "HEAD" : args[0]);
    if (ids.empty()) {
        std::cout << "No commits yet.\n";
        return 0;
    }
    const auto lab = labels(repo);
    for (const std::string& id : ids) {
        const Commit c = repo.read_commit(id);
        print_commit_header(repo, c, lab);
        for (const auto& e : c.edits) std::cout << "    edit  " << e << '\n';
        for (const Operation& op : c.operations) print_operation(op);
        for (const Change& ch : c.changes) print_change_summary(ch);
        std::cout << '\n';
    }
    return 0;
}

int cmd_show(Repository& repo, const std::vector<std::string>& args) {
    const Commit c = repo.read_commit(repo.resolve(args.empty() ? "HEAD" : args[0]));
    print_commit_header(repo, c, labels(repo));
    std::cout << "id      " << c.id << '\n';
    for (const auto& p : c.parents) std::cout << "parent  " << p << '\n';
    if (!c.edits.empty()) {
        std::cout << "\nEdits recorded by the editor:\n";
        for (const auto& e : c.edits) std::cout << "    " << e << '\n';
    }
    if (!c.operations.empty()) {
        std::cout << "\nDeclared operations:\n";
        for (const Operation& op : c.operations) {
            std::cout << "    " << op.canonical() << "   file " << op.file << '\n';
            if (!op.tool.empty()) std::cout << "        tool " << op.tool << '\n';
            if (const ActionSpec* spec = find_action(op.action)) {
                if (!spec->c2pa.empty()) std::cout << "        c2pa " << spec->c2pa << '\n';
                std::cout << "        " << (spec->replayable ? "replayable" : "declared only") << '\n';
            } else {
                std::cout << "        unknown action, declared only\n";
            }
            std::cout << "        id " << op.id() << '\n';
        }
    }
    std::cout << "\nChanges:\n";
    for (const Change& ch : c.changes) {
        std::cout << "    " << ch.kind << "  " << ch.path << '\n';
        for (const auto& d : ch.details) std::cout << "        " << d << '\n';
    }
    std::cout << "\nFiles in this version:\n";
    for (const auto& [path, f] : c.files)
        std::cout << "    " << short_id(f.image_id) << "  " << path << '\n';
    return 0;
}

int cmd_diff(Repository& repo, const std::vector<std::string>& args) {
    std::string out_path;
    std::vector<std::string> pos;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-o" && i + 1 < args.size()) out_path = args[++i];
        else pos.push_back(args[i]);
    }
    if (pos.empty() || pos.size() > 3)
        throw std::runtime_error("usage: ivc diff <file> [rev [rev]] [-o diff.png]");

    const std::string rp = repo.to_repo_path(pos[0]);
    auto from_commit = [&](const std::string& rev) {
        const Commit c = repo.read_commit(repo.resolve(rev));
        const auto it = c.files.find(rp);
        if (it == c.files.end()) throw std::runtime_error(rp + " does not exist in " + rev);
        return std::make_pair(load_image(repo.file_object(it->second.file_id).string()),
                              rev + " (" + short_id(c.id) + ")");
    };

    const auto [before, before_name] = from_commit(pos.size() >= 2 ? pos[1] : "HEAD");
    std::pair<Image, std::string> after;
    if (pos.size() == 3) after = from_commit(pos[2]);
    else after = {load_image((repo.root() / rp).string()), "folder"};

    const ImageDiff d = diff_images(before, after.first);
    const Comparison& c = d.comparison;
    std::cout << rp << ":  " << before_name << "  ->  " << after.second << "\n\n"
              << "verdict    " << verdict_name(c.verdict);
    if (c.verdict == Verdict::Similar && c.orientation != Orientation::Identity)
        std::cout << " (" << orientation_name(c.orientation) << ")";
    std::printf("   similarity %.3f\n", c.similarity);

    if (c.verdict == Verdict::Identical) {
        std::cout << "No differences.\n";
    } else if (c.verdict == Verdict::Different) {
        std::cout << "The image was replaced by a different one.\n";
    } else {
        if (d.exact_available)
            std::cout << "exact      " << d.exact_pixels << " pixel(s) changed\n";
        std::printf("visible    mean SSIM %.3f, %zu changed region(s)\n", d.mean_ssim,
                    d.regions.size());
        for (std::size_t i = 0; i < d.regions.size() && i < 10; ++i) {
            const Box& b = d.regions[i].box;
            std::printf("  #%zu  at %d,%d  size %dx%d  min SSIM %.2f\n", i + 1, b.x, b.y,
                        b.width, b.height, d.regions[i].min_ssim);
        }
    }
    if (!out_path.empty()) {
        write_png(out_path, render_diff(before, d));
        std::cout << "\ndifference image written to " << out_path << '\n';
    }
    return 0;
}

int cmd_checkout(Repository& repo, const std::vector<std::string>& args) {
    bool force = false, create = false;
    std::string target;
    for (const auto& a : args) {
        if (a == "--force" || a == "-f") force = true;
        else if (a == "-b") create = true;
        else if (target.empty()) target = a;
        else throw std::runtime_error("usage: ivc checkout [-b] [--force] <branch|revision>");
    }
    if (target.empty()) throw std::runtime_error("usage: ivc checkout [-b] [--force] <branch|revision>");

    if (create) {
        repo.checkout_new_branch(target);
        std::cout << "Switched to a new branch '" << target << "'\n";
        return 0;
    }
    repo.checkout(target, force);
    if (const auto b = repo.current_branch(); b && *b == target) {
        std::cout << "Switched to branch '" << target << "'\n";
    } else {
        const Commit c = repo.read_commit(*repo.head_commit());
        std::cout << "Now at commit " << short_id(c.id) << ": " << c.message << '\n'
                  << "You are looking at an older version (detached). To go back to the latest,\n"
                  << "run 'ivc checkout <branch>'; to keep working from here, 'ivc checkout -b <name>'.\n";
    }
    return 0;
}

int cmd_branch(Repository& repo, const std::vector<std::string>& args) {
    if (args.empty()) {
        const auto current = repo.current_branch();
        const auto all = repo.branches();
        if (all.empty()) std::cout << "No branches yet (they appear after the first commit).\n";
        for (const auto& b : all) {
            const Commit c = repo.read_commit(repo.resolve(b));
            std::cout << (current == b ? "* " : "  ") << b << "  " << short_id(c.id) << "  "
                      << c.message << '\n';
        }
        return 0;
    }
    if (args[0] == "-d") {
        if (args.size() != 2) throw std::runtime_error("usage: ivc branch -d <name>");
        repo.delete_branch(args[1]);
        std::cout << "Deleted branch " << args[1] << '\n';
        return 0;
    }
    repo.create_branch(args[0], args.size() > 1 ? args[1] : "HEAD");
    std::cout << "Created branch " << args[0] << " at " << short_id(repo.resolve(args[0]))
              << '\n';
    return 0;
}

int cmd_restore(Repository& repo, const std::vector<std::string>& args) {
    std::string rev = "HEAD";
    std::vector<std::string> files;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--from" && i + 1 < args.size()) rev = args[++i];
        else files.push_back(args[i]);
    }
    if (files.empty()) throw std::runtime_error("usage: ivc restore <file>... [--from revision]");
    for (const auto& f : files) {
        repo.restore(f, rev);
        std::cout << "restored " << repo.to_repo_path(f) << " from " << rev << '\n';
    }
    return 0;
}

int cmd_operations() {
    std::cout << "Known operations (others are allowed and recorded as declared only):\n\n";
    for (const ActionSpec& a : known_actions()) {
        std::cout << "  " << a.name << (a.replayable ? "   [replayable]" : "   [declared only]");
        if (!a.c2pa.empty()) std::cout << "   " << a.c2pa;
        std::cout << "\n      " << a.help << "\n";
    }
    std::cout << "\nAny operation may add region=x,y,width,height to say where it acted.\n";
    return 0;
}

int cmd_verify(Repository& repo, const std::vector<std::string>& args) {
    bool strict = false;
    std::string rev = "HEAD";
    for (const auto& a : args) {
        if (a == "--strict") strict = true;
        else rev = a;
    }
    auto reports = repo.verify(rev);
    if (strict) {
        // Unsigned or untrusted commits count as problems: otherwise someone
        // rewriting history could simply drop the signatures.
        for (CommitReport& r : reports) {
            std::vector<std::string> kept;
            for (const auto& w : r.warnings) {
                if (w == "not signed" || w.rfind("validly signed by", 0) == 0)
                    r.problems.push_back(w + " (--strict)");
                else
                    kept.push_back(w);
            }
            r.warnings = kept;
        }
    }
    if (reports.empty()) {
        std::cout << "No commits yet.\n";
        return 0;
    }
    int problems = 0, warnings = 0;
    for (const CommitReport& r : reports) {
        std::string message;
        try {
            message = repo.read_commit(r.id).message;
        } catch (const std::exception&) {
        }
        const char* mark = !r.problems.empty() ? "FAIL" : !r.warnings.empty() ? "warn" : " ok ";
        std::cout << "[" << mark << "] " << short_id(r.id) << "  " << message << '\n';
        for (const auto& p : r.problems) std::cout << "         PROBLEM  " << p << '\n';
        for (const auto& w : r.warnings) std::cout << "         warning  " << w << '\n';
        for (const auto& n : r.notes) std::cout << "         ok       " << n << '\n';
        problems += static_cast<int>(r.problems.size());
        warnings += static_cast<int>(r.warnings.size());
    }
    std::cout << '\n' << reports.size() << " commit(s) checked: " << problems << " problem(s), "
              << warnings << " warning(s)\n";
    return problems > 0 ? 1 : 0;
}

int cmd_key(const std::vector<std::string>& args) {
    if (!args.empty() && args[0] == "generate") {
        if (args.size() < 2) throw std::runtime_error("usage: ivc key generate \"<your name>\" [--force]");
        const bool force = args.size() > 2 && args[2] == "--force";
        const SigningKey k = generate_signing_key(args[1], force);
        std::cout << "Created a signing key for " << k.name << "\n  private key: "
                  << (key_directory() / "signing_key").string()
                  << "   (keep it secret, never commit it)\n  public key:  " << k.public_hex()
                  << "\nYour commits are now signed. In each repository, run 'ivc trust --me'\n"
                  << "and give the public key to anyone who should verify your work.\n";
        return 0;
    }
    if (!args.empty() && args[0] == "show") {
        const auto k = load_signing_key();
        if (!k) throw std::runtime_error("no signing key yet (ivc key generate \"<your name>\")");
        std::cout << k->public_hex() << "  " << k->name << '\n';
        return 0;
    }
    throw std::runtime_error("usage: ivc key generate \"<name>\" | ivc key show");
}

int cmd_trust(Repository& repo, const std::vector<std::string>& args) {
    if (args.empty()) {
        const auto keys = repo.trusted_keys();
        if (keys.empty()) std::cout << "No trusted keys in this repository.\n";
        for (const auto& [k, n] : keys) std::cout << k << "  " << n << '\n';
        return 0;
    }
    if (args[0] == "--me") {
        const auto k = load_signing_key();
        if (!k) throw std::runtime_error("no signing key yet (ivc key generate \"<your name>\")");
        repo.trust_key(k->public_hex(), k->name);
        std::cout << "Trusting " << k->name << " (" << k->public_hex().substr(0, 16) << "...)\n";
        return 0;
    }
    if (args.size() != 2) throw std::runtime_error("usage: ivc trust <public-key> \"<name>\"");
    repo.trust_key(args[0], args[1]);
    std::cout << "Trusting " << args[1] << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "help") == 0 || std::strcmp(argv[1], "--help") == 0 ||
        std::strcmp(argv[1], "-h") == 0) {
        std::cout << kHelp;
        return argc < 2 ? 1 : 0;
    }
    const std::string cmd = argv[1];
    const std::vector<std::string> args(argv + 2, argv + argc);

    try {
        if (cmd == "init") return cmd_init(args);
        if (cmd == "key") return cmd_key(args);
        if (cmd == "operations") return cmd_operations();

        Repository repo = Repository::open();
        if (cmd == "add") return cmd_add(repo, args);
        if (cmd == "rm") return cmd_rm(repo, args);
        if (cmd == "status") return cmd_status(repo);
        if (cmd == "commit") return cmd_commit(repo, args);
        if (cmd == "log") return cmd_log(repo, args);
        if (cmd == "show") return cmd_show(repo, args);
        if (cmd == "diff") return cmd_diff(repo, args);
        if (cmd == "checkout") return cmd_checkout(repo, args);
        if (cmd == "branch") return cmd_branch(repo, args);
        if (cmd == "restore") return cmd_restore(repo, args);
        if (cmd == "verify") return cmd_verify(repo, args);
        if (cmd == "trust") return cmd_trust(repo, args);

        std::cerr << "ivc: unknown command '" << cmd << "' (see 'ivc help')\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "ivc: " << e.what() << '\n';
        return 1;
    }
}
