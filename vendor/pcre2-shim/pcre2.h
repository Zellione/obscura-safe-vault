#pragma once
// All application regex operations use UTF-8; PCRE2 requires preprocessor configuration.
#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 8
#endif
#include "pcre2.h.generic"
