#include "ui/import_common.h"

#include <fstream>

namespace ui {

bool read_whole_file(const std::filesystem::path& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff sz = f.tellg();
    if (sz <= 0) return false;
    out.resize(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return true;
}

void tally_import_result(vault::VaultResult r, const std::string& filename, ZipImportOutcome& out)
{
    using enum vault::VaultResult;
    switch (r) {
        case Ok:
            ++out.imported;
            break;
        case AlreadyExists:
        case InvalidArg:
        case BadFormat:  // unsupported / duplicate content
            ++out.skipped;
            break;
        default:
            if (out.error.empty()) out.error = "Import error on " + filename;
            ++out.skipped;
            break;
    }
}

} // namespace ui
