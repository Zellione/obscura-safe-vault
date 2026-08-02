#include "ui/dup_scan.h"

#include "vault/vault.h"

namespace ui {

namespace {

void walk(const vault::Vault& v, const std::string& path,
          std::vector<DupScanItem>& out)
{
    for (const vault::IndexNode* n : v.list(path)) {
        const std::string child = path.empty() ? n->name : path + "/" + n->name;
        if (n->is_gallery()) {
            walk(v, child, out);
            continue;
        }
        DupScanItem it;
        it.node_path   = child;
        it.name        = n->name;
        it.parent_path = path;
        it.is_video    = n->is_video();
        if (n->is_image()) {
            it.bytes        = n->meta.orig_size;
            it.width        = n->meta.width;
            it.height       = n->meta.height;
            it.data_spans   = {{n->meta.data_offset, n->meta.data_length}};
            it.thumb_offset = n->meta.thumb_offset;
            it.thumb_length = n->meta.thumb_length;
        } else {
            it.bytes  = n->vmeta.orig_size;
            it.width  = n->vmeta.width;
            it.height = n->vmeta.height;
            it.data_spans.reserve(n->vmeta.chunks.size());
            for (const vault::VideoChunk& c : n->vmeta.chunks)
                it.data_spans.emplace_back(c.offset, c.length);
            it.thumb_offset = n->vmeta.poster_offset;
            it.thumb_length = n->vmeta.poster_length;
        }
        out.push_back(std::move(it));
    }
}

} // namespace

std::vector<DupScanItem> collect_scan_items(const vault::Vault& v)
{
    std::vector<DupScanItem> out;
    walk(v, "", out);
    return out;
}

} // namespace ui
