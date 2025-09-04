#ifndef NPSR_HWY_H_
#define NPSR_HWY_H_

#include <hwy/highway.h>

#endif  // NPSR_HWY_H_

#if defined(NPSR_HWY_FOREACH_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_HWY_FOREACH_H_
#undef NPSR_HWY_FOREACH_H_
#else
#define NPSR_HWY_FOREACH_H_
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

inline HWY_ATTR void DummyToSuppressUnusedWarning() {}
}  // namespace npsr::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#endif  // NPSR_HWY_FOREACH_H_
