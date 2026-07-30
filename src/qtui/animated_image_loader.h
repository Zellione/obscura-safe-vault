#pragma once

#include <QObject>
#include <memory>
#include <span>

#include "anim_controller.h"
#include "pixel_buffer.h"

namespace vault {
    class Vault;
    struct IndexNode;
}

class SecureImageItem;

// Bridge between AnimController frame timing and image decoding.
//
// When an animated image is opened in the viewer:
// 1. Create an AnimatedImageLoader with vault + node + target SecureImageItem
// 2. AnimController is wired to this loader's frameAdvance() slot
// 3. On each frameAdvance signal, pull and decode the next frame
// 4. Update the SecureImageItem with the new PixelBuffer
//
// This separates frame timing (AnimController) from frame production (this loader).
//
// Current implementation: placeholder that defines the wiring contract.
// Full implementation requires media::AnimDecoder integration (Phase 47/57).
class AnimatedImageLoader : public QObject {
    Q_OBJECT

public:
    explicit AnimatedImageLoader(QObject* parent = nullptr);
    ~AnimatedImageLoader();

    // Bind vault + node + target item for decoding animated frames
    void open(vault::Vault* vault,
              const vault::IndexNode* node,
              SecureImageItem* item);

    // Close and clean up decoder
    void close();

    // Called by AnimController when a new frame should be displayed
    // (connected to AnimController::frameAdvance signal in ViewerScreen)
    Q_SLOT void onFrameAdvance(int framesToAdvance);

    // True if currently decoding an animation
    [[nodiscard]] bool isActive() const { return active_; }

    // Current frame number (for testing/debug)
    [[nodiscard]] int currentFrameIndex() const { return currentFrameIndex_; }

private:
    vault::Vault* vault_ = nullptr;
    const vault::IndexNode* node_ = nullptr;
    SecureImageItem* item_ = nullptr;
    bool active_ = false;
    int currentFrameIndex_ = 0;
    // TODO: Add media::AnimDecoder member when Phase 47/57 integration is ready
};
