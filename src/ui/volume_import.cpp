#include "ui/volume_import.h"

#include "platform/paths.h"

#include <cstdio>
#include <format>
#include <optional>
#include <span>

namespace ui {
namespace {

[[nodiscard]] std::string join_numbers(const std::vector<int>& ns)
{
    std::string out;
    for (size_t i = 0; i < ns.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += std::to_string(ns[i]);
    }
    return out;
}

// Read a whole volume. Kept local rather than reusing the importer's helper so
// this module does not depend on the import pipeline.
[[nodiscard]] bool read_volume(const std::filesystem::path& p, std::vector<uint8_t>& out)
{
    std::FILE* fp = std::fopen(p.string().c_str(), "rb");
    if (fp == nullptr) {
        return false;
    }
    bool       ok   = std::fseek(fp, 0, SEEK_END) == 0;
    const long size = ok ? std::ftell(fp) : -1;
    ok              = ok && size >= 0 && std::fseek(fp, 0, SEEK_SET) == 0;
    if (ok) {
        out.resize(static_cast<size_t>(size));
        ok = out.empty() || std::fread(out.data(), 1, out.size(), fp) == out.size();
    }
    (void)std::fclose(fp);
    return ok;
}

} // namespace

VolumeSetSummary summarize_volume_set(const VolumeSet& set)
{
    VolumeSetSummary s;
    if (set.style == VolumeStyle::None || set.volumes.empty()) {
        return s;   // can_import stays false
    }

    s.heading = std::format("Import {} volumes as one archive?", set.volumes.size());
    s.volume_lines.reserve(set.volumes.size());
    for (const std::string& v : set.volumes) {
        s.volume_lines.push_back(v);
    }

    if (!set.missing.empty()) {
        // A gap yields a corrupt gallery rather than a partial one, so this is
        // a refusal, not a warning the user can click past.
        s.warning = std::format("Missing volume{}: {} — cannot import an incomplete set.",
                                set.missing.size() == 1 ? "" : "s", join_numbers(set.missing));
        return s;
    }

    s.can_import = true;
    return s;
}

AssembledVolumes assemble_volume_set(const VolumeSet& set, const std::filesystem::path& dir)
{
    AssembledVolumes out;
    out.assembly = assembly_for(set.style);

    if (out.assembly == VolumeAssembly::None || set.volumes.empty()) {
        out.error = "Not a multi-volume archive set.";
        return out;
    }
    if (!set.missing.empty()) {
        out.error = std::format("Incomplete set — missing volume{}: {}.",
                                set.missing.size() == 1 ? "" : "s", join_numbers(set.missing));
        return out;
    }

    // Invariant 6: these names came from a directory listing, i.e. from outside
    // the app. Normalise before anything opens them. ArchiveReader::open_files
    // deliberately does not normalise, so this boundary must.
    std::vector<std::filesystem::path> paths;
    paths.reserve(set.volumes.size());
    for (const std::string& name : set.volumes) {
        const auto normalised = platform::normalize_user_path((dir / name).string());
        if (!normalised.has_value()) {
            out.error = std::format("Unusable volume path: {}", name);
            return out;
        }
        paths.push_back(*normalised);
    }

    if (out.assembly == VolumeAssembly::FileOriented) {
        // Each RAR volume carries its own header; libarchive has to open the
        // files itself. Handing it a joined buffer truncates the archive.
        out.paths = std::move(paths);
        return out;
    }

    std::vector<std::vector<uint8_t>> bufs;
    bufs.reserve(paths.size());
    for (const std::filesystem::path& p : paths) {
        std::vector<uint8_t> b;
        if (!read_volume(p, b)) {
            // Detection saw the name in a listing; the file can still be gone,
            // unreadable, or renamed by the time the user confirms.
            out.error = std::format("Could not read volume: {}", p.filename().string());
            return out;
        }
        bufs.push_back(std::move(b));
    }

    std::vector<std::span<const uint8_t>> spans;
    spans.reserve(bufs.size());
    for (const auto& b : bufs) {
        spans.emplace_back(b);
    }
    out.bytes = concatenate_volumes(spans);

    // A spanned zip still needs its central directory rewritten; the merger is
    // applied by the caller, which owns that dependency.
    return out;
}

} // namespace ui
