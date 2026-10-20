cmake_minimum_required(VERSION 3.20)
project(osv7zip LANGUAGES C CXX)

set(Z7_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../7zip")
# Keep the pinned submodule pristine; the one security adaptation is applied
# only to this private build tree.
file(COPY "${Z7_ROOT}/C" "${Z7_ROOT}/CPP" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/source")
set(Z7_ROOT "${CMAKE_CURRENT_BINARY_DIR}/source")
set(aes "${Z7_ROOT}/CPP/7zip/Crypto/7zAes.cpp")
file(READ "${aes}" aes_source)
# Normalize insignificant trailing spaces before matching the pinned function.
string(REGEX REPLACE "[ \t]+\n" "\n" aes_source "${aes_source}")
set(old_prepare [=[
  // BCJ2 threads use same password. So we use long lock.
  MT_LOCK

  bool finded = false;
  if (!_cachedKeys.GetKey(_key))
  {
    finded = g_GlobalKeyCache.GetKey(_key);
    if (!finded)
      _key.CalcKey();
    _cachedKeys.Add(_key);
  }
  if (!finded)
    g_GlobalKeyCache.FindAndAdd(_key);
]=])
set(new_prepare [=[
  // Obscura: passwords and derived keys must not outlive this decoder.
  if (!_cachedKeys.GetKey(_key))
  {
    _key.CalcKey();
    _cachedKeys.Add(_key);
  }
]=])
string(FIND "${aes_source}" "${old_prepare}" patch_offset)
if(patch_offset EQUAL -1)
  message(FATAL_ERROR "Pinned 7-Zip key-cache adaptation no longer matches")
endif()
string(REPLACE "${old_prepare}" "${new_prepare}" aes_source "${aes_source}")
string(REPLACE "static CKeyInfoCache g_GlobalKeyCache(32);" "" aes_source "${aes_source}")
file(WRITE "${aes}" "${aes_source}")

set(rar_aes "${Z7_ROOT}/CPP/7zip/Crypto/Rar5Aes.cpp")
file(READ "${rar_aes}" rar_source)
set(old_lookup [=[    {
      MT_LOCK
      if (!g_Key._needCalc && IsKeyEqualTo(g_Key))
      {
        CopyCalcedKeysFrom(g_Key);
        _needCalc = false;
      }
    }]=])
set(old_store [=[      {
        MT_LOCK
        g_Key = *this;
      }]=])
foreach(fragment IN ITEMS "${old_lookup}" "${old_store}" "static CKey g_Key;")
  string(FIND "${rar_source}" "${fragment}" patch_offset)
  if(patch_offset EQUAL -1)
    message(FATAL_ERROR "Pinned RAR5 key-cache adaptation no longer matches")
  endif()
  string(REPLACE "${fragment}" "" rar_source "${rar_source}")
endforeach()
file(WRITE "${rar_aes}" "${rar_source}")

# Upstream's x86 shortcut casts arbitrary byte pointers to integer pointers.
# Select its portable byte-load/store implementation in every configuration;
# this removes alignment UB without suppressing UBSan checks.
set(cpu_arch "${Z7_ROOT}/C/CpuArch.h")
file(READ "${cpu_arch}" cpu_source)
set(unaligned_marker "#ifdef MY_CPU_LE_UNALIGN\n\n#define GetUi16")
string(FIND "${cpu_source}" "${unaligned_marker}" patch_offset)
if(patch_offset EQUAL -1)
  message(FATAL_ERROR "Pinned 7-Zip portable-access adaptation no longer matches")
endif()
string(REPLACE "${unaligned_marker}"
  "#undef MY_CPU_LE_UNALIGN\n#undef MY_CPU_LE_UNALIGN_64\n\n${unaligned_marker}"
  cpu_source "${cpu_source}")
file(WRITE "${cpu_arch}" "${cpu_source}")

function(add_sources directory extension)
  foreach(name ${ARGN})
    list(APPEND decoder_sources "${Z7_ROOT}/${directory}/${name}.${extension}")
  endforeach()
  set(decoder_sources "${decoder_sources}" PARENT_SCOPE)
endfunction()

# Based on upstream Bundles/Format7zExtract, with the RAR decoders added.
# No archive writers, filesystem extraction, external codecs, or CLI are linked.
add_sources(CPP/Common cpp
  CRC CrcReg IntToString MyString MyVector NewHandler StringConvert StringToInt
  Wildcard UTFConvert MyWindows Sha1Prepare Sha256Prepare)
add_sources(CPP/Windows cpp PropVariant PropVariantUtils PropVariantConv TimeUtils System)
add_sources(CPP/7zip/Common cpp
  CreateCoder CWrappers InBuffer FilterCoder LimitedStreams MethodId MethodProps
  OutBuffer ProgressUtils PropId StreamObjects StreamUtils)
add_sources(CPP/7zip/Archive cpp ArchiveExports DllExports2)
add_sources(CPP/7zip/Archive/Common cpp
  CoderMixer2 HandlerOut ItemNameUtils OutStreamWithCRC ParseProperties FindSignature InStreamWithCRC)
add_sources(CPP/7zip/Archive/7z cpp
  7zCompressionMode 7zDecode 7zExtract 7zHandler 7zHeader 7zIn 7zProperties 7zRegister)
add_sources(CPP/7zip/Archive/Rar cpp RarHandler Rar5Handler)
add_sources(CPP/7zip/Compress cpp
  CodecExports Bcj2Coder Bcj2Register BcjCoder BcjRegister BitlDecoder BranchMisc
  BranchRegister ByteSwap BZip2Crc BZip2Decoder BZip2Register CopyCoder CopyRegister
  DeflateDecoder DeflateRegister DeltaFilter Lzma2Decoder Lzma2Register LzmaDecoder
  LzmaRegister LzOutWindow PpmdDecoder PpmdRegister Rar1Decoder Rar2Decoder
  Rar3Decoder Rar3Vm Rar5Decoder RarCodecsRegister)
add_sources(CPP/7zip/Crypto cpp
  7zAes 7zAesRegister MyAes MyAesReg Rar20Crypto RarAes Rar5Aes HmacSha1 HmacSha256)
add_sources(C c
  7zStream Alloc Bcj2 Bra Bra86 BraIA64 CpuArch Delta Lzma2Dec Lzma2DecMt LzmaDec Ppmd7
  Ppmd7Dec Ppmd7aDec Ppmd8 Ppmd8Dec SwapBytes 7zCrc 7zCrcOpt Aes AesOpt
  Sha1 Sha1Opt Sha256 Sha256Opt Blake2s)

add_library(osv7zip SHARED ${decoder_sources} secure_alloc.cpp
  "${CMAKE_CURRENT_SOURCE_DIR}/../monocypher/src/monocypher.c")
target_compile_definitions(osv7zip PRIVATE Z7_EXTRACT_ONLY Z7_ST)
target_compile_features(osv7zip PRIVATE cxx_std_17)
target_include_directories(osv7zip PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../monocypher/src")
target_link_options(osv7zip PRIVATE
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
  -Wl,-z,defs -Wl,-z,noexecstack)
foreach(symbol malloc calloc realloc free posix_memalign
               _Znwm _Znam _ZdlPv _ZdaPv _ZdlPvm _ZdaPvm)
  target_link_options(osv7zip PRIVATE "-Wl,--wrap=${symbol}")
endforeach()
install(TARGETS osv7zip LIBRARY DESTINATION lib)
