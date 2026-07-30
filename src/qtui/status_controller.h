#pragma once

#include <QObject>
#include <QString>

// Controller for the footer status bar with priority-based message resolution.
//
// Maintains three independent per-kind text stores (normal, import, error).
// The displayed text and kind are determined by priority: error > import > normal.
//
// Threading: GUI-thread only (no locking needed; calls are Q_INVOKABLE and signal
// resolution happens synchronously in the same thread).
//
// Signal emission:
// - textChanged: emitted when the resolved text changes
// - kindChanged: emitted when the resolved kind changes
// Both signals fire on every set/clearKind, even if the resolved value doesn't change
// (QML property notifications use onChange semantics anyway).
class StatusController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(int kind READ kind NOTIFY kindChanged)

public:
    // Message kinds with priority: error (2) > import (1) > normal (0)
    enum MessageKind {
        Normal = 0,
        Import = 1,
        Error = 2,
    };

    explicit StatusController(QObject* parent = nullptr);
    ~StatusController();

    // Set text for a given kind. Empty text clears that kind.
    // Emits textChanged and kindChanged signals.
    Q_INVOKABLE void set(int kind, QString text);

    // Clear text for a given kind (no-op if already empty).
    // Emits textChanged and kindChanged signals if the resolved text changes.
    Q_INVOKABLE void clearKind(int kind);

    // Read-only properties
    [[nodiscard]] QString text() const { return resolvedText_; }
    [[nodiscard]] int kind() const { return resolvedKind_; }

signals:
    void textChanged();
    void kindChanged();

private:
    // Recompute resolved text and kind from per-kind stores
    void updateResolution();

    QString normalText_;
    QString importText_;
    QString errorText_;

    QString resolvedText_;
    int resolvedKind_ = Normal;
};
