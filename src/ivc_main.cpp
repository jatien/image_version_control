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
#include "repository.hpp"

namespace {

const char* kHelp = R"(ivc - version control for images

Start
  ivc init [folder]                 create a repository (default: here)

Record changes
  ivc status                        what changed since the last commit
  ivc add <file|folder>...          stage image files for the next commit
  ivc rm [--keep] <file>...         stop tracking files (--keep: leave them on disk)
  ivc commit -m "message" [--edit "<tool> <params>"]...
                                    record the staged files; --edit records
                                    which editor tool made the change

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
        if (d.rfind("region ", 0) == 0) continue;
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
    std::cout << "author  " << c.author << "\ndate    " << c.date << "\n\n    " << c.message
              << "\n\n";
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

int cmd_commit(Repository& repo, const std::vector<std::string>& args) {
    std::string message;
    std::vector<std::string> edits;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "-m" || args[i] == "--message") && i + 1 < args.size())
            message = args[++i];
        else if (args[i] == "--edit" && i + 1 < args.size())
            edits.push_back(args[++i]);
        else
            throw std::runtime_error("usage: ivc commit -m \"message\" [--edit \"<tool> <params>\"]...");
    }
    const std::string id = repo.commit(message, edits);
    const Commit c = repo.read_commit(id);
    const auto branch = repo.current_branch();
    std::cout << '[' << (branch ? *branch : "detached") << ' ' << short_id(id) << "] "
              << c.message << '\n';
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

        std::cerr << "ivc: unknown command '" << cmd << "' (see 'ivc help')\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "ivc: " << e.what() << '\n';
        return 1;
    }
}
