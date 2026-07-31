#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include <span>

#include "ui/cover_cache.h"

namespace vault {
    struct IndexNode;
}

// Cover provider for gallery tiles (Task 2.4).
// Wraps ui::CoverCache to provide cover thumbnail spans for gallery nodes.
// Each cover is resolved once per listing and memoised until clear().
// Renders via existing SecureImageItem/ThumbCache delegate path.
class CoverProvider : public QObject {
    Q_OBJECT

public:
    explicit CoverProvider(QObject* parent = nullptr);

    // Get covers for a gallery node (returns thumbnail chunk spans for rendering).
    // Returns empty span if the gallery is empty (caller falls back to folder icon).
    // Must be called on main thread only.
    [[nodiscard]] std::span<const ui::CoverSpan> getCovers(const vault::IndexNode* gallery);

    // Clear memoised covers on refresh or vault change.
    // Must be called before any nodes are accessed after tree mutations.
    void clear() noexcept;

private:
    ui::CoverCache cache_;
};
