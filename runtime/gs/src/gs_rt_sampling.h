// Standalone access to the RT shader fragment for CPU compilation checks.
#pragma once
namespace gsr {
inline constexpr const char* kHlslRTSampling =
#include "gs_rt_sampling.inc"
    ;
} // namespace gsr
