#include "test_framework.h"

#include "media/video_decoder.h"

TEST(display_dims_anamorphic_pal_16_9) {
    auto [w, h] = media::display_dims(720, 576, 64, 45);  // PAL 16:9 SAR
    CHECK(h == 576);
    CHECK(w == 1024);  // round(720*64/45)
}

TEST(display_dims_square_is_identity) {
    auto [w, h] = media::display_dims(640, 480, 1, 1);
    CHECK(w == 640);
    CHECK(h == 480);
}

TEST(display_dims_zero_or_negative_sar_is_identity) {
    {
        auto [w, h] = media::display_dims(640, 480, 0, 1);
        CHECK(w == 640);
        CHECK(h == 480);
    }
    {
        auto [w, h] = media::display_dims(640, 480, 1, 0);
        CHECK(w == 640);
        CHECK(h == 480);
    }
}

TEST(display_dims_negative_sar_num_is_identity) {
    auto [w, h] = media::display_dims(640, 480, -1, 1);
    CHECK(w == 640);
    CHECK(h == 480);
}

TEST(display_dims_anamorphic_pal_4_3) {
    auto [w, h] = media::display_dims(720, 576, 59, 54);  // PAL 4:3 SAR
    CHECK(h == 576);
    // round(720 * 59 / 54) = round(786.666...) = 787
    CHECK(w == 787);
}

TEST(display_dims_anamorphic_ntsc_16_9) {
    auto [w, h] = media::display_dims(720, 480, 40, 33);  // NTSC 16:9 SAR
    CHECK(h == 480);
    // round(720 * 40 / 33) = round(872.727...) = 873
    CHECK(w == 873);
}
