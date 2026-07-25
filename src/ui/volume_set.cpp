#include "ui/volume_set.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <optional>

namespace ui {
namespace {

// A volume's place in its set. `ordinal` is the set's own numbering, which is
// NOT normalised to start at 1: 7z starts at 001, split -d starts at 00, and
// forcing either onto a common base would invent gaps.
struct Parsed {
    std::string stem;
    int         ordinal = 0;
};

[[nodiscard]] std::string lowered(std::string_view s)
{
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

[[nodiscard]] bool all_digits(std::string_view s)
{
    return !s.empty() && std::ranges::all_of(s, [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
}

[[nodiscard]] int to_int(std::string_view digits)
{
    int v = 0;
    for (const char c : digits) {
        v = (v * 10) + (c - '0');
    }
    return v;
}

// ".7z.001" / ".tar.00" — trailing dot + digits. Two digits minimum, so a
// version-suffixed file like "backup.2" is not mistaken for a volume.
[[nodiscard]] std::optional<Parsed> parse_numeric(std::string_view name)
{
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) {
        return std::nullopt;
    }
    const std::string_view digits = name.substr(dot + 1);
    if (digits.size() < 2 || !all_digits(digits)) {
        return std::nullopt;
    }
    return Parsed{std::string(name.substr(0, dot)), to_int(digits)};
}

// Sentinel ordinal for a spanned set's trailing ".zip". Resolved to
// max(.zNN) + 1 once the whole set is known — the .zip really is the last
// volume, despite ".z01" sorting before it.
constexpr int SPANNED_ZIP_LAST = -1;

// ".z01" (volume N) or ".zip" (final volume).
[[nodiscard]] std::optional<Parsed> parse_spanned_zip(std::string_view name)
{
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) {
        return std::nullopt;
    }
    const auto             stem = std::string(name.substr(0, dot));
    const std::string_view ext  = name.substr(dot + 1);

    if (ext == "zip") {
        return Parsed{stem, SPANNED_ZIP_LAST};
    }
    if (ext.size() >= 2 && ext[0] == 'z' && all_digits(ext.substr(1))) {
        return Parsed{stem, to_int(ext.substr(1))};
    }
    return std::nullopt;
}

// ".part1.rar" / ".part01.rar" / ".part0001.rar" — all three padding widths
// occur in libarchive's own fixtures.
[[nodiscard]] std::optional<Parsed> parse_rar_part(std::string_view name)
{
    if (!name.ends_with(".rar")) {
        return std::nullopt;
    }
    const std::string_view without_rar = name.substr(0, name.size() - 4);
    const auto             dot         = without_rar.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) {
        return std::nullopt;
    }
    const std::string_view part = without_rar.substr(dot + 1);
    if (!part.starts_with("part") || !all_digits(part.substr(4))) {
        return std::nullopt;
    }
    return Parsed{std::string(without_rar.substr(0, dot)), to_int(part.substr(4))};
}

// ".rar" is volume ONE, then ".r00", ".r01", … — so ".rar" sorts after ".r00"
// alphabetically while preceding it in the set.
[[nodiscard]] std::optional<Parsed> parse_rar_old(std::string_view name)
{
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) {
        return std::nullopt;
    }
    const auto             stem = std::string(name.substr(0, dot));
    const std::string_view ext  = name.substr(dot + 1);

    if (ext == "rar") {
        return Parsed{stem, 1};
    }
    if (ext.size() >= 2 && ext[0] == 'r' && all_digits(ext.substr(1))) {
        return Parsed{stem, to_int(ext.substr(1)) + 2};
    }
    return std::nullopt;
}

using Parser = std::optional<Parsed> (*)(std::string_view);

[[nodiscard]] Parser parser_for(VolumeStyle style)
{
    using enum VolumeStyle;
    switch (style) {
        case NumericSuffix: return &parse_numeric;
        case SpannedZip:    return &parse_spanned_zip;
        case RarPart:       return &parse_rar_part;
        case RarOld:        return &parse_rar_old;
        case None:          break;
    }
    return nullptr;
}

struct Member {
    std::string name;
    int         ordinal;
};

// Every sibling that parses under `style` with the same stem as the pick.
[[nodiscard]] std::vector<Member> gather(VolumeStyle                  style,
                                         std::string_view             stem,
                                         std::span<const std::string> siblings)
{
    const Parser        parse = parser_for(style);
    std::vector<Member> found;
    for (const std::string& sib : siblings) {
        const auto p = parse(lowered(sib));
        if (p && p->stem == stem) {
            found.emplace_back(sib, p->ordinal);
        }
    }
    return found;
}

// Resolve the spanned-zip sentinel, then order by ordinal.
void finalise_order(std::vector<Member>& members)
{
    int highest = 0;
    for (const Member& m : members) {
        highest = std::max(highest, m.ordinal);
    }
    for (Member& m : members) {
        if (m.ordinal == SPANNED_ZIP_LAST) {
            m.ordinal = highest + 1;
        }
    }
    std::ranges::sort(members, [](const Member& a, const Member& b) {
        return a.ordinal < b.ordinal;
    });
}

// Ordinals absent from the run between the lowest and highest present.
[[nodiscard]] std::vector<int> gaps(const std::vector<Member>& ordered)
{
    std::vector<int> missing;
    if (ordered.empty()) {
        return missing;
    }
    for (int want = ordered.front().ordinal; want <= ordered.back().ordinal; ++want) {
        const bool present = std::ranges::any_of(
            ordered, [want](const Member& m) { return m.ordinal == want; });
        if (!present) {
            missing.push_back(want);
        }
    }
    return missing;
}

} // namespace

VolumeAssembly assembly_for(VolumeStyle style) noexcept
{
    using enum VolumeStyle;
    switch (style) {
        case NumericSuffix: return VolumeAssembly::Concatenate;
        // Each RAR volume carries its own header, so the set cannot be glued
        // together — libarchive has to be handed the paths.
        case RarPart:
        case RarOld:        return VolumeAssembly::FileOriented;
        // A spanned zip concatenates into something whose central directory
        // still points at per-disk offsets; it needs rewriting first.
        case SpannedZip:    return VolumeAssembly::SpannedZipMerge;
        case None:          break;
    }
    return VolumeAssembly::None;
}

std::vector<uint8_t> concatenate_volumes(std::span<const std::span<const uint8_t>> volumes)
{
    size_t total = 0;
    for (const std::span<const uint8_t>& v : volumes) {
        total += v.size();
    }

    std::vector<uint8_t> out;
    out.reserve(total);
    for (const std::span<const uint8_t>& v : volumes) {
        out.insert(out.end(), v.begin(), v.end());
    }
    return out;
}

VolumeSet detect_volume_set(std::string_view picked, std::span<const std::string> siblings)
{
    const std::string lower = lowered(picked);

    // Order matters. RarPart before RarOld, because "x.part1.rar" also ends
    // ".rar". SpannedZip before NumericSuffix is not required (".z01" is not
    // all-digits) but keeps the intent obvious.
    struct Candidate {
        VolumeStyle style;
        Parser      parse;
    };
    using enum VolumeStyle;
    const std::array<Candidate, 4> candidates{{
        {.style = RarPart, .parse = &parse_rar_part},
        {.style = SpannedZip, .parse = &parse_spanned_zip},
        {.style = NumericSuffix, .parse = &parse_numeric},
        {.style = RarOld, .parse = &parse_rar_old},
    }};

    for (const Candidate& c : candidates) {
        const auto p = c.parse(lower);
        if (!p) {
            continue;
        }
        std::vector<Member> members = gather(c.style, p->stem, siblings);
        // A set needs at least two volumes. This is what keeps an ordinary
        // "photos.zip" or "scans.rar" — which parse as a candidate volume of a
        // set of one — from being reported as a broken multi-volume archive.
        if (members.size() < 2) {
            continue;
        }
        finalise_order(members);

        VolumeSet set;
        set.style = c.style;
        set.stem  = p->stem;
        set.volumes.reserve(members.size());
        for (const Member& m : members) {
            set.volumes.push_back(m.name);
        }
        set.missing = gaps(members);
        return set;
    }

    return {};
}

} // namespace ui
