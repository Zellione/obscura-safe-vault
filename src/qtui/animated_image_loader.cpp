#include "animated_image_loader.h"

#include "secure_image_item.h"

AnimatedImageLoader::AnimatedImageLoader(QObject* parent)
    : QObject(parent)
{
}

AnimatedImageLoader::~AnimatedImageLoader()
{
    close();
}

void AnimatedImageLoader::open(vault::Vault* vault,
                              const vault::IndexNode* node,
                              SecureImageItem* item)
{
    close();  // clean up any previous animation

    vault_ = vault;
    node_ = node;
    item_ = item;
    currentFrameIndex_ = 0;
    active_ = true;

    // TODO (Phase 47/57): Initialize media::AnimDecoder here
    // The decoder will hold a reference to the vault's read handle
    // and the node's encrypted data.
    //
    // For now, mark active to show the wiring is ready. When the AnimController
    // emits frameAdvance signals, onFrameAdvance() will be called and can pull
    // frames from the decoder once it's implemented.
}

void AnimatedImageLoader::close()
{
    if (active_) {
        vault_ = nullptr;
        node_ = nullptr;
        item_ = nullptr;
        active_ = false;
        currentFrameIndex_ = 0;
        // TODO: Clean up media::AnimDecoder when implemented
    }
}

void AnimatedImageLoader::onFrameAdvance(int framesToAdvance)
{
    if (!active_ || !vault_ || !node_ || !item_) {
        return;
    }

    // Advance frame index
    currentFrameIndex_ += framesToAdvance;

    // TODO (Phase 47/57): Pull frame from media::AnimDecoder
    // - Decoder decodes frame at index currentFrameIndex_
    // - Returns pixel data as PixelBuffer (mlock'd, wiped after upload)
    // - Call item_->setImage(pixelBuffer)
    //
    // Pseudocode (Phase 47/57 implementation):
    //   auto pixels = decoder_->decode_frame(currentFrameIndex_);
    //   if (pixels) {
    //       item_->setImage(pixels);
    //   }
}
