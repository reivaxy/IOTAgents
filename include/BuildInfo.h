#pragma once

// build_info_generated.h is produced at build time by scripts/build_info.py.
#if __has_include("build_info_generated.h")
#include "build_info_generated.h"
#endif

#ifndef BUILD_GIT_HASH
#define BUILD_GIT_HASH "unknown"
#endif

#ifndef BUILD_TIME
#define BUILD_TIME "unknown"
#endif
