#include "ui/advanced_search_model.h"

#include <algorithm>
#include <utility>

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
    using enum Combinator;
    if (g.tags.empty()) return true;  // an empty group constrains nothing
    auto present = [&](const auto& t) { return tag_present(effective, t); };
    return g.combinator == And ? std::ranges::all_of(g.tags, present)
                               : std::ranges::any_of(g.tags, present);
}

constexpr int NAME_MATCH_SCORE = 2;  // a name-substring hit ranks alongside a weight-2 tag
constexpr int GROUP_TAG_SCORE  = 1;  // each present group tag nudges the score

// --- evaluate() helpers (kept small so the public function stays simple) ----

bool excluded(const AdvancedQuery& q, const std::vector<std::string>& eff)
{
    return std::ranges::any_of(q.exclude, [&](const auto& ex) { return tag_present(eff, ex); });
}

// Sum the weights of present include tags; sets `any` if at least one matched.
int include_score(const AdvancedQuery& q, const std::vector<std::string>& eff, bool& any)
{
    int score = 0;
    for (const auto& wt : q.include)
        if (tag_present(eff, wt.tag)) { any = true; score += wt.weight; }
    return score;
}

// Combine every group by the top-level join (empty group list ⇒ true).
bool groups_pass(const AdvancedQuery& q, const std::vector<std::string>& eff)
{
    using enum Combinator;
    if (q.groups.empty()) return true;
    const bool want_all = q.group_join == And;
    for (const auto& g : q.groups) {
        const bool held = group_holds(g, eff);
        if (want_all && !held) return false;   // AND: one miss fails the join
        if (!want_all && held) return true;     // OR: one hit satisfies the join
    }
    return want_all;
}

int group_score(const AdvancedQuery& q, const std::vector<std::string>& eff)
{
    int score = 0;
    for (const auto& g : q.groups)
        for (const auto& t : g.tags)
            if (tag_present(eff, t)) score += GROUP_TAG_SCORE;
    return score;
}

// --- serialisation ----------------------------------------------------------

constexpr uint16_t MAX_LIST      = 4096;   // include / exclude / groups / per-group tags
constexpr uint16_t MAX_STRLEN    = 0xFFFF;
constexpr uint8_t  QUERY_VERSION = 1;

void write_string(vault::ByteWriter& w, std::string_view s)
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

Combinator read_combinator(vault::ByteReader& r)
{
    using enum Combinator;
    return r.u8() == std::to_underlying(Or) ? Or : And;
}

bool read_weighted_list(vault::ByteReader& r, std::vector<WeightedTag>& out)
{
    const uint16_t n = r.u16();
    if (!r.ok() || n > MAX_LIST) return false;
    out.clear();
    for (uint16_t i = 0; i < n; ++i) {
        WeightedTag wt;
        if (!read_string(r, wt.tag)) return false;
        wt.weight = static_cast<int32_t>(r.u32());
        if (!r.ok()) return false;
        out.push_back(std::move(wt));
    }
    return true;
}

bool read_groups(vault::ByteReader& r, std::vector<TagGroup>& out)
{
    const uint16_t n = r.u16();
    if (!r.ok() || n > MAX_LIST) return false;
    out.clear();
    for (uint16_t i = 0; i < n; ++i) {
        TagGroup g;
        if (!read_string(r, g.name)) return false;
        g.combinator = read_combinator(r);
        if (!r.ok() || !read_string_list(r, g.tags)) return false;
        out.push_back(std::move(g));
    }
    return true;
}

} // namespace

EvalResult evaluate(const AdvancedQuery& query, std::string_view name,
                    const std::vector<std::string>& effective_tags)
{
    if (excluded(query, effective_tags)) return {false, 0};

    const bool name_hit = !query.name_query.empty() && contains_ci(name, query.name_query);
    if (!query.name_query.empty() && !name_hit) return {false, 0};

    bool      include_ok = query.include.empty();
    int       score      = include_score(query, effective_tags, include_ok);
    if (!include_ok)                          return {false, 0};
    if (!groups_pass(query, effective_tags))  return {false, 0};

    score += group_score(query, effective_tags);
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
        const TagGroup& g = query.groups[i];
        write_string(w, g.name);
        w.u8(std::to_underlying(g.combinator));
        const uint16_t tn = g.tags.size() > MAX_LIST ? MAX_LIST : static_cast<uint16_t>(g.tags.size());
        w.u16(tn);
        for (uint16_t j = 0; j < tn; ++j) write_string(w, g.tags[j]);
    }

    w.u8(std::to_underlying(query.group_join));
    write_string(w, query.name_query);
    w.u8(std::to_underlying(query.scope));
    return out;
}

bool deserialize_query(std::span<const uint8_t> in, AdvancedQuery& out)
{
    vault::ByteReader r(in);
    if (r.u8() != QUERY_VERSION || !r.ok())     return false;
    if (!read_weighted_list(r, out.include))    return false;
    if (!read_string_list(r, out.exclude))      return false;
    if (!read_groups(r, out.groups))            return false;

    out.group_join = read_combinator(r);
    if (!r.ok() || !read_string(r, out.name_query)) return false;

    const uint8_t scope = r.u8();
    if (!r.ok()) return false;
    out.scope = scope <= std::to_underlying(SearchScope::Both)
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
    struct Cand {
        std::string text;
        int         rank;
        Cand(std::string t, int r) : text(std::move(t)), rank(r) {}
    };
    std::vector<Cand> cands;
    for (const auto& v : vocabulary) {
        const bool is_prefix = v.size() >= prefix.size() &&
                               equals_ci(std::string_view(v).substr(0, prefix.size()), prefix);
        if (!is_prefix && !contains_ci(v, prefix)) continue;
        if (std::ranges::any_of(cands, [&](const auto& c) { return equals_ci(c.text, v); })) continue;
        cands.emplace_back(v, is_prefix ? 0 : 1);
    }

    std::ranges::sort(cands, [](const Cand& a, const Cand& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.text < b.text;  // alphabetical tie-break
    });
    out.reserve(cands.size());
    for (auto& c : cands) out.push_back(std::move(c.text));
    return out;
}

int move_tag_cursor(int cur, int dir, int count)
{
    if (count <= 0) return -1;
    cur = std::clamp(cur, -1, count - 1);   // normalise out-of-range input
    if (dir > 0) return std::min(cur + 1, count - 1);
    if (dir < 0) return cur < 0 ? -1 : cur - 1;   // 0 -> -1, -1 stays -1
    return cur;
}

} // namespace ui
