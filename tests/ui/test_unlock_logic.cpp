#include "test_framework.h"

#include <string>
#include <string_view>
#include <vector>
#include "vault/legacy_converter.h"

#include "ui/unlock_logic.h"

static std::span<const uint8_t> b(std::string_view s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

TEST(unlock_empty_password_rejected)
{
    auto d = ui::decide_submit(false, b(""), b(""));
    CHECK(d.action == ui::SubmitAction::None);
    CHECK(d.error != nullptr);
}

TEST(unlock_nonempty_is_unlock)
{
    auto d = ui::decide_submit(false, b("pw"), b(""));
    CHECK(d.action == ui::SubmitAction::Unlock);
    CHECK(d.error == nullptr);
}

TEST(create_mismatch_rejected)
{
    auto d = ui::decide_submit(true, b("pw1"), b("pw2"));
    CHECK(d.action == ui::SubmitAction::None);
    CHECK(d.error != nullptr);
}

TEST(create_match_is_create)
{
    auto d = ui::decide_submit(true, b("same"), b("same"));
    CHECK(d.action == ui::SubmitAction::Create);
    CHECK(d.error == nullptr);
}

TEST(conversion_failures_explain_recovery_without_claiming_corruption)
{
    using enum vault::LegacyConversionStatus;
    vault::LegacyConversionReport report;
    report.status = InsufficientSpace;
    report.required_free_bytes = 104857600;
    CHECK(ui::conversion_error_message(report).find("100 MiB") != std::string::npos);
    report.status = AuthenticationFailed;
    CHECK(ui::conversion_error_message(report).find("destination password") != std::string::npos);
    report.status = ResumeMismatch;
    CHECK(ui::conversion_error_message(report).find("another folder") != std::string::npos);
    report.status = SourceCorrupt;
    CHECK(ui::conversion_error_message(report).find("source") != std::string::npos);
    report.status = IoError;
    CHECK(ui::conversion_error_message(report).find("permissions") != std::string::npos);
    report.status = VerificationFailed;
    CHECK(ui::conversion_error_message(report).find("Keep the original") != std::string::npos);
}

TEST(conversion_statuses_have_distinct_messages)
{
    std::vector<std::string> messages;
    for (int value = 1; value <= static_cast<int>(vault::LegacyConversionStatus::InsufficientSpace);
         ++value) {
        vault::LegacyConversionReport report;
        report.status = static_cast<vault::LegacyConversionStatus>(value);
        const auto message = ui::conversion_error_message(report);
        CHECK(!message.empty());
        for (const auto& previous : messages)
            CHECK(message != previous);
        messages.push_back(message);
    }
    vault::LegacyConversionReport success;
    success.status = vault::LegacyConversionStatus::Ok;
    CHECK(ui::conversion_error_message(success).empty());
}

TEST(conversion_form_keeps_labels_and_inputs_separate_at_different_font_sizes)
{
    for (const float font : {20.0f, 28.0f, 36.0f}) {
        for (const float width : {800.0f, 1280.0f, 2560.0f}) {
            const auto form = ui::conversion_form_layout(width, font);
            const auto password = form.password_field(false);
            const auto confirm = form.password_field(true);
            CHECK(form.row(6) + font <= password.y);
            CHECK(password.y + password.h <= form.row(8));
            CHECK(password.x + password.w < confirm.x);
            CHECK(confirm.x + confirm.w <= width - 60);
            CHECK(password.h >= font);
            CHECK(form.row(4) + font <= form.row(5));
            const auto scrolled = ui::conversion_form_layout(width, font, 100);
            CHECK_EQ(scrolled.password_field(true).y, confirm.y - 100);
        }
    }
}

TEST(conversion_diagnostics_include_operation_reason_and_item)
{
    vault::LegacyConversionReport report;
    report.status = vault::LegacyConversionStatus::VerificationFailed;
    report.operation = "Compare original media";
    report.detail = "Original content digest mismatch";
    report.failed_ordinal = 42;
    const auto message = ui::conversion_error_message(report);
    CHECK(message.find("Compare original media") != std::string::npos);
    CHECK(message.find("Original content digest mismatch") != std::string::npos);
    CHECK(message.find("42") != std::string::npos);
    CHECK(message.find("Keep the original") != std::string::npos);
}

TEST(conversion_diagnostics_retain_opaque_object_and_verification_counts)
{
    vault::LegacyConversionReport report;
    report.status = vault::LegacyConversionStatus::VerificationFailed;
    report.operation = "Verify reopened destination";
    report.detail = "Encrypted object authentication failed";
    report.failed_object.fill(0xab);
    report.objects_checked = 17;
    report.authenticated_bytes = 5 * 1024 * 1024;
    const auto message = ui::conversion_error_message(report);
    CHECK(message.find("abababababababababababababababab") != std::string::npos);
    CHECK(message.find("17 objects") != std::string::npos);
    CHECK(message.find("5 MiB") != std::string::npos);
}

TEST(conversion_progress_distinguishes_copy_and_verification)
{
    const auto copy = ui::conversion_progress_text(vault::ConversionPhase::Copying, 4, 12, 1048576);
    CHECK(copy.title == "Copying vault...");
    CHECK(copy.count_line.find("4 / 12") != std::string::npos);
    CHECK_EQ(copy.total, 12);
    const auto verify =
        ui::conversion_progress_text(vault::ConversionPhase::Verifying, 12, 12, 2097152);
    CHECK(verify.title == "Verifying converted vault...");
    CHECK(verify.count_line.find("2 MiB") != std::string::npos);
    CHECK_EQ(verify.total, 0);
    const auto reopen = ui::conversion_progress_text(vault::ConversionPhase::Reopening, 12, 12, 0);
    CHECK(reopen.count_line.find("Original") != std::string::npos);
}
