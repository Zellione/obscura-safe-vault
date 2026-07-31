#include "animated_image_loader.h"

#include <memory>
#include <optional>

#include "secure_image_item.h"
#include "vault/index.h"
#include "vault/vault.h"
#include "crypto/secure_mem.h"
#include "media/anim_decoder.h"
#include "media/webp_anim_decoder.h"

#ifdef OSV_VENDORED_AV
#include "media/gif_decoder.h"
#endif

namespace {

// Picks the decoder for a node's format. GIF needs vendored FFmpeg; WebP does
// not, so an animated WebP plays in every build. Any other format returns
// nullptr.
std::unique_ptr<media::AnimDecoder> make_decoder(vault::ImageFormat format)
{
    switch (format) {
    case vault::ImageFormat::WebP:
        return std::make_unique<media::WebpAnimDecoder>();
#ifdef OSV_VENDORED_AV
    case vault::ImageFormat::GIF:
        return std::make_unique<media::GifDecoder>();
#endif
    default:
        return nullptr;
    }
}

}  // namespace

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

    if (!vault || !node || !item) {
        return;
    }

    vault_ = vault;
    node_ = node;
    item_ = item;
    currentFrameIndex_ = 0;
    totalFrames_ = 0;

    // Create decoder for this format
    decoder_ = make_decoder(node->meta.format);
    if (!decoder_) {
        // Format not supported for animation (e.g., GIF without vendored FFmpeg)
        return;
    }

    // Read the decrypted image into mlock'd SecureBytes
    auto bytes = std::make_unique<crypto::SecureBytes>();
    if (vault->read_image(*node, *bytes) != vault::VaultResult::Ok) {
        decoder_.reset();
        return;
    }

    // Open the decoder, which borrows bytes (must stay alive)
    if (!decoder_->open(bytes->as_span())) {
        decoder_.reset();
        return;
    }

    // Decode the first frame to validate
    auto f = decoder_->next_frame();
    if (!f) {
        decoder_.reset();
        return;
    }

    // A second frame confirms it really animates
    if (auto f2 = decoder_->next_frame(); !f2) {
        decoder_.reset();
        return;
    }

    // Rewind so playback starts from the top
    decoder_->rewind();

    // Extract frame count (decoder has decoded at least 2 frames)
    totalFrames_ = decoder_->frames_decoded();

    // Save the bytes (decoder borrows it)
    bytes_ = std::move(bytes);

    active_ = true;
    currentFrameIndex_ = 0;

    // Display the first frame immediately
    auto firstFrame = decoder_->next_frame();
    if (firstFrame) {
        auto px = std::make_shared<PixelBuffer>();
        px->width = firstFrame->width;
        px->height = firstFrame->height;
        px->rgba = std::move(firstFrame->rgba);
        // Flatten transparent areas to black (Phase 47 Task 10: alpha composition)
        flatten_alpha_to_black(*px);
        item_->setImage(px);
        currentFrameIndex_ = 1;
    }
}

void AnimatedImageLoader::close()
{
    active_ = false;
    vault_ = nullptr;
    node_ = nullptr;
    item_ = nullptr;
    currentFrameIndex_ = 0;
    totalFrames_ = 0;

    // Destroy decoder first (it borrows bytes_)
    decoder_.reset();
    // Then destroy bytes_ (will be crypto_wipe'd by SecureBytes destructor)
    bytes_.reset();
}

void AnimatedImageLoader::onFrameAdvance(int framesToAdvance)
{
    if (!active_ || !vault_ || !node_ || !item_ || !decoder_) {
        return;
    }

    // Advance through the requested frames
    for (int i = 0; i < framesToAdvance; ++i) {
        auto f = decoder_->next_frame();
        if (!f) {
            // End of stream: rewind and loop
            decoder_->rewind();
            f = decoder_->next_frame();
            if (!f) {
                // Undecodable on rewind: stop advancing
                return;
            }
            currentFrameIndex_ = 1;
        } else {
            ++currentFrameIndex_;
        }

        // Wrap frame count for display
        if (totalFrames_ > 0) {
            currentFrameIndex_ = currentFrameIndex_ % (totalFrames_ + 1);
            if (currentFrameIndex_ == 0) {
                currentFrameIndex_ = 1;
            }
        }

        // Convert AnimFrame to PixelBuffer and update SecureImageItem
        auto px = std::make_shared<PixelBuffer>();
        px->width = f->width;
        px->height = f->height;
        px->rgba = std::move(f->rgba);
        // Flatten transparent areas to black (Phase 47 Task 10: alpha composition)
        flatten_alpha_to_black(*px);
        item_->setImage(px);
    }
}
