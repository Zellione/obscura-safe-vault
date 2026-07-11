#include "test_framework.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

#include "platform/error_log.h"

namespace fs = std::filesystem;

namespace {
// RAII unique temp file path, removed on destruction.
struct TempFile {
    fs::path path;
    explicit TempFile(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_error_log_" + std::string(tag) + "_" + std::to_string(ctr++) + ".log");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};

std::string read_text(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
} // namespace

TEST(error_log_appends_tag_and_message)
{
    TempFile tf("basic");
    platform::ErrorLog log(tf.path);
    log.append("Vault", "something went wrong");
    CHECK_EQ(read_text(tf.path), std::string("[Vault] something went wrong\n"));
}

TEST(error_log_appends_multiple_lines_across_calls)
{
    TempFile tf("multi");
    platform::ErrorLog log(tf.path);
    log.append("A", "first");
    log.append("B", "second");
    CHECK_EQ(read_text(tf.path), std::string("[A] first\n[B] second\n"));
}

TEST(error_log_persists_across_instances)
{
    TempFile tf("persist");
    { platform::ErrorLog(tf.path).append("X", "one"); }
    { platform::ErrorLog(tf.path).append("X", "two"); }
    CHECK_EQ(read_text(tf.path), std::string("[X] one\n[X] two\n"));
}

TEST(error_log_empty_path_instance_is_safe_noop)
{
    platform::ErrorLog log;   // default ctor: no file
    log.append("X", "should not throw or crash");
    // Nothing to assert on disk — just must not throw/crash.
}

TEST(log_error_free_function_does_not_throw)
{
    // Exercises the stderr + default-location path; must be safe even in a
    // headless test environment (config_dir() may or may not be available).
    platform::log_error("Test", "log_error smoke test");
}

TEST(install_terminate_logger_is_callable_and_restorable)
{
    // Preserve/restore the ambient terminate handler so this test doesn't
    // leak a permanent process-wide change into other tests.
    const auto previous = std::get_terminate();
    platform::install_terminate_logger();
    CHECK_TRUE(std::get_terminate() != nullptr);
    std::set_terminate(previous);
}
