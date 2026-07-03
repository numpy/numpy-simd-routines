#ifndef NPSR_HWY_H_
#define NPSR_HWY_H_

#include <hwy/highway.h>
// This macro is used to define intrinsics that are:
// NOTE: equals to HWY_API.
// - always inlined
// - flattened (no separate stack frame)
// - marked maybe unused to suppress warnings when they are not used
// NOTE: we do not need to use HWY_ATTR because we wrap Highway intrinsics in
// HWY_BEFORE_NAMESPACE()/HWY_AFTER_NAMESPACE()
// which implies the nessessary target attributes via #pargma.
#define NPSR_INTRIN static HWY_INLINE HWY_FLATTEN HWY_MAYBE_UNUSED
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

/**
 * Substitute for the FMA product-error idiom on targets without native FMA.
 *
 * With FMA, `e = MulSub(a, b, Round(a*b))` recovers the rounding error of the
 * product exactly. Without it, that expression only sees the already-rounded
 * product and returns nothing useful. Instead, split both operands with a bit
 * mask (26 significant bits in the head) so `head = a_hi*b_hi` is exact by
 * construction, and return the dropped cross terms in `rest`:
 *   a*b == head + rest, up to a negligible ~2^-105-scale rounding of `rest`.
 */
template <typename V, HWY_IF_F64(TFromV<V>)>
NPSR_INTRIN void SplitMul(V a, V b, V& head, V& rest) {
  using namespace hn;
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du;
  const V mask = BitCast(d, Set(du, 0xFFFFFFFFF8000000u));
  const V a_hi = And(a, mask);
  const V a_lo = Sub(a, a_hi);
  const V b_hi = And(b, mask);
  const V b_lo = Sub(b, b_hi);
  head = Mul(a_hi, b_hi);
  rest = MulAdd(a_hi, b_lo, Mul(a_lo, b));
}

}  // namespace npsr::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#endif  // NPSR_HWY_FOREACH_H_
