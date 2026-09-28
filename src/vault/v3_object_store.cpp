#include "vault/v3_object_store.h"

#include "crypto/aead.h"
#include "crypto/random.h"
#include "vault/byte_io.h"
#include "vault/chunk_codec.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <monocypher.h>
#include <vector>

namespace vault::v3 {
namespace {

constexpr uint32_t HEADER_INDEX = 0xffffffffU;
constexpr uint32_t TABLE_INDEX = 0xfffffffeU;
constexpr uint32_t FOOTER_INDEX = 0xfffffffdU;
constexpr uint64_t RECORD_OVERHEAD = crypto::NONCE_SIZE + crypto::TAG_SIZE;
constexpr uint64_t HEADER_RECORD_SIZE = 64 + RECORD_OVERHEAD;
constexpr uint64_t FOOTER_RECORD_SIZE = 64 + RECORD_OVERHEAD;
constexpr std::array<uint8_t, 8> OBJECT_MAGIC{'O', 'S', 'V', 'O', 'B', 'J', 0, 0};

bool valid_role(ObjectRole role) noexcept
{
    const auto value = std::to_underlying(role);
    return value >= 1 && value <= 4;
}

ObjectFrameTag tag_for(const ObjectInfo& info, uint32_t index) noexcept
{
    return {.role = info.role,
            .vault_id = info.vault_id,
            .object_id = info.object_id,
            .owner_node_id = info.owner_node_id,
            .frame_index = index,
            .frame_count = info.frame_count,
            .total_plaintext_length = info.plaintext_length};
}

bool encrypt_record(std::span<const uint8_t, crypto::KEY_SIZE> key, const ObjectInfo& info,
                    uint32_t index, std::span<const uint8_t> plain,
                    std::vector<uint8_t>& encrypted) noexcept
{
    const auto ad = build_object_frame_ad(tag_for(info, index));
    return crypto::encrypt_chunk(key, plain, encrypted, ad);
}

ObjectStatus decrypt_record(const ObjectFile& file, std::span<const uint8_t, crypto::KEY_SIZE> key,
                            const ObjectInfo& info, uint32_t index, uint64_t offset,
                            uint64_t length, crypto::SecureBytes& plain) noexcept
{
    using enum ObjectStatus;
    if (length < RECORD_OVERHEAD || length > std::numeric_limits<size_t>::max()) return Corrupt;
    std::vector<uint8_t> encrypted(static_cast<size_t>(length));
    if (!file.read_at(offset, encrypted)) return IoError;
    if (!plain.resize(static_cast<size_t>(length - RECORD_OVERHEAD))) return OutOfMemory;
    if (const auto ad = build_object_frame_ad(tag_for(info, index)); !crypto::decrypt_chunk_to(
            key, encrypted, std::span<uint8_t>{plain.data(), plain.size()}, ad)) {
        (void)plain.resize(0);
        return AuthenticationFailed;
    }
    return Ok;
}

bool valid_write_request(const ObjectWriteRequest& request, const ObjectReadFn& read) noexcept
{
    return valid_object_id(request.vault_id) && valid_object_id(request.owner_node_id) &&
           valid_role(request.role) && read && request.frame_plain_limit != 0 &&
           request.frame_plain_limit <= OBJECT_MAX_FRAME_PLAIN;
}

bool valid_expected_info(const ObjectInfo& expected) noexcept
{
    return valid_object_id(expected.object_id) && valid_object_id(expected.vault_id) &&
           valid_object_id(expected.owner_node_id) && valid_role(expected.role) &&
           expected.frame_plain_limit != 0 &&
           expected.frame_plain_limit <= OBJECT_MAX_FRAME_PLAIN &&
           expected.frame_count <= OBJECT_MAX_FRAMES;
}

std::array<uint8_t, 32> table_digest(std::span<const uint8_t, crypto::KEY_SIZE> key,
                                     std::span<const uint8_t> header,
                                     std::span<const uint8_t> table) noexcept
{
    constexpr char DOMAIN[] = "OSV-V3-OBJECT-TABLE";
    crypto::WipingBytes input;
    input.insert(input.end(), DOMAIN, DOMAIN + sizeof(DOMAIN));
    input.insert(input.end(), header.begin(), header.end());
    input.insert(input.end(), table.begin(), table.end());
    std::array<uint8_t, 32> digest{};
    crypto_blake2b_keyed(digest.data(), digest.size(), key.data(), key.size(), input.data(),
                         input.size());
    return digest;
}

}  // namespace

bool valid_object_id(const ObjectId& id) noexcept
{
    return std::ranges::any_of(id, [](uint8_t byte) { return byte != 0; });
}

std::optional<ObjectId> generate_object_id() noexcept
{
    ObjectId id{};
    if (!crypto::fill_random(id) || !valid_object_id(id)) return std::nullopt;
    return id;
}

std::array<uint8_t, OBJECT_PREAMBLE_SIZE> build_object_preamble(const Id& vault_id,
                                                                const ObjectId& object_id,
                                                                uint32_t frame_plain_limit) noexcept
{
    std::array<uint8_t, OBJECT_PREAMBLE_SIZE> out{};
    put_bytes_at(out, 0, OBJECT_MAGIC);
    put_u16_at(out, 8, OBJECT_FORMAT_VERSION);
    put_u16_at(out, 10, OBJECT_PREAMBLE_SIZE);
    put_bytes_at(out, 16, vault_id);
    put_bytes_at(out, 32, object_id);
    put_u32_at(out, 48, frame_plain_limit);
    return out;
}

ObjectWriteResult write_object_stream(const VaultRoot& root,
                                      std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                                      const ObjectWriteRequest& request, uint64_t plaintext_length,
                                      const ObjectReadFn& read) noexcept
{
    ObjectWriteResult result;
    if (!valid_write_request(request, read)) {
        result.status = ObjectStatus::InvalidArgument;
        return result;
    }
    const uint64_t count64 =
        plaintext_length == 0 ? 0 : 1 + (plaintext_length - 1) / request.frame_plain_limit;
    if (count64 > OBJECT_MAX_FRAMES || count64 > std::numeric_limits<uint32_t>::max()) {
        result.status = ObjectStatus::InvalidArgument;
        return result;
    }
    const auto id = generate_object_id();
    auto staging = root.create_staging_file();
    if (!id || !staging) return result;
    result.info = {.object_id = *id,
                   .vault_id = request.vault_id,
                   .owner_node_id = request.owner_node_id,
                   .role = request.role,
                   .media_format = request.media_format,
                   .plaintext_length = plaintext_length,
                   .frame_plain_limit = request.frame_plain_limit,
                   .frame_count = static_cast<uint32_t>(count64)};
    auto key = derive_object_key(master_key, request.vault_id, *id);
    const auto preamble = build_object_preamble(request.vault_id, *id, request.frame_plain_limit);
    std::array<uint8_t, 64> header{};
    header[0] = 0;
    header[1] = std::to_underlying(request.role);
    header[2] = request.media_format;
    put_bytes_at(header, 4, request.owner_node_id);
    put_u64_at(header, 20, plaintext_length);
    put_u32_at(header, 28, request.frame_plain_limit);
    put_u32_at(header, 32, result.info.frame_count);
    std::vector<uint8_t> record;
    if (!staging->write_all(preamble) ||
        !encrypt_record(key.span(), result.info, HEADER_INDEX, header, record) ||
        !staging->write_all(record))
        return result;

    std::vector<ObjectReader::Entry> entries;
    try {
        entries.reserve(result.info.frame_count);
    } catch (...) {
        result.status = ObjectStatus::OutOfMemory;
        return result;
    }
    uint64_t file_offset = OBJECT_PREAMBLE_SIZE + HEADER_RECORD_SIZE;
    for (uint32_t frame = 0; frame < result.info.frame_count; ++frame) {
        const uint64_t start = static_cast<uint64_t>(frame) * request.frame_plain_limit;
        const auto length = static_cast<size_t>(
            std::min<uint64_t>(request.frame_plain_limit, plaintext_length - start));
        crypto::SecureBytes frame_plain;
        if (!frame_plain.resize(length)) {
            result.status = ObjectStatus::OutOfMemory;
            return result;
        }
        if (!read(start, std::span<uint8_t>{frame_plain.data(), frame_plain.size()})) {
            result.status = ObjectStatus::IoError;
            return result;
        }
        crypto::SecureBytes framed;
        if (!chunk_codec::encode_frame(frame_plain.as_span(), framed)) {
            result.status = ObjectStatus::OutOfMemory;
            return result;
        }
        if (!encrypt_record(key.span(), result.info, frame, framed.as_span(), record) ||
            !staging->write_all(record))
            return result;
        entries.emplace_back(file_offset, record.size(), static_cast<uint32_t>(length), framed[0]);
        file_offset += record.size();
    }
    crypto::WipingBytes table(8 + entries.size() * 24, 0);
    put_u16_at(table, 0, 1);
    put_u16_at(table, 2, 24);
    put_u32_at(table, 4, result.info.frame_count);
    for (size_t i = 0; i < entries.size(); ++i) {
        const size_t at = 8 + i * 24;
        put_u64_at(table, at, entries[i].offset);
        put_u64_at(table, at + 8, entries[i].length);
        put_u32_at(table, at + 16, entries[i].plain_length);
        table[at + 20] = entries[i].compression;
    }
    const uint64_t table_offset = file_offset;
    if (!encrypt_record(key.span(), result.info, TABLE_INDEX, table, record) ||
        !staging->write_all(record))
        return result;
    const uint64_t table_record_length = record.size();
    file_offset += table_record_length;
    std::array<uint8_t, 64> footer{};
    footer[0] = 2;
    footer[1] = std::to_underlying(request.role);
    put_u16_at(footer, 2, 1);
    put_u32_at(footer, 4, result.info.frame_count);
    put_u64_at(footer, 8, table_offset);
    put_u64_at(footer, 16, table_record_length);
    put_u64_at(footer, 24, file_offset + FOOTER_RECORD_SIZE);
    const auto digest = table_digest(key.span(), header, table);
    put_bytes_at(footer, 32, digest);
    if (!encrypt_record(key.span(), result.info, FOOTER_INDEX, footer, record) ||
        !staging->write_all(record) || !staging->sync())
        return result;
    result.info.encrypted_length = file_offset + record.size();
    if (!root.publish(*staging, *id)) {
        result.status = ObjectStatus::IoError;
        return result;
    }
    result.status = ObjectStatus::Ok;
    return result;
}

ObjectWriteResult write_object(const VaultRoot& root,
                               std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                               const ObjectWriteRequest& request,
                               std::span<const uint8_t> plaintext) noexcept
{
    return write_object_stream(
        root, master_key, request, plaintext.size(),
        [plaintext](uint64_t offset, std::span<uint8_t> destination) {
            if (offset > plaintext.size() || destination.size() > plaintext.size() - offset)
                return false;
            if (!destination.empty())
                std::memcpy(destination.data(), plaintext.data() + static_cast<size_t>(offset),
                            destination.size());
            return true;
        });
}

ObjectReader::ObjectReader(ObjectFile file, crypto::SecureBuffer<crypto::KEY_SIZE> key,
                           const ObjectInfo& info, std::vector<Entry> entries) noexcept
    : file_(std::move(file)), key_(std::move(key)), info_(info), entries_(std::move(entries))
{}

ObjectReader::OpenResult ObjectReader::open(const VaultRoot& root,
                                            std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                                            const ObjectInfo& expected) noexcept
{
    OpenResult result;
    if (!valid_expected_info(expected)) {
        result.status = ObjectStatus::InvalidArgument;
        return result;
    }
    auto file = root.open_object(expected.object_id);
    if (!file) return result;
    const auto size = file->size();
    if (!size || *size < OBJECT_PREAMBLE_SIZE + HEADER_RECORD_SIZE + FOOTER_RECORD_SIZE ||
        (expected.encrypted_length && *size != expected.encrypted_length)) {
        result.status = ObjectStatus::Corrupt;
        return result;
    }
    std::array<uint8_t, OBJECT_PREAMBLE_SIZE> preamble{};
    if (!file->read_at(0, preamble)) return result;
    if (!std::ranges::equal(std::span(preamble).first<8>(), OBJECT_MAGIC) ||
        get_u16_at(preamble, 8) != OBJECT_FORMAT_VERSION || get_u16_at(preamble, 10) != 64 ||
        get_u32_at(preamble, 12) != 0 ||
        !std::ranges::equal(std::span(preamble).subspan<16, 16>(), expected.vault_id) ||
        !std::ranges::equal(std::span(preamble).subspan<32, 16>(), expected.object_id) ||
        get_u32_at(preamble, 48) != expected.frame_plain_limit ||
        std::ranges::any_of(std::span(preamble).subspan<52>(), [](uint8_t b) { return b != 0; })) {
        result.status = ObjectStatus::Corrupt;
        return result;
    }
    auto key = derive_object_key(master_key, expected.vault_id, expected.object_id);
    crypto::SecureBytes header;
    result.status =
        decrypt_record(*file, key.span(), expected, HEADER_INDEX, 64, HEADER_RECORD_SIZE, header);
    if (result.status != ObjectStatus::Ok) return result;
    if (header[0] != 0 || header[1] != std::to_underlying(expected.role) ||
        header[2] != expected.media_format || header[3] != 0 ||
        !std::ranges::equal(header.as_span().subspan<4, 16>(), expected.owner_node_id) ||
        get_u64_at(header.as_span(), 20) != expected.plaintext_length ||
        get_u32_at(header.as_span(), 28) != expected.frame_plain_limit ||
        get_u32_at(header.as_span(), 32) != expected.frame_count ||
        std::ranges::any_of(header.as_span().subspan(36), [](uint8_t b) { return b != 0; })) {
        result.status = ObjectStatus::Corrupt;
        return result;
    }
    crypto::SecureBytes footer;
    result.status = decrypt_record(*file, key.span(), expected, FOOTER_INDEX,
                                   *size - FOOTER_RECORD_SIZE, FOOTER_RECORD_SIZE, footer);
    if (result.status != ObjectStatus::Ok) return result;
    const uint64_t table_offset = get_u64_at(footer.as_span(), 8);
    const uint64_t table_length = get_u64_at(footer.as_span(), 16);
    if (const uint64_t expected_table_length =
            RECORD_OVERHEAD + 8ULL + static_cast<uint64_t>(expected.frame_count) * 24ULL;
        footer[0] != 2 || footer[1] != std::to_underlying(expected.role) ||
        get_u16_at(footer.as_span(), 2) != 1 ||
        get_u32_at(footer.as_span(), 4) != expected.frame_count ||
        get_u64_at(footer.as_span(), 24) != *size || table_offset < 64 + HEADER_RECORD_SIZE ||
        table_length != expected_table_length || table_offset > *size - FOOTER_RECORD_SIZE ||
        table_length != *size - FOOTER_RECORD_SIZE - table_offset) {
        result.status = ObjectStatus::Corrupt;
        return result;
    }
    crypto::SecureBytes table;
    result.status =
        decrypt_record(*file, key.span(), expected, TABLE_INDEX, table_offset, table_length, table);
    if (result.status != ObjectStatus::Ok) return result;
    if (table.size() != 8ULL + static_cast<uint64_t>(expected.frame_count) * 24 ||
        get_u16_at(table.as_span(), 0) != 1 || get_u16_at(table.as_span(), 2) != 24 ||
        get_u32_at(table.as_span(), 4) != expected.frame_count) {
        result.status = ObjectStatus::Corrupt;
        return result;
    }
    if (const auto digest = table_digest(key.span(), header.as_span(), table.as_span());
        !std::ranges::equal(digest, footer.as_span().subspan(32, 32))) {
        result.status = ObjectStatus::Corrupt;
        return result;
    }
    std::vector<Entry> entries;
    try {
        entries.reserve(expected.frame_count);
    } catch (...) {
        result.status = ObjectStatus::OutOfMemory;
        return result;
    }
    uint64_t prior_end = 64 + HEADER_RECORD_SIZE;
    uint64_t plain_sum = 0;
    for (uint32_t i = 0; i < expected.frame_count; ++i) {
        const size_t at = 8 + static_cast<size_t>(i) * 24;
        Entry entry{get_u64_at(table.as_span(), at), get_u64_at(table.as_span(), at + 8),
                    get_u32_at(table.as_span(), at + 16), table[at + 20]};
        if (entry.offset != prior_end || entry.length < RECORD_OVERHEAD + 1 ||
            entry.length > static_cast<uint64_t>(expected.frame_plain_limit) + RECORD_OVERHEAD +
                               chunk_codec::DEFLATE_HDR + 64 ||
            entry.plain_length > expected.frame_plain_limit || entry.compression > 1 ||
            table[at + 21] || table[at + 22] || table[at + 23] ||
            entry.length > table_offset - entry.offset) {
            result.status = ObjectStatus::Corrupt;
            return result;
        }
        prior_end = entry.offset + entry.length;
        plain_sum += entry.plain_length;
        entries.push_back(entry);
    }
    if (prior_end != table_offset || plain_sum != expected.plaintext_length) {
        result.status = ObjectStatus::Corrupt;
        return result;
    }
    result.reader = std::make_unique<ObjectReader>(std::move(*file), std::move(key), expected,
                                                   std::move(entries));
    result.status = ObjectStatus::Ok;
    return result;
}

ObjectStatus ObjectReader::read_frame(uint32_t index, crypto::SecureBytes& out) const noexcept
{
    using enum ObjectStatus;
    if (index >= entries_.size()) return InvalidArgument;
    const auto& entry = entries_[index];
    crypto::SecureBytes framed;
    const auto status = decrypt_record(
        file_, std::span<const uint8_t, crypto::KEY_SIZE>{key_.data(), crypto::KEY_SIZE}, info_,
        index, entry.offset, entry.length, framed);
    if (status != Ok) return status;
    if (framed.empty() || framed[0] != entry.compression ||
        !chunk_codec::decode_frame(framed.as_span(), out) || out.size() != entry.plain_length) {
        (void)out.resize(0);
        return Corrupt;
    }
    return Ok;
}

ObjectStatus ObjectReader::read_all(crypto::SecureBytes& out) const noexcept
{
    return read_range(0, static_cast<size_t>(info_.plaintext_length), out);
}

ObjectStatus ObjectReader::read_range(uint64_t offset, size_t length,
                                      crypto::SecureBytes& out) const noexcept
{
    using enum ObjectStatus;
    if (offset > info_.plaintext_length || length > info_.plaintext_length - offset ||
        length > std::numeric_limits<size_t>::max())
        return InvalidArgument;
    if (!out.resize(length)) return OutOfMemory;
    size_t copied = 0;
    uint64_t frame_start = 0;
    for (uint32_t i = 0; i < entries_.size() && copied < length; ++i) {
        const uint64_t frame_end = frame_start + entries_[i].plain_length;
        if (const uint64_t wanted_end = offset + length;
            frame_end > offset && frame_start < wanted_end) {
            crypto::SecureBytes frame;
            if (const auto status = read_frame(i, frame); status != Ok) {
                (void)out.resize(0);
                return status;
            }
            const uint64_t begin = std::max(offset, frame_start) - frame_start;
            const uint64_t end = std::min(wanted_end, frame_end) - frame_start;
            std::memcpy(out.data() + copied, frame.data() + begin,
                        static_cast<size_t>(end - begin));
            copied += static_cast<size_t>(end - begin);
        }
        frame_start = frame_end;
    }
    return copied == length ? Ok : Corrupt;
}

ObjectInspection inspect_object(const VaultRoot& root,
                                std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                                const ObjectInfo& expected) noexcept
{
    auto opened = ObjectReader::open(root, master_key, expected);
    return {.status = opened.status,
            .info = opened.status == ObjectStatus::Ok ? opened.reader->info() : expected};
}

}  // namespace vault::v3
