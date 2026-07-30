#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Model for the F1 help popup, exposing help content to QML.
//
// Manages:
// - A synthesized "Global" group with F1/F2/Right-click entries (always first)
// - Screen-specific help groups appended after Global
//
// Data format: QVariantList of {title, entries:[{keys, description}]}
//
// Threading: GUI-thread only (no locking needed; calls are Q_INVOKABLE).
class HelpModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)

public:
    explicit HelpModel(QObject* parent = nullptr);
    ~HelpModel();

    // Set the screen-specific help groups. They appear after the synthesized Global group.
    // Screen groups format: [{title, entries:[{keys, description}]}]
    Q_INVOKABLE void setScreenGroups(const QVariantList& screenGroups);

    // Read-only property: all groups (Global prepended + screen groups)
    [[nodiscard]] QVariantList groups() const { return allGroups_; }

signals:
    void groupsChanged();

private:
    // Rebuild allGroups_ from the current screenGroups_
    void updateGroups();

    // Create the synthesized Global group
    [[nodiscard]] QVariantMap createGlobalGroup() const;

    QVariantList screenGroups_;
    QVariantList allGroups_;
};
