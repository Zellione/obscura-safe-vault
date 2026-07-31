#pragma once

#include <functional>
#include <QObject>
#include <QList>
#include <QString>

#include "ui/selection_model.h"

// QObject adapter over ui::SelectionModel, name-keyed for Phase 58 selection
// survival across model refetches. Interface adapted from SDL ui with Ctrl+A
// rules per Phase 53.
class SelectionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit SelectionController(QObject* parent = nullptr);

    // Row→name lookup callback for name-keyed selection.
    // Called by toggle() and rangeSelectTo() to map row indices to names.
    using NameLookupFn = std::function<QString(int row)>;
    void setNameLookup(NameLookupFn lookup) { nameLookup_ = lookup; }

    // Phase 53 rules:
    // - toggle(row): add row if absent, remove if present
    // - toggleAll(count): if count <= 0 or all [0,count) selected, clear;
    //   else select all [0,count)
    // - rangeSelectTo(row): select range from last selected to row (inclusive)
    // - clear(): drop all selections

    [[nodiscard]] int count() const { return static_cast<int>(model_.count()); }
    // Q_INVOKABLE: called from QML (tile borders, getSelectedNodeKeys) — a plain
    // public method is not callable from QML and silently breaks selection UI.
    [[nodiscard]] Q_INVOKABLE bool isSelected(int row) const { return model_.contains(row); }

    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void toggleAll(int count);
    Q_INVOKABLE void rangeSelectTo(int row);
    Q_INVOKABLE void clear();

    // QML-exposed getters
    Q_INVOKABLE QList<QString> selectedNames() const;

signals:
    void countChanged();
    void selectionChanged();

private:
    ui::SelectionModel model_;
    NameLookupFn nameLookup_;
    int lastSelectedRow_ = -1;  // For range selection context
};
