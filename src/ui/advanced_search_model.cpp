#include "ui/advanced_search_model.h"

#include <algorithm>

#include "vault/byte_io.h"  // header-only LE byte I/O (no vault dependency pulled in)

namespace ui {

namespace {

char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equals_ci(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    return true;
}

bool contains_ci(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool hit = true;
        for (std::size_t j = 0; j < needle.size(); ++j)
            if (ascii_lower(haystack[i + j]) != ascii_lower(needle[j])) { hit = false; break; }
        if (hit) return true;
    }
    return false;
}

// True if `tag` equals one of `tags` (case-insensitive exact match).
bool tag_present(const std::vector<std::string>& tags, std::string_view tag)
{
    return std::ranges::any_of(tags, [&](const auto& t) { return equals_ci(t, tag); });
}

bool group_holds(const TagGroup& g, const std::vector<std::string>& effective)
{
    if (g.tags.empty()) return true;  // an empty group constrains nothing
    if (g.combinator == Combinator::And)
        return std::ranges::all_of(g.tags, [&](const auto& t) { return tag_present(effective, t); });
    return std::ranges::any_of(g.tags, [&](const auto& t) { return tag_present(effective, t); });
}

constexpr int NAME_MATCH_SCORE = 2;  // a name-substring hit ranks alongside a weight-2 tag
constexpr int GROUP_TAG_SCORE  = 1;  // each present group tag nudges the score

// Serialisation bounds — keep a hostile/corrupt blob from driving a huge alloc.
constexpr uint16_t MAX_LIST   = 4096;    // include / exclude / groups / per-group tags
constexpr uint16_t MAX_STRLEN = 0xFFFF;
constexpr uint8_t  QUERY_VERSION = 1;

void write_string(vault::ByteWriter& w, const std::string& s)
{
    const uint16_t len = s.size() > MAX_STRLEN ? MAX_STRLEN : static_cast<uint16_t>(s.size());
    w.u16(len);
    w.bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), len));
}

bool read_string(vault::ByteReader& r, std::string& out)
{
    const uint16_t len = r.u16();
    if (!r.ok()) return false;
    out.resize(len);
    if (len > 0) {
        r.bytes(std::span<uint8_t>(reinterpret_cast<uint8_t*>(out.data()), len));
        if (!r.ok()) return false;
    }
    return true;
}

bool read_string_list(vault::ByteReader& r, std::vector<std::string>& out)
{
    const uint16_t n = r.u16();
    if (!r.ok() || n > MAX_LIST) return false;
    out.clear();
    for (uint16_t i = 0; i < n; ++i) {
        std::string s;
        if (!read_string(r, s)) return false;
        out.push_back(std::move(s));
    }
    return true;
}

} // namespace

EvalResult evaluate(const AdvancedQuery& query, std::string_view name,
                    const std::vector<std::string>& effective_tags)
{
    // Exclude is a hard negative filter — any present excluded tag rejects.
    for (const auto& ex : query.exclude)
        if (tag_present(effective_tags, ex)) return {false, 0};

    // Name substring clause (when present).
    const bool name_clause = query.name_query.empty();
    const bool name_hit    = name_clause ? false : contains_ci(name, query.name_query);
    if (!name_clause && !name_hit) return {false, 0};

    // Include clause: an OR gate — at least one include tag must be present.
    bool include_ok = query.include.empty();
    int  score      = 0;
    for (const auto& wt : query.include) {
        if (tag_present(effective_tags, wt.tag)) { include_ok = true; score += wt.weight; }
    }
    if (!include_ok) return {false, 0};

    // Group clauses combined by the top-level join.
    if (!query.groups.empty()) {
        bool join = query.group_join == Combinator::And;  // identity element
        for (const auto& g : query.groups) {
            const bool h = group_holds(g, effective_tags);
            join = query.group_join == Combinator::And ? (join && h) : (join || h);
        }
        if (!join) return {false, 0};

        for (const auto& g : query.groups)
            for (const auto& t : g.tags)
                if (tag_present(effective_tags, t)) score += GROUP_TAG_SCORE;
    }

    if (name_hit) score += NAME_MATCH_SCORE;
    return {true, score};
}

std::vector<uint8_t> serialize_query(const AdvancedQuery& query)
{
    std::vector<uint8_t> out;
    vault::ByteWriter w(out);
    w.u8(QUERY_VERSION);

    const uint16_t inc = query.include.size() > MAX_LIST ? MAX_LIST
                                                         : static_cast<uint16_t>(query.include.size());
    w.u16(inc);
    for (uint16_t i = 0; i < inc; ++i) {
        write_string(w, query.include[i].tag);
        w.u32(static_cast<uint32_t>(query.include[i].weight));
    }

    const uint16_t exc = query.exclude.size() > MAX_LIST ? MAX_LIST
                                                         : static_cast<uint16_t>(query.exclude.size());
    w.u16(exc);
    for (uint16_t i = 0; i < exc; ++i) write_string(w, query.exclude[i]);

    const uint16_t grp = query.groups.size() > MAX_LIST ? MAX_LIST
                                                        : static_cast<uint16_t>(query.groups.size());
    w.u16(grp);
    for (uint16_t i = 0; i < grp; ++i) {
        write_string(w, query.groups[i].name);
        w.u8(static_cast<uint8_t>(query.groups[i].combinator));
        const auto& tags = query.groups[i].tags;
        const uint16_t tn = tags.size() > MAX_LIST ? MAX_LIST : static_cast<uint16_t>(tags.size());
        w.u16(tn);
        for (uint16_t j = 0; j < tn; ++j) write_string(w, tags[j]);
    }

    w.u8(static_cast<uint8_t>(query.group_join));
    write_string(w, query.name_query);
    w.u8(static_cast<uint8_t>(query.scope));
    return out;
}

bool deserialize_query(std::span<const uint8_t> in, AdvancedQuery& out)
{
    vault::ByteReader r(in);
    if (r.u8() != QUERY_VERSION || !r.ok()) return false;

    const uint16_t inc = r.u16();
    if (!r.ok() || inc > MAX_LIST) return false;
    out.include.clear();
    for (uint16_t i = 0; i < inc; ++i) {
        WeightedTag wt;
        if (!read_string(r, wt.tag)) return false;
        wt.weight = static_cast<int32_t>(r.u32());
        if (!r.ok()) return false;
        out.include.push_back(std::move(wt));
    }

    if (!read_string_list(r, out.exclude)) return false;

    const uint16_t grp = r.u16();
    if (!r.ok() || grp > MAX_LIST) return false;
    out.groups.clear();
    for (uint16_t i = 0; i < grp; ++i) {
        TagGroup g;
        if (!read_string(r, g.name)) return false;
        g.combinator = r.u8() == static_cast<uint8_t>(Combinator::Or) ? Combinator::Or : Combinator::And;
        if (!r.ok()) return false;
        if (!read_string_list(r, g.tags)) return false;
        out.groups.push_back(std::move(g));
    }

    out.group_join = r.u8() == static_cast<uint8_t>(Combinator::Or) ? Combinator::Or : Combinator::And;
    if (!r.ok()) return false;
    if (!read_string(r, out.name_query)) return false;
    const uint8_t scope = r.u8();
    if (!r.ok()) return false;
    out.scope = scope <= static_cast<uint8_t>(SearchScope::Both)
                    ? static_cast<SearchScope>(scope) : SearchScope::Both;

    return r.remaining() == 0;  // trailing bytes ⇒ corruption
}

std::vector<std::string> tag_suggestions(std::string_view prefix,
                                         const std::vector<std::string>& vocabulary)
{
    std::vector<std::string> out;
    if (prefix.empty()) return out;

    // Rank 0 = prefix match, rank 1 = mid-string substring match. De-duplicate
    // case-insensitively, keeping the first casing encountered.
    struct Cand { std::string text; int rank; };
    std::vector<Cand> cands;
    for (const auto& v : vocabulary) {
        if (v.size() < prefix.size()) {
            if (!contains_ci(v, prefix)) continue;  // can't prefix-match; maybe substring
        }
        const bool is_prefix = v.size() >= prefix.size() &&
                               equals_ci(std::string_view(v).substr(0, prefix.size()), prefix);
        if (!is_prefix && !contains_ci(v, prefix)) continue;
        if (std::ranges::any_of(cands, [&](const auto& c) { return equals_ci(c.text, v); })) continue;
        cands.push_back({v, is_prefix ? 0 : 1});
    }

    std::ranges::sort(cands, [](const Cand& a, const Cand& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.text < b.text;  // alphabetical tie-break
    });
    out.reserve(cands.size());
    for (auto& c : cands) out.push_back(std::move(c.text));
    return out;
}

} // namespace ui
