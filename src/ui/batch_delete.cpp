#include "ui/batch_delete.h"

#include <format>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/delete_summary.h"
#include "ui/meta_format.h"
#include "vault/vault.h"

namespace ui {

std::vector<std::string> prune_descendant_paths(std::vector<std::string> paths)
{
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const std::string& p : paths) {
        bool inside = false;
        for (const std::string& q : paths) {
            if (&p != &q && p.size() > q.size() + 1 && p[q.size()] == '/' &&
                p.compare(0, q.size(), q) == 0) {
                inside = true;
                break;
            }
        }
        if (!inside) out.push_back(p);
    }
    return out;
}

BatchDeleteSummary summarize_batch_delete(const vault::Vault& v,
                                          std::span<const std::string> paths)
{
    BatchDeleteSummary s;
    for (const std::string& p : paths) {
        const vault::IndexNode* n = v.resolve_node(p);
        if (!n || p.empty()) continue;   // already gone / root — a no-op to delete
        ++s.top_level;
        if (n->is_gallery()) {
            SubtreeCounts c;
            count_subtree(*n, c);
            s.galleries  += 1 + c.galleries;
            s.images     += c.images;
            s.videos     += c.videos;
            s.bytes      += c.bytes;
            s.item_total += c.images + c.videos + c.galleries + 1;
        } else {
            n->is_video() ? ++s.videos : ++s.images;
            s.bytes      += n->is_video() ? n->vmeta.orig_size : n->meta.orig_size;
            s.item_total += 1;
        }
    }
    return s;
}

std::string batch_delete_counts_line(const BatchDeleteSummary& s)
{
    std::string line;
    // Noun pairs, not a naive "+s" — gallery/galleries has an irregular plural.
    const auto append = [&line](int n, const char* one, const char* many) {
        if (n == 0) return;
        if (!line.empty()) line += " · ";
        line += std::format("{} {}", n, n == 1 ? one : many);
    };
    append(s.galleries, "gallery", "galleries");
    append(s.images, "image", "images");
    append(s.videos, "video", "videos");
    if (!line.empty()) line += " · ";
    line += format_size(s.bytes);
    return line;
}

void draw_batch_delete_confirm(gfx::Renderer& r, gfx::FontAtlas& font,
                               float W, float H, const BatchDeleteSummary& s)
{
    using namespace gfx::theme;

    r.draw_rect({0, 0, W, H}, gfx::Color{8, 9, 12, 255});   // veil

    const float pw = 560;
    const float ph = 200;
    const float px = (W - pw) / 2;
    const float py = (H - ph) / 2;
    r.draw_round_rect({px, py, pw, ph}, RADIUS, SURFACE);
    r.draw_round_rect({px, py, pw, ph}, RADIUS, DANGER, /*filled*/ false);

    const auto centered = [&](const std::string& text, float y, gfx::Color c) {
        const auto tw = static_cast<float>(font.measure(text));
        r.draw_text(font, px + (pw - tw) / 2, y, text, c);
    };

    centered(std::format("Delete {} selected {}?", s.top_level,
                         s.top_level == 1 ? "item" : "items"), py + 28, TEXT);
    centered(s.galleries > 0
                 ? "This permanently removes them — galleries with everything in them."
                 : "This permanently removes them from the vault.",
             py + 72, DANGER);
    centered(batch_delete_counts_line(s), py + 104, DANGER);
    centered("[Esc/N] Cancel        [Y] Delete", py + ph - 50, TEXT_DIM);
}

}  // namespace ui
