#pragma once

#include "CullingEngineMacros.h"

#if defined(CULLING_ENGINE_PLATFORM_ANDROID) || defined(__aarch64__)
#include "SimdArmNeonTranslation.h"
#elif defined(CULLING_ENGINE_NATIVE)
#include "SimdX86Intrinsics.h"

#endif
