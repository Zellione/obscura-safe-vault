// osv-qt-mkvault <vault.osv> <password> [image-dir]
// Creates a fresh test vault; if image-dir is given, add_image()s every regular
// file in it (invalid images are rejected by the vault and reported, not fatal).
// A first-level subdirectory becomes a sub-gallery holding its regular files.
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "vault/vault.h"

int main(int argc, char** argv)
{
    if (argc < 3) { std::puts("usage: osv-qt-mkvault <vault.osv> <password> [image-dir]"); return 2; }
    std::span<const uint8_t> pw{reinterpret_cast<const uint8_t*>(argv[2]), std::strlen(argv[2])};

    vault::Vault v;
    // Use test-speed KdfParams (same as tests/vault/test_vault.cpp)
    if (vault::Vault::create(argv[1], pw, {},
                            crypto::KdfParams{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1}, v)
        != vault::VaultResult::Ok) {
        std::puts("create failed"); return 1;
    }

    // Add one regular file into gallery `parent` ("" = root).
    auto add_file = [&v](const std::filesystem::directory_entry& e, const std::string& parent) {
        std::ifstream f(e.path(), std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());

        auto ext = e.path().extension().string();
        // Convert to lowercase for comparison
        for (auto& c : ext) c = std::tolower(c);

        vault::VaultResult r = vault::VaultResult::Ok;
        if (ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".mov") {
            r = v.add_video(parent, data, e.path().filename().string());
        } else {
            r = v.add_image(parent, data, e.path().filename().string());
        }
        std::printf("%s%s%s: %s\n", parent.c_str(), parent.empty() ? "" : "/",
                    e.path().filename().c_str(),
                    r == vault::VaultResult::Ok ? "ok" : "skipped");
    };

    if (argc >= 4) {
        for (const auto& e : std::filesystem::directory_iterator(argv[3])) {
            if (e.is_regular_file()) {
                add_file(e, "");
            } else if (e.is_directory()) {
                const std::string gal = e.path().filename().string();
                if (v.create_gallery(gal) != vault::VaultResult::Ok) {
                    std::printf("%s: gallery skipped\n", gal.c_str());
                    continue;
                }
                for (const auto& s : std::filesystem::directory_iterator(e.path())) {
                    if (s.is_regular_file()) add_file(s, gal);
                }
            }
        }
    }
    std::puts("done");
    return 0;
}
