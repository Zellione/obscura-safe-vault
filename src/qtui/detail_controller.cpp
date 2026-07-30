#include "detail_controller.h"
#include "vault/vault.h"
#include "ui/tag_inherit.h"
#include "ui/text_metrics.h"

#include <algorithm>

DetailController::DetailController(vault::Vault* vault, QObject* parent)
    : QObject(parent), vault_(vault)
{
    // Initialize metrics with default font size (12px body text)
    metrics_ = ui::detail_metrics(12.0f);
}

void DetailController::showNode(quintptr nodeKey,
                               const QStringList& inheritedTags,
                               const QStringList& fromContentsTags)
{
    if (!vault_) {
        clear();
        return;
    }

    // Recover the IndexNode* from the opaque key
    const auto* node = reinterpret_cast<const vault::IndexNode*>(nodeKey);
    if (!node) {
        clear();
        return;
    }

    // Convert QStringList to std::vector<std::string>
    std::vector<std::string> inherited, fromContents;
    for (const auto& tag : inheritedTags) {
        inherited.push_back(tag.toStdString());
    }
    for (const auto& tag : fromContentsTags) {
        fromContents.push_back(tag.toStdString());
    }

    // Get vault default sort for label generation
    const auto& settings = vault::vault_settings(*vault_);

    // Build the detail content
    content_ = ui::build_node_details(*node, inherited, fromContents, settings.default_sort);
    updateLayout();
    emit contentChanged();
}

void DetailController::showNodeWithPath(quintptr nodeKey, const QString& nodePath)
{
    if (!vault_) {
        clear();
        return;
    }

    // Recover the IndexNode* from the opaque key
    const auto* node = reinterpret_cast<const vault::IndexNode*>(nodeKey);
    if (!node) {
        clear();
        return;
    }

    // Compute inherited tags from the vault (Scope: WS2.4 — wire ui::tag_inherit)
    const auto inherited = ui::inherited_tags(*vault_, nodePath.toStdString());

    // Compute from-contents tags (only for galleries)
    std::vector<std::string> fromContents;
    if (node->is_gallery()) {
        fromContents = ui::contents_tags(*vault_, nodePath.toStdString());
    }

    // Get vault default sort for label generation
    const auto& settings = vault::vault_settings(*vault_);

    // Build the detail content
    content_ = ui::build_node_details(*node, inherited, fromContents, settings.default_sort);
    updateLayout();
    emit contentChanged();
}

void DetailController::showSelection(const QList<quintptr>& nodeKeys,
                                    const QStringList& inheritedTags)
{
    if (!vault_ || nodeKeys.empty()) {
        clear();
        return;
    }

    // Convert nodeKeys to IndexNode* pointers
    std::vector<const vault::IndexNode*> nodes;
    for (const auto key : nodeKeys) {
        const auto* node = reinterpret_cast<const vault::IndexNode*>(key);
        if (node) {
            nodes.push_back(node);
        }
    }

    if (nodes.empty()) {
        clear();
        return;
    }

    // Convert QStringList to std::vector<std::string>
    std::vector<std::string> inherited;
    for (const auto& tag : inheritedTags) {
        inherited.push_back(tag.toStdString());
    }

    // Build the multi-selection detail content
    content_ = ui::build_selection_details(nodes, inherited);
    updateLayout();
    emit contentChanged();
}

void DetailController::clear()
{
    content_ = ui::DetailContent{};
    lines_.clear();
    emit contentChanged();
}

QString DetailController::heading() const
{
    return QString::fromStdString(content_.heading);
}

QString DetailController::subheading() const
{
    return QString::fromStdString(content_.subheading);
}

QStringList DetailController::sectionTitles() const
{
    QStringList titles;
    for (const auto& section : content_.sections) {
        if (!section.title.empty()) {
            titles.append(QString::fromStdString(section.title));
        }
    }
    return titles;
}

float DetailController::totalHeight() const
{
    return ui::detail_content_height(lines_);
}

int DetailController::sectionCount() const
{
    return static_cast<int>(content_.sections.size());
}

QString DetailController::sectionTitle(int index) const
{
    if (index < 0 || index >= static_cast<int>(content_.sections.size())) {
        return QString();
    }
    return QString::fromStdString(content_.sections[index].title);
}

int DetailController::rowCount(int sectionIndex) const
{
    if (sectionIndex < 0 || sectionIndex >= static_cast<int>(content_.sections.size())) {
        return 0;
    }
    return static_cast<int>(content_.sections[sectionIndex].rows.size());
}

QString DetailController::rowLabel(int sectionIndex, int rowIndex) const
{
    if (sectionIndex < 0 || sectionIndex >= static_cast<int>(content_.sections.size())) {
        return QString();
    }
    const auto& section = content_.sections[sectionIndex];
    if (rowIndex < 0 || rowIndex >= static_cast<int>(section.rows.size())) {
        return QString();
    }
    return QString::fromStdString(section.rows[rowIndex].label);
}

QString DetailController::rowValue(int sectionIndex, int rowIndex) const
{
    if (sectionIndex < 0 || sectionIndex >= static_cast<int>(content_.sections.size())) {
        return QString();
    }
    const auto& section = content_.sections[sectionIndex];
    if (rowIndex < 0 || rowIndex >= static_cast<int>(section.rows.size())) {
        return QString();
    }
    return QString::fromStdString(section.rows[rowIndex].value);
}

int DetailController::bulletCount(int sectionIndex) const
{
    if (sectionIndex < 0 || sectionIndex >= static_cast<int>(content_.sections.size())) {
        return 0;
    }
    return static_cast<int>(content_.sections[sectionIndex].bullets.size());
}

QString DetailController::bullet(int sectionIndex, int bulletIndex) const
{
    if (sectionIndex < 0 || sectionIndex >= static_cast<int>(content_.sections.size())) {
        return QString();
    }
    const auto& section = content_.sections[sectionIndex];
    if (bulletIndex < 0 || bulletIndex >= static_cast<int>(section.bullets.size())) {
        return QString();
    }
    return QString::fromStdString(section.bullets[bulletIndex]);
}

bool DetailController::isBulletTag(int sectionIndex) const
{
    if (sectionIndex < 0 || sectionIndex >= static_cast<int>(content_.sections.size())) {
        return false;
    }
    return content_.sections[sectionIndex].is_tags;
}

void DetailController::updateLayout()
{
    // Recalculate metrics for current font size (always 12px)
    metrics_ = ui::detail_metrics(12.0f);
    // Layout the content lines
    lines_ = ui::layout_detail_lines(content_, metrics_);
}
