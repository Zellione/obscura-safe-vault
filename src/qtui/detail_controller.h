#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>
#include <span>

#include "ui/detail_model.h"
#include "ui/detail_layout.h"

namespace vault {
    class Vault;
    struct IndexNode;
}

// QML wrapper over ui::detail_model and ui::detail_layout (Phase 48).
// Provides single-node and multi-selection detail rendering for the gallery detail panel.
// Threading: main-thread-only; workers never hold DetailController pointers.
class DetailController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString heading READ heading NOTIFY contentChanged)
    Q_PROPERTY(QString subheading READ subheading NOTIFY contentChanged)
    Q_PROPERTY(QStringList sectionTitles READ sectionTitles NOTIFY contentChanged)
    Q_PROPERTY(float totalHeight READ totalHeight NOTIFY contentChanged)
public:
    explicit DetailController(vault::Vault* vault, QObject* parent = nullptr);

    // Show details for a single node
    Q_INVOKABLE void showNode(quintptr nodeKey,
                             const QStringList& inheritedTags,
                             const QStringList& fromContentsTags);

    // Show details for a single node, computing inherited/from-contents tags from vault (Scope: WS2.4)
    Q_INVOKABLE void showNodeWithPath(quintptr nodeKey, const QString& nodePath);

    // Show details for a multi-selection of nodes
    Q_INVOKABLE void showSelection(const QList<quintptr>& nodeKeys,
                                  const QStringList& inheritedTags);

    // Clear all content
    Q_INVOKABLE void clear();

    // Property accessors
    [[nodiscard]] QString heading() const;
    [[nodiscard]] QString subheading() const;
    [[nodiscard]] QStringList sectionTitles() const;
    [[nodiscard]] float totalHeight() const;

    // For QML rendering: access section titles and content via model-like interface
    Q_INVOKABLE int sectionCount() const;
    Q_INVOKABLE QString sectionTitle(int index) const;
    Q_INVOKABLE int rowCount(int sectionIndex) const;
    Q_INVOKABLE QString rowLabel(int sectionIndex, int rowIndex) const;
    Q_INVOKABLE QString rowValue(int sectionIndex, int rowIndex) const;
    Q_INVOKABLE int bulletCount(int sectionIndex) const;
    Q_INVOKABLE QString bullet(int sectionIndex, int bulletIndex) const;
    Q_INVOKABLE bool isBulletTag(int sectionIndex) const;

signals:
    void contentChanged();

private:
    vault::Vault* vault_;
    ui::DetailContent content_;
    std::vector<ui::DetailLine> lines_;
    ui::DetailMetrics metrics_;

    void updateLayout();
};
