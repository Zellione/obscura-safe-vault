#include "status_controller.h"

#include <QtCore/qloggingcategory.h>

Q_LOGGING_CATEGORY(lcStatusController, "osv.status_controller")

StatusController::StatusController(QObject* parent)
    : QObject(parent)
{
    qCDebug(lcStatusController) << "StatusController constructed";
}

StatusController::~StatusController()
{
    qCDebug(lcStatusController) << "StatusController destroyed";
}

void StatusController::set(int kind, QString text)
{
    qCDebug(lcStatusController) << "set(" << kind << ", '" << text << "')";

    // Store text for this kind (empty text clears it)
    switch (kind) {
    case Normal:
        normalText_ = text;
        break;
    case Import:
        importText_ = text;
        break;
    case Error:
        errorText_ = text;
        break;
    default:
        qCWarning(lcStatusController) << "Invalid kind:" << kind;
        return;
    }

    updateResolution();
}

void StatusController::clearKind(int kind)
{
    qCDebug(lcStatusController) << "clearKind(" << kind << ")";

    // Only clear if the text is non-empty
    switch (kind) {
    case Normal:
        if (!normalText_.isEmpty()) {
            normalText_.clear();
            updateResolution();
        }
        break;
    case Import:
        if (!importText_.isEmpty()) {
            importText_.clear();
            updateResolution();
        }
        break;
    case Error:
        if (!errorText_.isEmpty()) {
            errorText_.clear();
            updateResolution();
        }
        break;
    default:
        qCWarning(lcStatusController) << "Invalid kind:" << kind;
        break;
    }
}

void StatusController::updateResolution()
{
    QString newResolvedText;
    int newResolvedKind = Normal;

    // Priority: error > import > normal
    if (!errorText_.isEmpty()) {
        newResolvedText = errorText_;
        newResolvedKind = Error;
    } else if (!importText_.isEmpty()) {
        newResolvedText = importText_;
        newResolvedKind = Import;
    } else if (!normalText_.isEmpty()) {
        newResolvedText = normalText_;
        newResolvedKind = Normal;
    }
    // else: newResolvedText stays empty, newResolvedKind is Normal

    // Update and notify if changed
    bool textChanged = (newResolvedText != resolvedText_);
    bool kindChanged = (newResolvedKind != resolvedKind_);

    resolvedText_ = newResolvedText;
    resolvedKind_ = newResolvedKind;

    if (textChanged) {
        qCDebug(lcStatusController) << "Resolved text changed to '" << newResolvedText << "'";
        emit this->textChanged();
    }

    if (kindChanged) {
        qCDebug(lcStatusController) << "Resolved kind changed to" << newResolvedKind;
        emit this->kindChanged();
    }
}
