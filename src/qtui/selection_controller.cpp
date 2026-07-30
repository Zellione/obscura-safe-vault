#include "selection_controller.h"

SelectionController::SelectionController(QObject* parent) : QObject(parent)
{
}

void SelectionController::toggle(int row)
{
    const uint64_t before_revision = model_.revision();

    model_.toggle(row);
    lastSelectedRow_ = row;

    if (model_.revision() != before_revision) {
        emit countChanged();
        emit selectionChanged();
    }
}

void SelectionController::toggleAll(int count)
{
    if (count <= 0) {
        // Empty gallery: inert (Phase 53 rule)
        return;
    }

    const uint64_t before_revision = model_.revision();

    // If all [0, count) are selected, clear; else select all
    if (model_.all_selected(count)) {
        model_.clear();
    } else {
        model_.select_all(count);
    }

    if (model_.revision() != before_revision) {
        emit countChanged();
        emit selectionChanged();
    }
}

void SelectionController::rangeSelectTo(int row)
{
    if (lastSelectedRow_ < 0) {
        // No anchor yet, just toggle this row
        toggle(row);
        return;
    }

    const uint64_t before_revision = model_.revision();

    int start = std::min(lastSelectedRow_, row);
    int end = std::max(lastSelectedRow_, row);

    for (int i = start; i <= end; ++i) {
        if (!model_.contains(i)) {
            model_.toggle(i);
        }
    }

    lastSelectedRow_ = row;

    if (model_.revision() != before_revision) {
        emit countChanged();
        emit selectionChanged();
    }
}

void SelectionController::clear()
{
    if (model_.empty()) {
        return;  // Already empty
    }

    model_.clear();
    lastSelectedRow_ = -1;

    emit countChanged();
    emit selectionChanged();
}

QList<QString> SelectionController::selectedNames() const
{
    QList<QString> names;

    if (!nameLookup_) {
        return names;  // No lookup available
    }

    // Get selected indices in order
    const auto indices = model_.indices();
    for (int idx : indices) {
        QString name = nameLookup_(idx);
        if (!name.isEmpty()) {
            names.append(name);
        }
    }

    return names;
}
