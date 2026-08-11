#include "ui/dual_transfer.h"

#include <algorithm>

namespace ui {

void DualTransferPrompt::open(std::string dst_label, std::vector<std::string> conflicts)
{
    stage_      = Stage::Mode;
    selected_   = 0;
    dst_label_  = std::move(dst_label);
    conflicts_  = std::move(conflicts);
}

DualTransferPrompt::Launch DualTransferPrompt::key(Key k)
{
    Launch result;
    result.mode   = vault::TransferMode::Move;
    result.policy = vault::CollisionPolicy::Fail;
    result.fire   = false;

    if (stage_ == Stage::Mode) mode_key(k, result);
    else if (stage_ == Stage::Conflict) conflict_key(k, result);
    return result;
}

// Mode stage: 3 rows = {"Move to <dst>", "Copy to <dst>", "Cancel"}
void DualTransferPrompt::mode_key(Key k, Launch& result)
{
    switch (k) {
        case Key::Up:    selected_ = std::max(0, selected_ - 1); break;
        case Key::Down:  selected_ = std::min(2, selected_ + 1); break;
        case Key::Esc:   stage_ = Stage::Closed; break;
        case Key::Enter: mode_enter(result); break;
    }
}

void DualTransferPrompt::mode_enter(Launch& result)
{
    if (selected_ == 2) {  // Cancel row
        stage_ = Stage::Closed;
        return;
    }
    mode_       = selected_ == 0 ? vault::TransferMode::Move : vault::TransferMode::Copy;
    result.mode = mode_;
    if (conflicts_.empty()) {
        result.policy = vault::CollisionPolicy::Fail;
        result.fire   = true;
        stage_        = Stage::Closed;
    } else {
        // Transition to Conflict stage
        stage_    = Stage::Conflict;
        selected_ = 0;  // reset selection for conflict stage
    }
}

// Conflict stage: 3 rows = {"Combine into existing", "Rename (_2)", "Cancel"}
void DualTransferPrompt::conflict_key(Key k, Launch& result)
{
    switch (k) {
        case Key::Up:    selected_ = std::max(0, selected_ - 1); break;
        case Key::Down:  selected_ = std::min(2, selected_ + 1); break;
        case Key::Esc:
            // Return to Mode stage, reset selection
            stage_    = Stage::Mode;
            selected_ = 0;
            break;
        case Key::Enter: conflict_enter(result); break;
    }
}

void DualTransferPrompt::conflict_enter(Launch& result)
{
    if (selected_ == 2) {  // Cancel row
        stage_ = Stage::Closed;
        return;
    }
    result.mode   = mode_;  // mode chosen in Mode stage
    result.policy = selected_ == 0 ? vault::CollisionPolicy::Combine : vault::CollisionPolicy::Suffix;
    result.fire   = true;
    stage_        = Stage::Closed;
}

} // namespace ui
