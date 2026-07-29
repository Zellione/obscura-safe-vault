// osv-qt-mkvault <vault.osv> <password> [image-dir]
// Creates a fresh test vault; if image-dir is given, add_image()s every regular
// file in it (invalid images are rejected by the vault and reported, not fatal).
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
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
    if (argc >= 4) {
        for (const auto& e : std::filesystem::directory_iterator(argv[3])) {
            if (!e.is_regular_file()) continue;
            std::ifstream f(e.path(), std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
            auto r = v.add_image("", data, e.path().filename().string());
            std::printf("%s: %s\n", e.path().filename().c_str(),
                        r == vault::VaultResult::Ok ? "ok" : "skipped");
        }
    }
    std::puts("done");
    return 0;
}
