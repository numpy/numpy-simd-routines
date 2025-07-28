#include "npsr/common.h"
#include "npsr/trig/data/data.h"
#include "npsr/utils-inl.h"

#if defined(NPSR_TRIG_LOW_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_TRIG_LOW_INL_H_
#undef NPSR_TRIG_LOW_INL_H_
#else
#define NPSR_TRIG_LOW_INL_H_
#endif

HWY_BEFORE_NAMESPACE();

namespace npsr::HWY_NAMESPACE::trig {

template <bool IS_COS, typename V, HWY_IF_F32(TFromV<V>)>
HWY_API V PolyLow(V r, V r2) {
  using namespace hn;

  const DFromV<V> d;
  const V c9 = Set(d, IS_COS ? 0x1.5d866ap-19f : 0x1.5dbdfp-19f);
  const V c7 = Set(d, IS_COS ? -0x1.9f6d9ep-13 : -0x1.9f6ffep-13f);
  const V c5 = Set(d, IS_COS ? 0x1.110ec8p-7 : 0x1.110eccp-7f);
  const V c3 = Set(d, -0x1.55554cp-3f);
  V poly = MulAdd(c9, r2, c7);
  poly = MulAdd(r2, poly, c5);
  poly = MulAdd(r2, poly, c3);
  if constexpr (IS_COS) {
    // Although this path handles cosine, we have already transformed the
    // input using the identity: cos(x) = sin(x + π/2) This means we're no
    // longer directly evaluating a cosine Taylor series; instead, we evaluate
    // the sine approximation polynomial at (x + π/2).
    //
    // The sine approximation has the general form:
    //    sin(r) ≈ r + r³ · P(r²)
    //
    // So, we compute:
    //    r³ = r · r²
    //    sin(r) ≈ r + r³ · poly
    //
    // This formulation preserves accuracy by computing the highest order
    // terms last, which benefits from FMA to reduce rounding error.
    V r3 = Mul(r2, r);
    poly = MulAdd(r3, poly, r);
  } else {
    poly = Mul(poly, r2);
    poly = MulAdd(r, poly, r);
  }
  return poly;
}

template <bool IS_COS, typename V, HWY_IF_F64(TFromV<V>)>
HWY_API V PolyLow(V r, V r2) {
  using namespace hn;

  const DFromV<V> d;
  const V c15 = Set(d, -0x1.9f1517e9f65fp-41);
  const V c13 = Set(d, 0x1.60e6bee01d83ep-33);
  const V c11 = Set(d, -0x1.ae6355aaa4a53p-26);
  const V c9 = Set(d, 0x1.71de3806add1ap-19);
  const V c7 = Set(d, -0x1.a01a019a659ddp-13);
  const V c5 = Set(d, 0x1.111111110a573p-7);
  const V c3 = Set(d, -0x1.55555555554a8p-3);
  V poly = MulAdd(c15, r2, c13);
  poly = MulAdd(r2, poly, c11);
  poly = MulAdd(r2, poly, c9);
  poly = MulAdd(r2, poly, c7);
  poly = MulAdd(r2, poly, c5);
  poly = MulAdd(r2, poly, c3);
  return poly;
}

template <bool IS_COS, typename V>
HWY_API V Low(V x) {
  using namespace hn;
  using hwy::SignMask;
  namespace data = ::npsr::trig::data;

  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du;
  using T = TFromV<V>;
  // Load frequently used constants as vector registers
  const V abs_mask = BitCast(d, Set(du, SignMask<T>() - 1));
  const V x_abs = And(abs_mask, x);
  const V x_sign = AndNot(x_abs, x);

  constexpr bool kIsSingle = std::is_same_v<T, float>;
  // Transform cosine to sine using identity: cos(x) = sin(x + π/2)
  const V half_pi = Set(d, data::kHalfPi<T>);
  V x_trans = x_abs;
  if constexpr (IS_COS) {
    x_trans = Add(x_abs, half_pi);
  }
  // check zero input/subnormal for cosine (cos(~0) = 1)
  const auto is_cos_near_zero = Eq(x_trans, half_pi);

  // Compute N = round(x/π) using "magic number" technique
  // and stores integer part in mantissa
  const V magic_round = Set(d, kIsSingle ? 0x1.8p23f : 0x1.8p52);
  V n_biased = MulAdd(x_trans, Set(d, data::kInvPi<T>), magic_round);
  V n = Sub(n_biased, magic_round);

  // Adjust quotient for cosine (accounts for π/2 phase shift)
  if constexpr (IS_COS) {
    // For cosine, we computed N = round((x + π/2)/π) but need N' for x:
    //   N = round((x + π/2)/π) = round(x/π + 0.5)
    // This is often 1 more than round(x/π), so we subtract 0.5:
    //   N' = N - 0.5
    n = Sub(n, Set(d, static_cast<T>(0.5)));
  }
  // Use Cody-Waite method with triple-precision PI
  constexpr auto kPi = data::kPi<T, kNativeFMA>;

  V r = NegMulAdd(n, Set(d, kPi[0]), x_abs);
  r = NegMulAdd(n, Set(d, kPi[1]), r);
  V r_lo = NegMulAdd(n, Set(d, kPi[2]), r);
  if constexpr (!kNativeFMA) {
    if (!kIsSingle) {
      r = r_lo;
    }
    r_lo = NegMulAdd(n, Set(d, kPi[3]), r_lo);
  }

  if (kIsSingle) {
    r = r_lo;
  }
  V r2 = Mul(r, r);
  V poly = PolyLow<IS_COS>(r, r2);

  if (!kIsSingle) {
    V r2_corr = Mul(r2, r_lo);
    poly = MulAdd(r2_corr, poly, r_lo);
  }

  // Extract octant sign information from quotient and flip the sign bit
  poly = Xor(poly,
             BitCast(d, ShiftLeft<sizeof(T) * 8 - 1>(BitCast(du, n_biased))));
  if constexpr (IS_COS) {
    poly = IfThenElse(is_cos_near_zero, Set(d, static_cast<T>(1.0)), poly);
  } else {
    // Restore original sign for sine (odd function)
    poly = Xor(poly, x_sign);
  }
  return poly;
}
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace npsr::HWY_NAMESPACE::trig

HWY_AFTER_NAMESPACE();

#endif  // NPSR_TRIG_LOW_INL_H_
