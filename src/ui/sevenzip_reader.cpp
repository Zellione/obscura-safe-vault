#include "ui/sevenzip_reader.h"

#include "platform/path_utf8.h"
#include "ui/zip_encoding.h"

// Must precede all SDK interface headers: instantiates their GUIDs once.
#include <CPP/Common/MyInitGuid.h>

#include <CPP/7zip/Archive/IArchive.h>
#include <CPP/7zip/IPassword.h>
#include <CPP/7zip/PropID.h>
#include <CPP/Common/MyCom.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

extern "C" HRESULT CreateObject(const GUID*, const GUID*, void**);
extern "C" int osv7zip_memory_degraded();

namespace ui {
using enum ArchiveReadError;
namespace {
constexpr uint64_t max_entry_bytes = uint64_t{4} << 30;
constexpr UInt32 max_entries = 100000;

void note_memory_status()
{
    if (osv7zip_memory_degraded() && crypto::should_warn_mlock_once())
        crypto::print_mlock_warning();
}

// BSTR allocation and release belong to the private secure decoder allocator.
// No intermediate std::wstring ever holds an archive password.
BSTR utf8_bstr(std::string_view text)
{
    if (!is_valid_utf8(text) || text.size() > std::numeric_limits<UINT>::max()) return nullptr;
    // 7-Zip's COM contract uses UTF-16 code units even on Linux, where
    // wchar_t is 32 bits. Supplementary characters need surrogate pairs.
    UINT count = 0;
    for (const char c : text) {
        const auto byte = std::byte{static_cast<unsigned char>(c)};
        if ((byte & std::byte{0xc0}) != std::byte{0x80}) ++count;
        if (byte >= std::byte{0xf0}) ++count;
    }
    BSTR result = SysAllocStringLen(nullptr, count);
    if (!result) return nullptr;
    size_t pos = 0;
    UINT out = 0;
    while (pos < text.size()) {
        const auto first = static_cast<unsigned char>(text[pos++]);
        uint32_t cp = first;
        unsigned extra = 0;
        if (first >= 0xf0) {
            cp &= 7;
            extra = 3;
        } else if (first >= 0xe0) {
            cp &= 15;
            extra = 2;
        } else if (first >= 0xc0) {
            cp &= 31;
            extra = 1;
        }
        for (unsigned i = 0; i < extra; ++i)
            cp = (cp << 6) |
                 (std::to_integer<uint32_t>(std::byte{static_cast<unsigned char>(text[pos++])} &
                                            std::byte{0x3f}));
        if (cp >= 0x10000) {
            cp -= 0x10000;
            result[out++] = static_cast<wchar_t>(0xd800 + (cp >> 10));
            result[out++] = static_cast<wchar_t>(0xdc00 + (cp & 0x3ff));
        } else {
            result[out++] = static_cast<wchar_t>(cp);
        }
    }
    return result;
}

void append_utf8(std::string& result, uint32_t cp)
{
    if (cp < 0x80) {
        result.push_back(static_cast<char>(cp));
        return;
    }
    unsigned tails = 1;
    uint32_t lead = 0xc0;
    if (cp >= 0x10000) {
        tails = 3;
        lead = 0xf0;
    } else if (cp >= 0x800) {
        tails = 2;
        lead = 0xe0;
    }
    result.push_back(static_cast<char>(lead | (cp >> (6 * tails))));
    while (tails != 0) {
        --tails;
        result.push_back(static_cast<char>(0x80 | ((cp >> (6 * tails)) & 63)));
    }
}

std::string bstr_utf8(BSTR text)
{
    std::string result;
    UINT pos = 0;
    const UINT length = SysStringLen(text);
    while (pos < length) {
        auto cp = static_cast<uint32_t>(text[pos++]);
        if (cp >= 0xd800 && cp <= 0xdbff && pos < length) {
            if (const auto low = static_cast<uint32_t>(text[pos]); low >= 0xdc00 && low <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                ++pos;
            }
        }
        append_utf8(result, cp);
    }
    return result;
}

class Input final : public IInStream, public CMyUnknownImp {
public:
    template <class T> friend class ::CMyComPtr;
    Z7_COM_UNKNOWN_IMP_1(IInStream)
public:
    Z7_IFACE_COM7_IMP(IInStream)
    Z7_IFACE_COM7_IMP(ISequentialInStream)
    std::span<const uint8_t> memory;
    std::vector<std::FILE*> files;
    std::vector<uint64_t> ends;
    uint64_t position = 0;
    ~Input()
    {
        for (auto* file : files)
            std::fclose(file);
    }
    bool open_files(std::span<const std::filesystem::path> paths)
    {
        uint64_t total = 0;
        files.reserve(paths.size());
        ends.reserve(paths.size());
        for (const auto& path : paths) {
            auto* file = platform::fopen_path(path, "rb");
            if (!file) return false;
            files.push_back(file);
            if (fseeko(file, 0, SEEK_END) != 0) return false;
            const auto size = ftello(file);
            if (size < 0 ||
                static_cast<uint64_t>(size) > std::numeric_limits<uint64_t>::max() - total)
                return false;
            total += static_cast<uint64_t>(size);
            ends.push_back(total);
        }
        return !files.empty();
    }
    uint64_t size() const
    {
        return files.empty() ? memory.size() : ends.back();
    }
};

Z7_COM7F_IMF(Input::Read(void* data, UInt32 amount, UInt32* processed))
{
    if (processed) *processed = 0;
    if (amount == 0 || position >= size()) return S_OK;
    auto count = static_cast<size_t>(std::min<uint64_t>(amount, size() - position));
    if (files.empty()) {
        std::memcpy(data, memory.data() + position, count);
    } else {
        const auto it = std::ranges::upper_bound(ends, position);
        const auto index = static_cast<size_t>(it - ends.begin());
        const uint64_t start = index == 0 ? 0 : ends[index - 1];
        count = static_cast<size_t>(std::min<uint64_t>(count, ends[index] - position));
        if (fseeko(files[index], static_cast<off_t>(position - start), SEEK_SET) != 0)
            return E_FAIL;
        count = std::fread(data, 1, count, files[index]);
        if (count == 0 && std::ferror(files[index])) return E_FAIL;
    }
    position += count;
    if (processed) *processed = static_cast<UInt32>(count);
    return S_OK;
}

Z7_COM7F_IMF(Input::Seek(Int64 offset, UInt32 origin, UInt64* new_position))
{
    if (origin > 2) return E_INVALIDARG;
    uint64_t base = 0;
    if (origin == 1)
        base = position;
    else if (origin == 2)
        base = size();
    const uint64_t magnitude =
        offset < 0 ? uint64_t{0} - static_cast<uint64_t>(offset) : static_cast<uint64_t>(offset);
    if ((offset < 0 && magnitude > base) ||
        (offset >= 0 && magnitude > std::numeric_limits<uint64_t>::max() - base))
        return E_INVALIDARG;
    position = offset < 0 ? base - magnitude : base + magnitude;
    if (new_position) *new_position = position;
    return S_OK;
}

class OpenCallback final : public IArchiveOpenCallback,
                           public IArchiveOpenVolumeCallback,
                           public ICryptoGetTextPassword,
                           public CMyUnknownImp {
public:
    template <class T> friend class ::CMyComPtr;
    Z7_COM_UNKNOWN_IMP_3(IArchiveOpenCallback, IArchiveOpenVolumeCallback, ICryptoGetTextPassword)
public:
    Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
    Z7_IFACE_COM7_IMP(IArchiveOpenVolumeCallback)
    Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
    crypto::SecureBytes password;
    std::vector<std::filesystem::path> paths;
    size_t next_volume = 1;
    std::atomic_bool asked{false};
    HRESULT get_password(BSTR* out)
    {
        *out = nullptr;
        asked.store(true);
        if (password.empty()) return E_ABORT;
        *out = utf8_bstr({reinterpret_cast<const char*>(password.data()), password.size()});
        return *out ? S_OK : E_OUTOFMEMORY;
    }
};
Z7_COM7F_IMF(OpenCallback::SetTotal(const UInt64*, const UInt64*))
{
    return S_OK;
}
Z7_COM7F_IMF(OpenCallback::SetCompleted(const UInt64*, const UInt64*))
{
    return S_OK;
}
Z7_COM7F_IMF(OpenCallback::CryptoGetTextPassword(BSTR* out))
{
    return get_password(out);
}
Z7_COM7F_IMF(OpenCallback::GetProperty(PROPID id, PROPVARIANT* value))
try {
    value->vt = VT_EMPTY;
    if (id == kpidName && !paths.empty()) {
        value->bstrVal = utf8_bstr(platform::path_to_utf8(paths.front().filename()));
        if (!value->bstrVal) return E_OUTOFMEMORY;
        value->vt = VT_BSTR;
    }
    return S_OK;
} catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
}
Z7_COM7F_IMF(OpenCallback::GetStream(const wchar_t*, IInStream** out))
try {
    *out = nullptr;
    if (next_volume >= paths.size()) return S_FALSE;
    CMyComPtr<Input> input = new Input;
    // Never follow an archive-controlled filename. Only the ordered, explicitly
    // discovered volume list is a capability to open a source file.
    if (!input->open_files(std::span(paths).subspan(next_volume++, 1))) return E_FAIL;
    *out = input.Detach();
    return S_OK;
} catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
}
struct EntryInfo {
    uint64_t size = 0;
    bool encrypted = false;
};
}  // namespace

struct SevenZipState {
    CMyComPtr<IInArchive> archive;
    CMyComPtr<Input> input;
    CMyComPtr<OpenCallback> opener;
    std::vector<ZipEntry> entries;
    std::vector<EntryInfo> info;
    ArchiveReadError error = None;
    std::jthread producer;
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic_bool stop{false};
    bool requested = false;
    bool ready = false;
    bool finished = false;
    size_t target = 0;
    size_t next = 0;
    size_t opens = 0;
    size_t written = 0;
    UInt32 active = 0;
    crypto::SecureBytes output;

    ~SevenZipState()
    {
        {
            std::lock_guard lock(mutex);
            stop.store(true);
        }
        changed.notify_all();
        if (producer.joinable()) producer.join();
        if (archive) archive->Close();
        archive.Release();
        opener.Release();
        input.Release();
        note_memory_status();
    }
    bool open_archive(std::string_view password);
    bool list_entries();
    void start();
};

namespace {
class Output final : public ISequentialOutStream, public CMyUnknownImp {
public:
    template <class T> friend class ::CMyComPtr;
    Z7_IFACES_IMP_UNK_1(ISequentialOutStream)
public:
    explicit Output(SevenZipState& state) : state_(state) {}

private:
    SevenZipState& state_;
};
Z7_COM7F_IMF(Output::Write(const void* data, UInt32 size, UInt32* processed))
{
    if (processed) *processed = 0;
    if (state_.stop.load()) return E_ABORT;
    if (size > state_.output.size() - state_.written) return E_FAIL;
    if (size != 0) std::memcpy(state_.output.data() + state_.written, data, size);
    state_.written += size;
    if (processed) *processed = size;
    return S_OK;
}

class ExtractCallback final : public IArchiveExtractCallback,
                              public ICryptoGetTextPassword,
                              public CMyUnknownImp {
public:
    template <class T> friend class ::CMyComPtr;
    Z7_IFACES_IMP_UNK_2(IArchiveExtractCallback, ICryptoGetTextPassword)
    Z7_IFACE_COM7_IMP(IProgress)
public:
    explicit ExtractCallback(SevenZipState& state) : state_(state) {}

private:
    SevenZipState& state_;
};
Z7_COM7F_IMF(ExtractCallback::SetTotal(UInt64))
{
    return state_.stop ? E_ABORT : S_OK;
}
Z7_COM7F_IMF(ExtractCallback::SetCompleted(const UInt64*))
{
    return state_.stop ? E_ABORT : S_OK;
}
Z7_COM7F_IMF(ExtractCallback::CryptoGetTextPassword(BSTR* out))
{
    return state_.opener->get_password(out);
}
Z7_COM7F_IMF(ExtractCallback::PrepareOperation(Int32))
{
    return state_.stop ? E_ABORT : S_OK;
}
Z7_COM7F_IMF(ExtractCallback::GetStream(UInt32 index, ISequentialOutStream** out, Int32))
{
    *out = nullptr;
    std::unique_lock lock(state_.mutex);
    state_.active = index;
    if (index >= state_.info.size()) return E_FAIL;
    if (state_.entries[index].is_dir) return S_OK;
    state_.changed.wait(lock, [this] { return state_.stop || state_.requested; });
    if (state_.stop) return E_ABORT;
    if (index < state_.target) return S_OK;
    if (index > state_.target) return E_FAIL;
    if (const uint64_t size = state_.info[index].size;
        size > max_entry_bytes || !state_.output.resize(static_cast<size_t>(size))) {
        state_.error = MemoryLimit;
        return E_OUTOFMEMORY;
    }
    state_.written = 0;
    CMyComPtr<Output> output = new (std::nothrow) Output(state_);
    if (!output) {
        state_.error = MemoryLimit;
        return E_OUTOFMEMORY;
    }
    *out = output.Detach();
    return S_OK;
}
Z7_COM7F_IMF(ExtractCallback::SetOperationResult(Int32 result))
{
    std::lock_guard lock(state_.mutex);
    if (!state_.requested || state_.active != state_.target) return S_OK;
    using namespace NArchive::NExtract::NOperationResult;
    if (result == kUnsupportedMethod)
        state_.error = Unsupported;
    else if (result != kOK || state_.written != state_.output.size()) {
        state_.error = state_.info[state_.active].encrypted ? WrongPassword : Corrupt;
        if (state_.opener->password.empty() && state_.opener->asked)
            state_.error = PasswordRequired;
    }
    state_.requested = false;
    state_.ready = true;
    state_.changed.notify_all();
    return state_.stop ? E_ABORT : S_OK;
}
}  // namespace

bool SevenZipState::list_entries()
{
    UInt32 count = 0;
    if (archive->GetNumberOfItems(&count) != S_OK || count > max_entries) return false;
    for (UInt32 i = 0; i < count; ++i) {
        PROPVARIANT property{};
        if (archive->GetProperty(i, kpidPath, &property) != S_OK) return false;
        const std::string name = property.vt == VT_BSTR ? bstr_utf8(property.bstrVal) : "";
        VariantClear(&property);
        if (archive->GetProperty(i, kpidIsDir, &property) != S_OK) return false;
        const bool directory = property.vt == VT_BOOL && property.boolVal != 0;
        VariantClear(&property);
        if (archive->GetProperty(i, kpidSize, &property) != S_OK) return false;
        const uint64_t size = property.vt == VT_UI8 ? property.uhVal.QuadPart : max_entry_bytes + 1;
        VariantClear(&property);
        if (archive->GetProperty(i, kpidEncrypted, &property) != S_OK) return false;
        const bool encrypted = property.vt == VT_BOOL && property.boolVal != 0;
        VariantClear(&property);
        entries.emplace_back(name, directory);
        info.push_back({size, encrypted});
    }
    return true;
}

bool SevenZipState::open_archive(std::string_view password)
{
    std::array<uint8_t, 8> magic{};
    if (UInt32 read = 0; input->Read(magic.data(), magic.size(), &read) != S_OK ||
                         !SevenZipReader::handles(std::span(magic).first(read)))
        return false;
    input->Seek(0, 0, nullptr);
    uint8_t format = 3;
    if (magic[0] == '7')
        format = 7;
    else if (magic[6] == 1)
        format = 0xcc;
    void* object = nullptr;
    if (const GUID clsid{0x23170f69, 0x40c1, 0x278a, {0x10, 0, 0, 1, 0x10, format, 0, 0}};
        CreateObject(&clsid, &IID_IInArchive, &object) != S_OK)
        return false;
    archive.Attach(static_cast<IInArchive*>(object));
    if (!opener->password.assign(
            {reinterpret_cast<const uint8_t*>(password.data()), password.size()})) {
        error = MemoryLimit;
        return false;
    }
    const UInt64 scan_limit = 0;
    // SDK crypto uses opaque stack scratch outside the secured heap allocator.
    crypto::mark_opaque_plaintext_seen();
    if (const HRESULT result = archive->Open(input, &scan_limit, opener); result != S_OK) {
        error = Corrupt;
        if (opener->asked) error = password.empty() ? PasswordRequired : WrongPassword;
        if (result == E_OUTOFMEMORY) error = MemoryLimit;
        return false;
    }
    PROPVARIANT flags{};
    const HRESULT flags_result = archive->GetArchiveProperty(kpidErrorFlags, &flags);
    const bool damaged = flags_result == S_OK && flags.vt == VT_UI4 && flags.ulVal != 0;
    VariantClear(&flags);
    if (damaged || !list_entries()) {
        entries.clear();
        info.clear();
        error = Corrupt;
        return false;
    }
    note_memory_status();
    return true;
}

void SevenZipState::start()
{
    ++opens;
    producer = std::jthread([this] {
        CMyComPtr<ExtractCallback> callback = new (std::nothrow) ExtractCallback(*this);
        const HRESULT result =
            callback ? archive->Extract(nullptr, std::numeric_limits<UInt32>::max(), 0, callback)
                     : E_OUTOFMEMORY;
        std::lock_guard lock(mutex);
        if (requested && !ready && error == None) {
            error = opener->asked && opener->password.empty() ? PasswordRequired : Corrupt;
            if (result == E_OUTOFMEMORY) error = MemoryLimit;
        }
        finished = true;
        changed.notify_all();
    });
}

SevenZipReader::SevenZipReader() : state_(std::make_unique<SevenZipState>()) {}
SevenZipReader::~SevenZipReader() = default;

bool SevenZipReader::handles(std::span<const uint8_t> bytes)
{
    constexpr std::array<uint8_t, 6> seven{'7', 'z', 0xbc, 0xaf, 0x27, 0x1c};
    constexpr std::array<uint8_t, 6> rar{'R', 'a', 'r', '!', 0x1a, 7};
    return bytes.size() >= 7 && (std::equal(seven.begin(), seven.end(), bytes.begin()) ||
                                 std::equal(rar.begin(), rar.end(), bytes.begin()));
}

bool SevenZipReader::open(std::span<const uint8_t> bytes, std::string_view password)
{
    state_ = std::make_unique<SevenZipState>();
    state_->input = new Input;
    state_->input->memory = bytes;
    state_->opener = new OpenCallback;
    return state_->open_archive(password);
}

bool SevenZipReader::open_files(std::span<const std::filesystem::path> paths,
                                std::string_view password)
{
    state_ = std::make_unique<SevenZipState>();
    if (paths.empty()) return false;
    state_->input = new Input;
    state_->opener = new OpenCallback;
    state_->opener->paths.assign(paths.begin(), paths.end());
    if (!state_->input->open_files(paths.first(1))) return false;
    std::array<uint8_t, 8> magic{};
    UInt32 read = 0;
    if (state_->input->Read(magic.data(), magic.size(), &read) != S_OK) return false;
    // Numeric 7z volumes are a seekable concatenation. RAR volumes each carry
    // a header and are supplied separately by the open callback.
    if (read >= 2 && magic[0] == '7' && magic[1] == 'z' && paths.size() > 1) {
        state_->input = new Input;
        if (!state_->input->open_files(paths)) return false;
        state_->opener->paths.clear();
    }
    state_->input->Seek(0, 0, nullptr);
    return state_->open_archive(password);
}

bool SevenZipReader::extract(size_t index, crypto::SecureBytes& out)
{
    if (index >= state_->entries.size() || state_->entries[index].is_dir) return false;
    // Reopening the archive itself isn't needed: Extract starts a fresh pass.
    if (state_->producer.joinable() && (index < state_->next || state_->error != None)) {
        {
            std::lock_guard stop_lock(state_->mutex);
            state_->stop.store(true);
        }
        state_->changed.notify_all();
        state_->producer.join();
        state_->stop.store(false);
        state_->finished = false;
    }
    std::unique_lock lock(state_->mutex);
    state_->error = None;
    state_->target = index;
    state_->requested = true;
    state_->ready = false;
    if (!state_->producer.joinable()) state_->start();
    state_->changed.notify_all();
    state_->changed.wait(lock, [this] { return state_->ready || state_->finished; });
    state_->next = index + 1;
    const bool ok = state_->ready && state_->error == None;
    if (!ok && state_->error == None) state_->error = Corrupt;
    if (ok)
        out = std::move(state_->output);
    else {
        state_->output.wipe();
        (void)state_->output.resize(0);
        out.wipe();
        (void)out.resize(0);
    }
    note_memory_status();
    return ok;
}

const std::vector<ZipEntry>& SevenZipReader::entries() const
{
    return state_->entries;
}
ArchiveReadError SevenZipReader::error() const
{
    return state_->error;
}
bool SevenZipReader::encrypted() const
{
    return state_->opener &&
           (state_->opener->asked ||
            std::ranges::any_of(state_->info, [](const auto& entry) { return entry.encrypted; }));
}
size_t SevenZipReader::stream_opens() const
{
    return state_->opens;
}
}  // namespace ui
