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

    if (stage_ == Stage::Closed) return result;

    // Mode stage: 3 rows = {"Move to <dst>", "Copy to <dst>", "Cancel"}
    if (stage_ == Stage::Mode) {
        switch (k) {
            case Key::Up:
                selected_ = std::max(0, selected_ - 1);
                return result;
            case Key::Down:
                selected_ = std::min(2, selected_ + 1);
                return result;
            case Key::Esc:
                stage_ = Stage::Closed;
                return result;
            case Key::Enter:
                if (selected_ == 0) {
                    // Move row
                    mode_       = vault::TransferMode::Move;
                    result.mode = vault::TransferMode::Move;
                    if (conflicts_.empty()) {
                        result.policy = vault::CollisionPolicy::Fail;
                        result.fire   = true;
                        stage_        = Stage::Closed;
                    } else {
                        // Transition to Conflict stage
                        stage_    = Stage::Conflict;
                        selected_ = 0;  // reset selection for conflict stage
                    }
                } else if (selected_ == 1) {
                    // Copy row
                    mode_       = vault::TransferMode::Copy;
                    result.mode = vault::TransferMode::Copy;
                    if (conflicts_.empty()) {
                        result.policy = vault::CollisionPolicy::Fail;
                        result.fire   = true;
                        stage_        = Stage::Closed;
                    } else {
                        // Transition to Conflict stage
                        stage_    = Stage::Conflict;
                        selected_ = 0;  // reset selection for conflict stage
                    }
                } else if (selected_ == 2) {
                    // Cancel row
                    stage_ = Stage::Closed;
                }
                return result;
        }
    }

    // Conflict stage: 3 rows = {"Combine into existing", "Rename (_2)", "Cancel"}
    if (stage_ == Stage::Conflict) {
        switch (k) {
            case Key::Up:
                selected_ = std::max(0, selected_ - 1);
                return result;
            case Key::Down:
                selected_ = std::min(2, selected_ + 1);
                return result;
            case Key::Esc:
                // Return to Mode stage, reset selection
                stage_    = Stage::Mode;
                selected_ = 0;
                return result;
            case Key::Enter:
                if (selected_ == 0) {
                    // Combine row
                    result.mode   = mode_;  // mode chosen in Mode stage
                    result.policy = vault::CollisionPolicy::Combine;
                    result.fire   = true;
                    stage_        = Stage::Closed;
                } else if (selected_ == 1) {
                    // Rename row
                    result.mode   = mode_;  // mode chosen in Mode stage
                    result.policy = vault::CollisionPolicy::Suffix;
                    result.fire   = true;
                    stage_        = Stage::Closed;
                } else if (selected_ == 2) {
                    // Cancel row
                    stage_ = Stage::Closed;
                }
                return result;
        }
    }

    return result;
}

} // namespace ui
