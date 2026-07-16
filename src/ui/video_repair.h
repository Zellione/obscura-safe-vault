#pragma once

#include <span>
#include <string_view>

namespace vault {
class Vault;
struct IndexNode;
}  // namespace vault

namespace ui {

// Best-effort re-probe of every video in `children` (already vault.list()'d
// from `gallery_path`) whose codec metadata is still Unknown — the state
// add_video() leaves a video in when its codec couldn't be decoded at import
// time (e.g. an AV1 .webm imported before AV1 decode support existed).
// Fills in codec/dimensions/duration/poster via Vault::repair_video_metadata()
// wherever the codec is now decodable; silently leaves still-undecodable
// videos unchanged. Called from GalleryGrid::refresh() so previously-imported
// videos self-heal on the next gallery visit, with no separate migration step.
void repair_unknown_video_metadata(vault::Vault& vault, std::string_view gallery_path,
                                    std::span<const vault::IndexNode* const> children);

}  // namespace ui
