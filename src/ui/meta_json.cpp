#include "ui/meta_json.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace ui {
namespace {

using nlohmann::json;

std::string trimmed(std::string_view s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) --e;
    return std::string(s.substr(b, e - b));
}

// obj[key] as a trimmed string, tolerating a missing key or a non-string value.
std::string str_or_empty(const json& obj, const char* key)
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return {};
    return trimmed(it->get_ref<const std::string&>());
}

// One tags[] element → "type:name" ("name" alone when type is empty). Entries
// without a usable name are dropped. A plain string entry is a bare tag.
void append_tag(const json& e, std::vector<std::string>& out)
{
    std::string name;
    std::string type;
    if (e.is_string()) {
        name = trimmed(e.get_ref<const std::string&>());
    } else if (e.is_object()) {
        name = str_or_empty(e, "name");
        type = str_or_empty(e, "type");
    }
    if (name.empty()) return;
    out.push_back(type.empty() ? name : type + ":" + name);
}

} // namespace

ArchiveMeta parse_meta_json(std::span<const uint8_t> bytes)
{
    ArchiveMeta meta;
    // allow_exceptions=false: malformed input (including invalid UTF-8) yields
    // a discarded value instead of a throw — the project is exception-free.
    const json doc = json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) return meta;

    if (const auto t = doc.find("title"); t != doc.end() && t->is_object()) {
        meta.title_english  = str_or_empty(*t, "english");
        meta.title_japanese = str_or_empty(*t, "japanese");
    }
    if (const auto tags = doc.find("tags"); tags != doc.end() && tags->is_array())
        for (const json& e : *tags) append_tag(e, meta.tags);
    return meta;
}

std::string meta_gallery_name(const ArchiveMeta& m, std::string_view fallback)
{
    for (const std::string* title : {&m.title_english, &m.title_japanese}) {
        std::string name = trimmed(*title);
        if (name.empty()) continue;
        std::ranges::replace(name, '/', '_');   // '/' would split the gallery path
        return name;
    }
    return std::string(fallback);
}

std::vector<std::string> meta_gallery_tags(const ArchiveMeta& m)
{
    std::vector<std::string> tags;
    if (!m.title_japanese.empty()) tags.push_back(m.title_japanese);
    tags.insert(tags.end(), m.tags.begin(), m.tags.end());
    return tags;
}

} // namespace ui
