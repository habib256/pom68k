// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// See GuiRomWindow.h. Strings drawn here stay ASCII: ImGui's default font
// has no em-dash or arrows (gui_windows_test scans this file), and the
// library's own sentences pass through `plain()` for the same reason.

#include "GuiRomWindow.h"

#include "GuiSessionState.h"
#include "MachineFactory.h"
#include "SessionFile.h"
#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>

namespace pom68k::gui {

namespace fs = std::filesystem;

// ── The worker ─────────────────────────────────────────────────────────────

struct RomJob {
    RomJobKind kind = RomJobKind::Scan;
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    mutable std::mutex mutex;
    roms::FetchProgress progress;   // guarded by `mutex`
    // Written by the worker before `finished`, read after the join.
    bool scanned = false;
    std::vector<roms::RomStatus> rows;
    roms::RomDestination destination;
    std::optional<roms::FetchResult> fetched;
    std::optional<roms::ImportResult> imported;
    std::string error;              // an exception the library let through
    std::thread thread;

    ~RomJob() {
        cancel.store(true);
        if (thread.joinable()) thread.join();
    }
};

GuiRomState::GuiRomState() = default;
GuiRomState::~GuiRomState() = default;   // RomJob's destructor cancels and joins

namespace {

// One worker at a time. `work` runs on the worker; every job ends with a
// scan unless it was cancelled, so the table follows what was installed.
template <class Work>
bool startJob(GuiRomState& s, RomJobKind kind, Work work) {
    if (s.job) return false;
    auto job = std::make_unique<RomJob>();
    job->kind = kind;
    RomJob* j = job.get();
    auto body = [j, work = std::move(work), scan = s.scanner, search = s.search]() mutable {
        try {
            work(*j);
            if (!j->cancel.load()) {
                j->rows = scan ? scan(search) : roms::scanRoms(search);
                j->destination = roms::writableRomsDir(search);
                j->scanned = true;
            }
        } catch (const std::exception& e) {
            j->error = e.what();
        }
        j->finished.store(true);
    };
#ifdef __EMSCRIPTEN__
    body();   // no threads in the browser build; its file system is in memory
#else
    j->thread = std::thread(std::move(body));
#endif
    s.job = std::move(job);
    return true;
}

// The library writes sentences with typographic punctuation the default
// font cannot draw.
std::string plain(std::string s) {
    static constexpr std::pair<std::string_view, std::string_view> kSwap[] = {
        {"\xE2\x80\x94", "-"},   {"\xE2\x80\x93", "-"},   {"\xE2\x80\xA6", "..."},
        {"\xE2\x86\x92", "->"},  {"\xC2\xAB ", "\""},     {" \xC2\xBB", "\""},
        {"\xE2\x80\x98", "'"},   {"\xE2\x80\x99", "'"},
    };
    for (const auto& [from, to] : kSwap)
        for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
            s.replace(at, from.size(), to);
    return s;
}

std::vector<std::string> profileIds(SnapMachine m) {
    std::vector<std::string> ids;
    if (const MachineProfile* p = machineProfile(m))
        for (const roms::RomDump* d : roms::romDumpsForProfile(p->slug)) ids.emplace_back(d->id);
    return ids;
}

// ── Drawing helpers ────────────────────────────────────────────────────────

constexpr ImVec4 kGreen{0.30f, 0.85f, 0.35f, 1.0f};
constexpr ImVec4 kOrange{0.95f, 0.75f, 0.30f, 1.0f};
constexpr ImVec4 kRed{0.95f, 0.40f, 0.35f, 1.0f};
constexpr ImVec4 kGrey{0.62f, 0.62f, 0.62f, 1.0f};
constexpr ImVec4 kHeading{0.75f, 0.65f, 0.95f, 1.0f};

ImVec4 stateColour(roms::RomState s) {
    switch (s) {
    case roms::RomState::Verified: return kGreen;
    case roms::RomState::Shadowed: return kOrange;
    case roms::RomState::Mismatch: return kRed;
    case roms::RomState::Missing: return kGrey;
    }
    return kGrey;
}

const char* stateText(roms::RomState s) {
    switch (s) {
    case roms::RomState::Verified: return "Verified";
    case roms::RomState::Shadowed: return "Shadowed";
    case roms::RomState::Mismatch: return "Mismatch";
    case roms::RomState::Missing: return "Missing";
    }
    return "?";
}

const char* stateTip(roms::RomState s) {
    switch (s) {
    case roms::RomState::Verified:
        return "The first file POM68K finds is this dump (SHA-256 checked).";
    case roms::RomState::Shadowed:
        return "The dump is here, but another file is found first and is\n"
               "what a machine would load.";
    case roms::RomState::Mismatch:
        return "Files exist where POM68K looks for this dump, but none of\n"
               "them is it (size or SHA-256 differ).";
    case roms::RomState::Missing:
        return "Nothing where POM68K looks for this dump.";
    }
    return "";
}

// A filled chip, as in the Abstraction Levels window: the state is the
// column the eye scans.
void chip(const char* text, ImVec4 c) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pad(ImGui::GetStyle().FramePadding.x * 0.75f, 1.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x + pad.x * 2, p0.y + size.y + pad.y * 2);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 0.20f)), 3.0f);
    draw->AddRect(p0, p1, ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 0.60f)), 3.0f);
    draw->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y), ImGui::GetColorU32(c), text);
    ImGui::Dummy(ImVec2(p1.x - p0.x, p1.y - p0.y));
}

const char* needText(roms::RomNeed n) {
    switch (n) {
    case roms::RomNeed::Required: return "Required";
    case roms::RomNeed::Substitute: return "Substitute (HLE)";
    case roms::RomNeed::Optional: return "Optional";
    }
    return "?";
}

const char* needTip(roms::RomNeed n) {
    switch (n) {
    case roms::RomNeed::Required:
        return "Without it the machine refuses to start or stalls in its boot.";
    case roms::RomNeed::Substitute:
        return "Without it a documented software substitute (HLE) runs in its\n"
               "place; strict LLE mode refuses such a session.";
    case roms::RomNeed::Optional:
        return "Nothing breaks without it: a feature, or another revision.";
    }
    return "";
}

const char* kindHeading(roms::RomKind k) {
    switch (k) {
    case roms::RomKind::MachineRom: return "Machine ROMs";
    case roms::RomKind::Firmware: return "Controller firmware";
    case roms::RomKind::DeclarationRom: return "Declaration ROMs";
    }
    return "?";
}

std::vector<const MachineProfile*> usersOf(const roms::RomDump& d) {
    std::vector<const MachineProfile*> out;
    for (const MachineProfile& p : kMachineProfiles)
        if (roms::romUsedBy(d, p.slug)) out.push_back(&p);
    return out;
}

std::string usersText(const std::vector<const MachineProfile*>& users, std::size_t keep) {
    std::string out;
    for (std::size_t i = 0; i < users.size() && i < keep; ++i) {
        if (i) out += ", ";
        out += users[i]->label;
    }
    if (users.size() > keep) out += ", +" + std::to_string(users.size() - keep) + " more";
    return out;
}

// A path as the user reads it: relative to where POM68K looks when it is
// below that folder.
std::string shownPath(const std::string& p, const roms::RomSearch& search) {
    if (p.empty()) return p;
    std::error_code ec;
    const fs::path rel = fs::path(p).lexically_relative(search.workingDir);
    if (!rel.empty() && *rel.begin() != "..") return rel.generic_string();
    (void)ec;
    return p;
}

bool rowMatches(const roms::RomDump& d, const std::vector<const MachineProfile*>& users,
                const char* needle) {
    if (!needle[0]) return true;
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower((unsigned char)c));
        return s;
    };
    std::string hay = std::string(d.id) + " " + d.label + " " + d.path + " " +
                      roms::romKindName(d.kind);
    for (const MachineProfile* p : users) hay += std::string(" ") + p->label + " " + p->slug;
    return lower(hay).find(lower(needle)) != std::string::npos;
}

void dumpTooltip(const roms::RomDump& d, const roms::RomStatus& st,
                 const std::vector<const MachineProfile*>& users) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
    ImGui::Text("%s  (%s)", d.label, d.id);
    ImGui::TextDisabled("%s, %u bytes", roms::romKindName(d.kind), unsigned(d.size));
    ImGui::TextDisabled("SHA-256 %.16s...", d.sha256);
    ImGui::Separator();
    ImGui::TextWrapped("Installed as: %s", d.path);
    if (d.alsoAt[0]) ImGui::TextWrapped("Also read from: %s", d.alsoAt);
    if (!st.verifiedAt.empty() && st.verifiedAt != st.foundAt)
        ImGui::TextWrapped("The dump itself: %s", st.verifiedAt.c_str());
    ImGui::TextWrapped("Without it: %s", d.absent);
    ImGui::Separator();
    ImGui::TextWrapped("Used by: %s", usersText(users, users.size()).c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// ── The import browser (the Set Up window's, for files AND folders) ────────

void copyPath(std::array<char, 1024>& field, const std::string& path) {
    std::snprintf(field.data(), field.size(), "%s", path.c_str());
}

void drawImportBrowser(GuiRomState& s) {
    if (!s.browsing) return;
    ImGui::SeparatorText("Choose a ROM file, a folder or a zip");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##romBrowseFolder", s.folder.data(), s.folder.size());
    const fs::path directory = app::pathFromUtf8(s.folder.data());
    if (ImGui::SmallButton("Parent folder"))
        copyPath(s.folder, app::utf8FromPath(directory.parent_path()));
    ImGui::SameLine();
    if (ImGui::SmallButton("Use this folder")) {
        copyPath(s.importPath, s.folder.data());
        s.browsing = false;
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Close browser")) {
        s.browsing = false;
        return;
    }
    struct Entry { fs::path path; bool directory; };
    std::vector<Entry> entries;
    std::error_code error;
    for (fs::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        const bool folder = it->is_directory(error);
        if (error) break;
        if (folder || it->is_regular_file(error)) entries.push_back({it->path(), folder});
    }
    s.browseError = error ? "Cannot open folder: " + error.message() : "";
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory) return a.directory;
        return a.path.filename() < b.path.filename();
    });
    ImGui::BeginChild("RomFiles", ImVec2(0, 120), ImGuiChildFlags_Borders);
    for (const Entry& entry : entries) {
        const std::string label = app::utf8FromPath(entry.path.filename()) +
                                  (entry.directory ? " /" : "");
        if (ImGui::Selectable(label.c_str())) {
            if (entry.directory) {
                copyPath(s.folder, app::utf8FromPath(entry.path));
            } else {
                std::error_code absoluteError;
                const fs::path absolute = fs::absolute(entry.path, absoluteError);
                copyPath(s.importPath, app::utf8FromPath(absoluteError ? entry.path : absolute));
                s.browsing = false;
            }
            break;
        }
    }
    if (entries.empty() && !error) ImGui::TextDisabled("This folder is empty.");
    ImGui::EndChild();
    if (!s.browseError.empty()) ImGui::TextWrapped("%s", s.browseError.c_str());
}

// ── Sections ───────────────────────────────────────────────────────────────

void drawProgress(GuiRomState& s, const RomJobView& v) {
    switch (v.kind) {
    case RomJobKind::Scan:
        ImGui::TextDisabled("Checking the roms folders (every candidate is hashed)...");
        return;
    case RomJobKind::Import:
        ImGui::ProgressBar(-1.0f * float(ImGui::GetTime()), ImVec2(-6.0f * ImGui::GetFontSize(), 0),
                           "Importing...");
        break;
    case RomJobKind::Fetch: {
        char overlay[64];
        std::snprintf(overlay, sizeof overlay, "%d / %d", v.done, v.total);
        const float fraction = v.total > 0 ? float(v.done) / float(v.total) : 0.0f;
        ImGui::ProgressBar(fraction, ImVec2(-6.0f * ImGui::GetFontSize(), 0), overlay);
        break;
    }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(v.cancelling);
    if (ImGui::Button("Cancel")) romWindowCancel(s);
    ImGui::EndDisabled();
    if (v.kind == RomJobKind::Fetch)
        ImGui::TextDisabled("%s", v.label.empty() ? "Contacting the download sources..."
                                                  : ("Fetching " + plain(v.label)).c_str());
}

void drawFetchResult(const roms::FetchResult& r) {
    ImGui::PushTextWrapPos(0.0f);
    if (r.cancelled) ImGui::TextColored(kOrange, "Download cancelled.");
    if (r.items.empty()) {
        // Nothing attempted: the run could not start, or had nothing to do.
        ImGui::TextColored(r.error.empty() ? kGreen : kRed, "%s", plain(r.summary).c_str());
        ImGui::PopTextWrapPos();
        return;
    }
    if (r.saved == 0)
        ImGui::TextColored(kRed, "Nothing was installed: %d dump%s failed.", r.failed,
                           r.failed == 1 ? "" : "s");
    else
        ImGui::TextColored(r.failed ? kOrange : kGreen,
                           "Installed %d: %d from the POM68K archive, %d from RetroBIOS. Failed: %d.",
                           r.saved, r.fromArchive, r.fromRetroBios, r.failed);
    if (r.replaced)
        ImGui::TextWrapped("%d replaced a different file; the old file was kept in roms-replaced/.",
                           r.replaced);
    if (!r.archiveError.empty())
        ImGui::TextColored(kOrange, "%s", plain(r.archiveError).c_str());
    if (r.failed && ImGui::TreeNode("##fetchFailed", "Not installed (%d)", r.failed)) {
        for (const roms::FetchItem& it : r.items) {
            if (it.ok || !it.dump) continue;
            // The archive's reason is said once, above; each line keeps
            // what is particular to its dump.
            std::string why = it.error;
            if (!r.archiveError.empty() && why.rfind(r.archiveError, 0) == 0) {
                why.erase(0, r.archiveError.size());
                while (!why.empty() && (why[0] == ';' || why[0] == ' ' || why[0] == ','))
                    why.erase(0, 1);
                if (why.empty()) why = "the POM68K archive is unavailable";
            }
            ImGui::Bullet();
            ImGui::TextWrapped("%s: %s", it.dump->label, plain(why).c_str());
        }
        ImGui::TreePop();
    }
    ImGui::PopTextWrapPos();
}

void drawImportResult(const roms::ImportResult& r) {
    ImGui::PushTextWrapPos(0.0f);
    const ImVec4 tone = r.failed ? kOrange : (r.installed || r.alreadyPresent) ? kGreen : kOrange;
    ImGui::TextColored(tone, "%s", plain(r.summary).c_str());
    if (!r.items.empty() && ImGui::TreeNode("##importFound", "Recognised (%d)", int(r.items.size()))) {
        for (const roms::ImportItem& it : r.items) {
            if (!it.dump) continue;
            const char* verdict = !it.error.empty() ? "refused" : it.alreadyPresent ? "already present"
                                                                                    : "installed";
            ImGui::Bullet();
            ImGui::TextWrapped("%s: %s (from %s)%s%s", it.dump->label, verdict,
                               plain(it.source).c_str(), it.error.empty() ? "" : " - ",
                               plain(it.error).c_str());
        }
        ImGui::TreePop();
    }
    if (!r.unrecognised.empty() &&
        ImGui::TreeNode("##importUnknown", "Not recognised (%d)", int(r.unrecognised.size()))) {
        for (const std::string& u : r.unrecognised) {
            ImGui::Bullet();
            ImGui::TextWrapped("%s", plain(u).c_str());
        }
        ImGui::TreePop();
    }
    ImGui::PopTextWrapPos();
}

struct Counts {
    int verified = 0, total = 0;
    int missingRequired = 0, missingOther = 0;      // whole catalogue
    int focusMissingRequired = 0, focusMissing = 0; // the focused profile
};

Counts countRows(const GuiRomState& s, const MachineProfile* focus) {
    Counts c;
    // A profile starts on ANY machine ROM the catalogue lists for it
    // (MachineFactory::findProfileRom), so once one is verified the others
    // are no longer what keeps it from starting.
    bool focusHasRom = false;
    for (const roms::RomStatus& st : s.rows)
        focusHasRom = focusHasRom ||
            (focus && st.dump && st.dump->kind == roms::RomKind::MachineRom &&
             st.state == roms::RomState::Verified && roms::romUsedBy(*st.dump, focus->slug));
    for (const roms::RomStatus& st : s.rows) {
        if (!st.dump) continue;
        ++c.total;
        const bool ok = st.state == roms::RomState::Verified;
        if (ok) { ++c.verified; continue; }
        const bool required = st.dump->need == roms::RomNeed::Required;
        (required ? c.missingRequired : c.missingOther)++;
        if (focus && roms::romUsedBy(*st.dump, focus->slug)) {
            ++c.focusMissing;
            const bool spare = focusHasRom && st.dump->kind == roms::RomKind::MachineRom;
            if (required && !spare) ++c.focusMissingRequired;
        }
    }
    return c;
}

void drawTable(GuiRomState& s, const MachineProfile* focus) {
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##roms", 5, flags)) return;
    const float em = ImGui::GetFontSize();
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 5.2f * em);
    ImGui::TableSetupColumn("Dump", ImGuiTableColumnFlags_WidthStretch, 0.27f);
    ImGui::TableSetupColumn("Need", ImGuiTableColumnFlags_WidthFixed, 9.6f * em);
    ImGui::TableSetupColumn("Used by", ImGuiTableColumnFlags_WidthStretch, 0.25f);
    ImGui::TableSetupColumn("Found at / without it", ImGuiTableColumnFlags_WidthStretch, 0.48f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    const ImU32 focusBg = ImGui::GetColorU32(ImVec4(0.40f, 0.55f, 0.95f, 0.14f));
    for (int k = 0; k <= int(roms::RomKind::DeclarationRom); ++k) {
        const roms::RomKind kind = roms::RomKind(k);
        bool headed = false;
        for (const roms::RomStatus& st : s.rows) {
            const roms::RomDump* d = st.dump;
            if (!d || d->kind != kind) continue;
            const bool mine = focus && roms::romUsedBy(*d, focus->slug);
            if (!s.allMachines && !mine) continue;
            const std::vector<const MachineProfile*> users = usersOf(*d);
            if (!rowMatches(*d, users, s.filter.data())) continue;
            if (!headed) {
                headed = true;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(kHeading, "%s", kindHeading(kind));
            }
            ImGui::PushID(d->id);
            ImGui::TableNextRow();
            if (mine && s.allMachines) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, focusBg);

            ImGui::TableSetColumnIndex(0);
            chip(stateText(st.state), stateColour(st.state));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", stateTip(st.state));

            ImGui::TableSetColumnIndex(1);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(d->label);
            ImGui::PopTextWrapPos();
            if (ImGui::IsItemHovered()) dumpTooltip(*d, st, users);

            ImGui::TableSetColumnIndex(2);
            const ImVec4 needTone = d->need == roms::RomNeed::Required ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                                       : kGrey;
            ImGui::TextColored(needTone, "%s", needText(d->need));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", needTip(d->need));

            ImGui::TableSetColumnIndex(3);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(usersText(users, 3).c_str());
            ImGui::PopTextWrapPos();
            if (users.size() > 3 && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", usersText(users, users.size()).c_str());

            ImGui::TableSetColumnIndex(4);
            ImGui::PushTextWrapPos(0.0f);
            switch (st.state) {
            case roms::RomState::Verified:
                ImGui::TextUnformatted(shownPath(st.foundAt, s.search).c_str());
                break;
            case roms::RomState::Shadowed:
                ImGui::TextColored(kOrange, "Loaded first: %s", shownPath(st.foundAt, s.search).c_str());
                if (!st.detail.empty()) ImGui::TextDisabled("%s", plain(st.detail).c_str());
                break;
            case roms::RomState::Mismatch:
                ImGui::TextColored(kRed, "%s", shownPath(st.foundAt, s.search).c_str());
                if (!st.detail.empty()) ImGui::TextDisabled("%s", plain(st.detail).c_str());
                break;
            case roms::RomState::Missing:
                ImGui::TextUnformatted(d->absent);
                ImGui::TextDisabled(roms::romHasRetroBiosCopy(*d)
                                        ? "Download: POM68K archive, or RetroBIOS"
                                        : "Download: POM68K archive only");
                break;
            }
            ImGui::PopTextWrapPos();
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

} // namespace

// ── Jobs ───────────────────────────────────────────────────────────────────

RomJobView romJobView(const GuiRomState& s) {
    RomJobView v;
    if (!s.job) return v;
    v.running = true;
    v.kind = s.job->kind;
    v.cancelling = s.job->cancel.load();
    const std::lock_guard<std::mutex> lock(s.job->mutex);
    v.done = s.job->progress.done;
    v.total = s.job->progress.total;
    v.label = s.job->progress.label;
    return v;
}

bool romWindowRescan(GuiRomState& s) {
    return startJob(s, RomJobKind::Scan, [](RomJob&) {});
}

bool romWindowStartFetch(GuiRomState& s, std::optional<SnapMachine> only) {
    if (s.job || !roms::romFetchAvailable()) return false;
    roms::FetchOptions options;
    options.download = s.download;
    if (only) {
        options.only = profileIds(*only);
        if (options.only.empty()) return false;
    }
    s.fetched.reset();
    s.imported.reset();
    return startJob(s, RomJobKind::Fetch, [options = std::move(options), search = s.search,
                                           fetcher = s.fetcher](RomJob& j) mutable {
        options.progress = [&j](const roms::FetchProgress& p) {
            const std::lock_guard<std::mutex> lock(j.mutex);
            j.progress = p;
        };
        options.cancelled = [&j] { return j.cancel.load(); };
        j.fetched = fetcher ? fetcher(search, options) : roms::fetchRoms(search, options);
    });
}

bool romWindowStartImport(GuiRomState& s, std::vector<fs::path> inputs) {
    if (s.job || inputs.empty()) return false;
    s.fetched.reset();
    s.imported.reset();
    return startJob(s, RomJobKind::Import, [inputs = std::move(inputs), search = s.search,
                                            importer = s.importer](RomJob& j) {
        roms::ImportOptions options;
        options.cancelled = [&j] { return j.cancel.load(); };
        j.imported = importer ? importer(inputs, search, options)
                              : roms::importRoms(inputs, search, options);
    });
}

void romWindowCancel(GuiRomState& s) {
    if (!s.job) return;
    s.job->cancel.store(true);
    if (s.job->thread.joinable()) s.job->thread.join();
    romWindowPoll(s);
}

bool romWindowPoll(GuiRomState& s) {
    if (!s.job || !s.job->finished.load()) return false;
    if (s.job->thread.joinable()) s.job->thread.join();
    const std::unique_ptr<RomJob> j = std::move(s.job);
    if (j->scanned) {
        s.rows = std::move(j->rows);
        s.destination = std::move(j->destination);
        s.scanned = true;
    }
    int installed = 0;
    if (j->fetched) {
        installed = j->fetched->saved;
        s.fetched = std::move(j->fetched);
    }
    if (j->imported) {
        installed = j->imported->installed;
        s.imported = std::move(j->imported);
    }
    if (!j->error.empty()) {
        // A library exception is a bug, but the panel still says something.
        if (j->kind == RomJobKind::Fetch && !s.fetched) {
            s.fetched.emplace();
            s.fetched->error = s.fetched->summary = "The download stopped: " + j->error;
        } else if (j->kind == RomJobKind::Import && !s.imported) {
            s.imported.emplace();
            s.imported->summary = "The import stopped: " + j->error;
        }
    }
    if (installed > 0) {
        ++s.installs;
        app::MachineFactory::forgetRomSignatures();
    }
    // A cancelled run skipped its closing scan: what it did install must
    // still show.
    if (!j->scanned && j->kind != RomJobKind::Scan) romWindowRescan(s);
    else if (!s.pendingImport.empty()) romWindowStartImport(s, std::exchange(s.pendingImport, {}));
    return true;
}

// ── Drops ──────────────────────────────────────────────────────────────────

namespace {
std::mutex gDropMutex;
std::vector<fs::path> gDrops;
} // namespace

bool romDropWanted(const fs::path& p) {
    std::error_code ec;
    const fs::file_status st = fs::status(p, ec);
    if (ec) return false;
    if (fs::is_directory(st)) return true;
    if (!fs::is_regular_file(st)) return false;
    std::string ext = p.extension().string();
    for (char& c : ext) c = char(std::tolower((unsigned char)c));
    if (ext == ".zip") return true;
    // A disk image that happens to be a dump's size stays the Disks
    // window's; `.bin` is the one extension both sides use.
    for (const char* disk : {".dsk", ".img", ".image", ".vhd", ".hda", ".dart", ".moof",
                             ".iso", ".cdr", ".toast", ".cue"})
        if (ext == disk) return false;
    const std::uintmax_t size = fs::file_size(p, ec);
    if (ec) return false;
    for (const roms::RomDump& d : roms::kRomDumps)
        if (d.size == size) return true;
    return false;
}

void romWindowOfferDrop(const std::vector<std::string>& paths) {
    const std::lock_guard<std::mutex> lock(gDropMutex);
    for (const std::string& p : paths) gDrops.push_back(app::pathFromUtf8(p));
}

std::vector<fs::path> romWindowTakeDrops() {
    const std::lock_guard<std::mutex> lock(gDropMutex);
    return std::exchange(gDrops, {});
}

// ── The window ─────────────────────────────────────────────────────────────

void romWindowShowFor(GuiSessionState& state, SnapMachine profile) {
    GuiRomState& s = state.roms;
    s.showWindow = true;
    s.focus = profile;
    s.allMachines = false;
    s.returnToStartup = true;
}

void romWindowDownloadFor(GuiSessionState& state, SnapMachine profile) {
    romWindowShowFor(state, profile);
    GuiRomState& s = state.roms;
    // A scan in flight (the window was already open) would make the click
    // a no-op; stop it instead — the fetch ends with its own scan.
    if (s.job && s.job->kind == RomJobKind::Scan) romWindowCancel(s);
    romWindowStartFetch(s, profile);
}

void drawRomWindow(GuiSessionState& state, SnapMachine current) {
    GuiRomState& s = state.roms;
    std::vector<fs::path> dropped = romWindowTakeDrops();
    if (!dropped.empty()) {
        s.showWindow = true;
        for (fs::path& p : dropped) s.pendingImport.push_back(std::move(p));
    }
    drawRomWindow(s, current);
}

void drawRomWindow(GuiRomState& s, SnapMachine current) {
    romWindowPoll(s);
    if (!s.job && !s.pendingImport.empty())
        romWindowStartImport(s, std::exchange(s.pendingImport, {}));
    if (!s.showWindow) return;
    if (!s.scanned && !s.job) romWindowRescan(s);

    ImGui::SetNextWindowSize(ImVec2(900, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kRomWindowTitle, &s.showWindow)) {
        ImGui::End();
        return;
    }
    const MachineProfile* focus = machineProfile(s.focus.value_or(current));
    const RomJobView job = romJobView(s);
    const Counts c = countRows(s, focus);

    // ── Where things stand ──────────────────────────────────────────────
    ImGui::PushTextWrapPos(0.0f);
    if (!s.scanned) {
        ImGui::TextDisabled("Checking the roms folders...");
    } else {
        ImGui::Text("%d of %d catalogued dumps verified. Missing or wrong: %d required, %d other.",
                    c.verified, c.total, c.missingRequired, c.missingOther);
        if (focus) {
            if (c.focusMissing == 0)
                ImGui::TextColored(kGreen, "%s: every dump it uses is here.", focus->label);
            else if (c.focusMissingRequired)
                ImGui::TextColored(kRed, "%s: %d required dump%s missing, %d in all.", focus->label,
                                   c.focusMissingRequired, c.focusMissingRequired == 1 ? "" : "s",
                                   c.focusMissing);
            else
                ImGui::TextColored(kOrange, "%s: can start; %d optional or substitutable dump%s missing.",
                                   focus->label, c.focusMissing, c.focusMissing == 1 ? "" : "s");
        }
        if (s.destination.ok())
            ImGui::TextDisabled("New dumps go to %s", app::utf8FromPath(s.destination.romsDir).c_str());
        else
            ImGui::TextColored(kOrange, "Nothing can be installed: %s", plain(s.destination.error).c_str());
    }
    ImGui::PopTextWrapPos();

    // ── Actions ─────────────────────────────────────────────────────────
    std::string fetchWhy;
    const bool canFetch = roms::romFetchAvailable(&fetchWhy);
    const bool idle = !job.running;
    const bool writable = s.scanned && s.destination.ok();
    ImGui::BeginDisabled(!idle);
    if (ImGui::Button("Rescan")) romWindowRescan(s);
    ImGui::EndDisabled();
    ImGui::SameLine();
    char forLabel[128];
    std::snprintf(forLabel, sizeof forLabel, "Download missing for %s###romFetchMine",
                  focus ? focus->label : "this Mac");
    ImGui::BeginDisabled(!idle || !canFetch || !writable || !focus || c.focusMissing == 0);
    if (ImGui::Button(forLabel)) romWindowStartFetch(s, focus->snapshot);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!idle || !canFetch || !writable ||
                         c.missingRequired + c.missingOther == 0);
    if (ImGui::Button("Download all missing")) romWindowStartFetch(s, std::nullopt);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!idle || !writable);
    if (ImGui::Button("Import...")) {
        s.browsing = !s.browsing;
        if (!s.folder[0]) {
            std::error_code ec;
            copyPath(s.folder, app::utf8FromPath(fs::current_path(ec)));
        }
    }
    ImGui::EndDisabled();
    if (!canFetch)
        ImGui::TextColored(kOrange, "Downloads are unavailable: %s", plain(fetchWhy).c_str());

    // The import path: typed, picked in the browser, or dropped on the window.
    if (s.browsing || s.importPath[0]) {
        ImGui::SetNextItemWidth(-7.0f * ImGui::GetFontSize());
        ImGui::InputTextWithHint("##romImportPath", "ROM file, folder or zip to import",
                                 s.importPath.data(), s.importPath.size());
        ImGui::SameLine();
        ImGui::BeginDisabled(!idle || !writable || !s.importPath[0]);
        if (ImGui::Button("Import")) {
            romWindowStartImport(s, {app::pathFromUtf8(s.importPath.data())});
            s.browsing = false;
        }
        ImGui::EndDisabled();
        drawImportBrowser(s);
    }

    // ── The running job, or the last result ─────────────────────────────
    if (job.running) drawProgress(s, job);
    if (s.fetched && !(job.running && job.kind == RomJobKind::Fetch)) drawFetchResult(*s.fetched);
    if (s.imported && !(job.running && job.kind == RomJobKind::Import)) drawImportResult(*s.imported);
    if (s.installs > 0) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("A running machine keeps the dumps it started with: new ones are used "
                           "the next time a machine starts (Set Up a Macintosh, Change machine, "
                           "or Apply and restart in the LLE / HLE controllers window).");
        ImGui::PopTextWrapPos();
    }
    if (s.returnToStartup && focus) {
        char back[128];
        std::snprintf(back, sizeof back, "Back to Set Up a Macintosh (%s)###romBack", focus->label);
        ImGui::BeginDisabled(!idle);
        if (ImGui::Button(back)) {
            s.backToStartup = s.focus;
            s.returnToStartup = false;
            s.showWindow = false;
        }
        ImGui::EndDisabled();
    }

    // ── Sources and terms ───────────────────────────────────────────────
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("ROMs are Apple's copyrighted firmware: use only dumps you are entitled to "
                        "use. Every download and import is checked against the catalogue's "
                        "SHA-256 before it is installed, and a different file already in its "
                        "place is first copied to roms-replaced/. Nothing is downloaded until "
                        "you click a download button.");
    ImGui::PopTextWrapPos();
    ImGui::TextDisabled("Sources:");
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("POM68K archive (archive.org)", roms::kRomArchivePage);
    ImGui::SameLine();
    ImGui::TextDisabled("then");
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("RetroBIOS (GitHub)", roms::kRetroBiosHome);
    ImGui::SameLine();
    ImGui::TextDisabled("at commit %.8s, for the dumps it carries.", roms::kRetroBiosCommit);
    ImGui::TextDisabled("Drop ROM files, folders or zips on the POM68K window to import them.");

    // ── Filter and table ────────────────────────────────────────────────
    ImGui::Separator();
    if (ImGui::RadioButton("This machine", !s.allMachines)) s.allMachines = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("All dumps", s.allMachines)) s.allMachines = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##romFilter", "Filter by name, file or machine...", s.filter.data(),
                             s.filter.size());
    if (s.scanned) drawTable(s, focus);
    ImGui::End();
}

} // namespace pom68k::gui
