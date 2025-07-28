#ifndef NUMPY_SIMD_ROUTINES_NPSR_COMMON_H_
#define NUMPY_SIMD_ROUTINES_NPSR_COMMON_H_

#include <hwy/highway.h>

#include <cfenv>
#include <type_traits>

#include "precise.h"

#endif  // NUMPY_SIMD_ROUTINES_NPSR_COMMON_H_

#if defined(NUMPY_SIMD_ROUTINES_NPSR_COMMON_FOREACH_H_) == \
    defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NUMPY_SIMD_ROUTINES_NPSR_COMMON_FOREACH_H_
#undef NUMPY_SIMD_ROUTINES_NPSR_COMMON_FOREACH_H_
#else
#define NUMPY_SIMD_ROUTINES_NPSR_COMMON_FOREACH_H_
#endif

HWY_BEFORE_NAMESPACE();
namespace npsr::HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;
using hn::DFromV;
using hn::MFromD;
using hn::Rebind;
using hn::RebindToUnsigned;
using hn::TFromD;
using hn::TFromV;
using hn::VFromD;
constexpr bool kNativeFMA = HWY_NATIVE_FMA != 0;

HWY_ATTR void DummyToSuppressUnusedWarning() {}
}  // namespace npsr::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#endif  // NUMPY_SIMD_ROUTINES_NPSR_COMMON_FOREACH_H_
