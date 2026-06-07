#pragma once

// Umbrella header for the crypto module.
// Includes all crypto sub-modules: sizes, secure memory, RNG, KDF, and AEAD.
// Callers who want the whole layer can include this one file.

#include "crypto_sizes.h"
#include "secure_mem.h"
#include "random.h"
#include "kdf.h"
#include "aead.h"
