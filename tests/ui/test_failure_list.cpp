#include "test_framework.h"

#include "ui/failure_list_dialog.h"
#include "vault/vault.h"

using vault::VaultResult;
using Stage = vault::TransferFailure::Stage;

TEST(failure_line_maps_reasons)
{
    auto line = [](VaultResult c, Stage s) {
        return ui::transfer_failure_line({.path = "g/a.jpg", .code = c, .stage = s});
    };
    CHECK_EQ(line(VaultResult::InvalidArg,    Stage::Write),
             std::string("g/a.jpg - name not allowed in destination"));
    CHECK_EQ(line(VaultResult::AlreadyExists, Stage::Write),
             std::string("g/a.jpg - name already exists at destination"));
    CHECK_EQ(line(VaultResult::AuthFailed,    Stage::Read),
             std::string("g/a.jpg - source data corrupt or unreadable"));
    CHECK_EQ(line(VaultResult::IoError,       Stage::Read),
             std::string("g/a.jpg - could not read source (possibly out of memory)"));
    CHECK_EQ(line(VaultResult::IoError,       Stage::Write),
             std::string("g/a.jpg - destination write failed (disk full?)"));
    CHECK_EQ(line(VaultResult::NotFound,      Stage::Read),
             std::string("g/a.jpg - item not found in source"));
}

TEST(transfer_failure_reason_maps_codes)
{
    auto reason = [](VaultResult c, Stage s) {
        return ui::transfer_failure_reason(c, s);
    };
    CHECK_EQ(reason(VaultResult::InvalidArg,    Stage::Write),
             std::string("name not allowed in destination"));
    CHECK_EQ(reason(VaultResult::AlreadyExists, Stage::Write),
             std::string("name already exists at destination"));
    CHECK_EQ(reason(VaultResult::AuthFailed,    Stage::Read),
             std::string("source data corrupt or unreadable"));
    CHECK_EQ(reason(VaultResult::IoError,       Stage::Read),
             std::string("could not read source (possibly out of memory)"));
    CHECK_EQ(reason(VaultResult::IoError,       Stage::Write),
             std::string("destination write failed (disk full?)"));
    CHECK_EQ(reason(VaultResult::NotFound,      Stage::Read),
             std::string("item not found in source"));
}
