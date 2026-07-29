// Headless proof that the premake-built core also builds+works under CMake:
// create → reopen → wrong-password AuthFailed → unlock → create_gallery → list.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>

#include "vault/vault.h"

static std::span<const uint8_t> b(const char* s)
{
    return {reinterpret_cast<const uint8_t*>(s), std::strlen(s)};
}

int main()
{
    const std::string path = "/tmp/osv_qt_core_smoke.osv";
    std::remove(path.c_str());

    {
        vault::Vault v;
        // Use test-speed KdfParams (same as tests/vault/test_vault.cpp)
        auto r = vault::Vault::create(path, b("hunter2"), {},
                                      crypto::KdfParams{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1}, v);
        assert(r == vault::VaultResult::Ok && v.is_unlocked());
        assert(v.create_gallery("trips/beach") == vault::VaultResult::Ok);
    }
    {
        vault::Vault v;
        assert(vault::Vault::open(path, v) == vault::VaultResult::Ok);
        assert(!v.is_unlocked());
        assert(v.unlock(b("wrong"), {}) == vault::VaultResult::AuthFailed);
        assert(v.unlock(b("hunter2"), {}) == vault::VaultResult::Ok);
        assert(v.list("trips").size() == 1);
    }
    std::remove(path.c_str());
    std::puts("core_smoke OK");
    return 0;
}
