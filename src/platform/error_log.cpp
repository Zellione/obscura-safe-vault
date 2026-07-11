#include "platform/error_log.h"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <mutex>
#include <print>
#include <string>

#include "platform/paths.h"

namespace platform {

ErrorLog::ErrorLog(std::filesystem::path file) : file_(std::move(file)) {}

ErrorLog ErrorLog::default_location()
{
    auto dir = config_dir();
    if (dir.empty()) {
        return ErrorLog{};
    }
    return ErrorLog{dir / "error.log"};
}

void ErrorLog::append(std::string_view tag, std::string_view message) const
{
    if (file_.empty()) {
        return;
    }
    std::ofstream out(file_, std::ios::app);
    if (!out) {
        return;
    }
    out << '[' << tag << "] " << message << '\n';
}

namespace {
std::mutex& log_mutex()
{
    static std::mutex m;
    return m;
}
} // namespace

void log_error(std::string_view tag, std::string_view message)
{
    std::println(stderr, "[{}] {}", tag, message);

    std::lock_guard lock(log_mutex());
    static const ErrorLog log = ErrorLog::default_location();
    log.append(tag, message);
}

void install_terminate_logger()
{
    std::set_terminate([] {
        std::string what = "non-std exception";
        if (auto eptr = std::current_exception()) {
            try {
                std::rethrow_exception(eptr);
            } catch (const std::exception& e) {
                what = e.what();
            } catch (...) {
                what = "non-std exception";
            }
        }
        log_error("Fatal", "Unhandled exception, terminating: " + what);
        std::abort();
    });
}

} // namespace platform
