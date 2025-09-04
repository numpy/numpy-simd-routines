// To include them once per target, which is ensured by the toggle check.
#if defined(NPSR_NPSR_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_NPSR_H_
#undef NPSR_NPSR_H_
#else
#define NPSR_NPSR_H_
#endif

#include "npsr/trig/inl.h"

#endif  // NPSR_NPSR_H_
