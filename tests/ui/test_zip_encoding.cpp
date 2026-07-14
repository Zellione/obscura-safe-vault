#include "test_framework.h"
#include "ui/zip_encoding.h"

#include <string>

using ui::decode_zip_entry_name;

TEST(zip_encoding_utf8_flag_passes_bytes_through_unchanged)
{
    // Bit 11 set => the entry declares itself UTF-8 already; trust it verbatim,
    // even though these particular bytes would also happen to decode validly.
    const std::string raw = "caf\xC3\xA9.jpg";  // "café.jpg" as UTF-8
    CHECK_EQ(decode_zip_entry_name(raw, /*utf8_flag=*/true), raw);
}

TEST(zip_encoding_ascii_only_name_is_unchanged_without_flag)
{
    const std::string raw = "holiday_photo.jpg";
    CHECK_EQ(decode_zip_entry_name(raw, /*utf8_flag=*/false), raw);
}

TEST(zip_encoding_valid_utf8_without_flag_is_detected_and_kept)
{
    // Some tools write UTF-8 bytes but never set the flag. Re-decoding these
    // bytes as CP437 would produce mojibake ("Ã©" instead of "é"), so a name
    // that is already valid UTF-8 must be preferred over the CP437 fallback.
    const std::string raw = "caf\xC3\xA9.jpg";  // "café.jpg" as UTF-8, no flag
    CHECK_EQ(decode_zip_entry_name(raw, /*utf8_flag=*/false), raw);
}

TEST(zip_encoding_cp437_accented_letter_decodes_correctly)
{
    // CP437 0x82 == U+00E9 (é). Without the UTF-8 flag and not valid UTF-8 on
    // its own, this must be decoded via the CP437 table.
    const std::string raw = std::string("caf") + '\x82' + ".jpg";
    CHECK_EQ(decode_zip_entry_name(raw, /*utf8_flag=*/false), "caf\xC3\xA9.jpg");
}

TEST(zip_encoding_cp437_multiple_accented_letters)
{
    // CP437 0x8E == U+00C4 (Ä), 0x84 == U+00E4 (ä).
    const std::string raw = std::string("M\x8E") + "dchen_\x84nglich.jpg";
    CHECK_EQ(decode_zip_entry_name(raw, /*utf8_flag=*/false),
             "M\xC3\x84""dchen_\xC3\xA4nglich.jpg");
}

TEST(zip_encoding_cp437_box_drawing_and_symbol_bytes)
{
    // CP437 0xF8 == U+00B0 (degree sign), 0xB3 == U+2502 (box-drawing vertical).
    const std::string raw = std::string("temp_30\xF8""C_\xB3""chart.jpg");
    CHECK_EQ(decode_zip_entry_name(raw, /*utf8_flag=*/false),
             "temp_30\xC2\xB0"
             "C_\xE2\x94\x82"
             "chart.jpg");
}
