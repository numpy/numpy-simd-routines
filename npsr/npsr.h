// To include them once per target, which is ensured by the toggle check.
// clang-format off
#if defined(_NPSR_NPSR_H_) == defined(HWY_TARGET_TOGGLE) // NOLINT
#ifdef _NPSR_NPSR_H_
#undef _NPSR_NPSR_H_
#else
#define _NPSR_NPSR_H_
#endif

#include "npsr/trig/inl.h"

#endif // _NPSR_NPSR_H_
