#if defined(NPSR_TRIG_LOW_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_TRIG_LOW_INL_H_
#undef NPSR_TRIG_LOW_INL_H_
#else
#define NPSR_TRIG_LOW_INL_H_
#endif

#include "npsr/hwy.h"
#include "npsr/trig/data/data.h"

HWY_BEFORE_NAMESPACE();

namespace npsr::HWY_NAMESPACE::trig {

enum class Operation { kSin = 0, kCos = 1 };

template <Operation OP, typename V, HWY_IF_F32(TFromV<V>)>
NPSR_INTRIN V PolyLow(V r, V r2) {
  using namespace hn;

  const DFromV<V> d;
  constexpr bool kCos = OP == Operation::kCos;
  const V c9 = Set(d, kCos ? 0x1.5d866ap-19f : 0x1.5dbdfp-19f);
  const V c7 = Set(d, kCos ? -0x1.9f6d9ep-13 : -0x1.9f6ffep-13f);
  const V c5 = Set(d, kCos ? 0x1.110ec8p-7 : 0x1.110eccp-7f);
  const V c3 = Set(d, -0x1.55554cp-3f);
  V poly = MulAdd(c9, r2, c7);
  poly = MulAdd(r2, poly, c5);
  poly = MulAdd(r2, poly, c3);
  if constexpr (OP == Operation::kCos) {
    // cos was mapped to sin via cos(x) = sin(x + π/2) during reduction, so
    // this evaluates the same sine polynomial sin(r) ≈ r + r³ · P(r²), not a
    // cosine series. Reconstruct r + r³ · poly by forming r³ first (r3 = r²·r),
    // then a single FMA — this is the order SVML uses for both sin and cos
    // (R = X + X·X²·(...)). The sin branch below builds the same value with a
    // different but mathematically equivalent association (poly·r², then ×r).
    V r3 = Mul(r2, r);
    poly = MulAdd(r3, poly, r);
  } else {
    poly = Mul(poly, r2);
    poly = MulAdd(r, poly, r);
  }
  return poly;
}

template <Operation OP, typename V, HWY_IF_F64(TFromV<V>)>
NPSR_INTRIN V PolyLow(V r, V r2) {
  using namespace hn;
  const DFromV<V> d;
  // Both sin and cos evaluate sin(r) on r ∈ [−π/2, π/2]; cos merely reduces
  // around (N + 0.5)·π instead of N·π, which does not change the fitted
  // interval, so both want the same sin(r) minimax polynomial. The two sets
  // differ from each other only at the last-ULP level (trailing-bit rounding),
  // and each matches its Intel SVML counterpart (__svml_sin_d_la /
  // __svml_cos_d_la) byte-for-byte.
  constexpr bool kCos = OP == Operation::kCos;
  const V c15 = Set(d, kCos ? -0x1.9f0d60811aac8p-41 : -0x1.9f1517e9f65fp-41);
  const V c13 = Set(d, kCos ? 0x1.60e6857a2f220p-33 : 0x1.60e6bee01d83ep-33);
  const V c11 = Set(d, kCos ? -0x1.ae63546002231p-26 : -0x1.ae6355aaa4a53p-26);
  const V c9 = Set(d, kCos ? 0x1.71de38030fea0p-19 : 0x1.71de3806add1ap-19);
  const V c7 = Set(d, kCos ? -0x1.a01a019a5b87bp-13 : -0x1.a01a019a659ddp-13);
  const V c5 = Set(d, kCos ? 0x1.111111110a4a8p-7 : 0x1.111111110a573p-7);
  const V c3 = Set(d, kCos ? -0x1.55555555554a7p-3 : -0x1.55555555554a8p-3);
  V poly = MulAdd(c15, r2, c13);
  poly = MulAdd(r2, poly, c11);
  poly = MulAdd(r2, poly, c9);
  poly = MulAdd(r2, poly, c7);
  poly = MulAdd(r2, poly, c5);
  poly = MulAdd(r2, poly, c3);
  return poly;
}

template <Operation OP, typename V>
NPSR_INTRIN V Low(V x) {
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
  if constexpr (OP == Operation::kCos) {
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
  if constexpr (OP == Operation::kCos) {
    // For cosine, we computed N = round((x + π/2)/π) but need N' for x:
    //   N = round((x + π/2)/π) = round(x/π + 0.5)
    // This is often 1 more than round(x/π), so we subtract 0.5:
    //   N' = N - 0.5
    n = Sub(n, Set(d, static_cast<T>(0.5)));
  }
  // Cody-Waite reduction with multi-word π (3 words with FMA, 4 without)
  constexpr auto kPi = data::kPi<T, kNativeFMA>;
  V r = NegMulAdd(n, Set(d, kPi[0]), x_abs);
  r = NegMulAdd(n, Set(d, kPi[1]), r);
  V r_lo = NegMulAdd(n, Set(d, kPi[2]), r);

  if constexpr (!kNativeFMA) {
    r_lo = NegMulAdd(n, Set(d, kPi[3]), r_lo);
  }
  if constexpr (kIsSingle || !kNativeFMA) {
    // The polynomial must consume the fully reduced value. The partial r has
    // not yet subtracted the last π word(s), so it differs from the true
    // remainder by ~n·(remaining π tail) — far too coarse to feed r² and the
    // correction terms, so drop down to the fully reduced r_lo.
    r = r_lo;
  }
  
  V r2 = Mul(r, r);
  V poly = PolyLow<OP>(r, r2);

  if constexpr (!kIsSingle) {
    // f64 reconstruction. Non-FMA set r = r_lo above, so r2_corr = r³ and
    // this is the standard r + r³·poly = sin(r). With FMA, r is the 2-word
    // partial and r_lo the 3-word reduced value, so the result is
    // r_lo·(1 + r²·poly) = r_lo·sin(r)/r ≈ sin(r_lo): sin(r)/r varies slowly
    // and r ≈ r_lo (they differ by exactly n·π[2], ~2^-83), so the
    // reconstruction error is sub-ULP and the residual is just the poly fit.
    V r2_corr = Mul(r2, r_lo);
    poly = MulAdd(r2_corr, poly, r_lo);
  }

  // sin(r) was reduced around n·π, and sin/cos flip sign every π, so the sign
  // is the parity of n. That parity lives in bit 0 of n_biased (the integer n
  // sits in the low mantissa bits), so shifting it into the sign position and
  // XOR-ing flips the result on odd n.
  poly = Xor(poly,
             BitCast(d, ShiftLeft<sizeof(T) * 8 - 1>(BitCast(du, n_biased))));
  if constexpr (OP == Operation::kCos) {
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
