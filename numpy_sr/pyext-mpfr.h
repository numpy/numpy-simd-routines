// MPFR reference oracle, registered as pseudo-target kTargetMpfr
// (numpy_sr.mpfr.<op>). The loop lives in C: a per-element Python loop
// would dominate stress-test runtime.
#ifndef NPSR_PY_PYEXT_MPFR_H_
#define NPSR_PY_PYEXT_MPFR_H_

#include <mpfr.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "numpy_sr/pyext.h"

namespace npsr::py {

using MpfrFn = int (*)(mpfr_ptr, mpfr_srcptr, mpfr_rnd_t);

// One high-precision pass -> correctly rounded T plus its sub-ULP residual
// res_out = (ref - exact)/ulp(ref) in [-0.5, 0.5] (0 for non-finite ref).
// Tests score err = ulp_dist(computed, ref) + residual vs the exact value.
template <MpfrFn Fn, typename T>
void MpfrRefResidual(const T* src, T* ref_out, double* res_out, size_t n) {
  constexpr bool kIsF32 = std::is_same_v<T, float>;
  constexpr mpfr_prec_t kPrec = kIsF32 ? 24 : 53;
  const mpfr_exp_t emin = mpfr_get_emin(), emax = mpfr_get_emax();
  mpfr_set_emin(kIsF32 ? -148 : -1073);
  mpfr_set_emax(kIsF32 ? 128 : 1024);
  mpfr_t x, hi, ref, diff;
  mpfr_init2(x, kPrec);
  mpfr_init2(hi, kPrec + 64);  // guard bits -> `hi` is the exact value for us
  mpfr_init2(ref, kPrec);
  mpfr_init2(diff, kPrec + 64);
  for (size_t i = 0; i < n; ++i) {
    if constexpr (kIsF32) {
      mpfr_set_flt(x, src[i], MPFR_RNDN);
    } else {
      mpfr_set_d(x, src[i], MPFR_RNDN);
    }
    Fn(hi, x, MPFR_RNDN);
    int t =
        mpfr_set(ref, hi, MPFR_RNDN);  // correctly rounded T, == MpfrForward
    mpfr_subnormalize(ref, t, MPFR_RNDN);
    const T r = kIsF32 ? mpfr_get_flt(ref, MPFR_RNDN)
                       : static_cast<T>(mpfr_get_d(ref, MPFR_RNDN));
    ref_out[i] = r;
    if (!std::isfinite(r)) {
      res_out[i] = 0.0;
      continue;
    }
    const T a = std::fabs(r);
    const double ulp =
        a == T(0)
            ? std::ldexp(1.0, kIsF32 ? -149 : -1074)
            : static_cast<double>(
                  std::nextafter(a, std::numeric_limits<T>::infinity()) - a);
    mpfr_sub(diff, ref, hi, MPFR_RNDN);  // ref - exact
    res_out[i] = mpfr_get_d(diff, MPFR_RNDN) / ulp;
  }
  mpfr_clear(x);
  mpfr_clear(hi);
  mpfr_clear(ref);
  mpfr_clear(diff);
  mpfr_set_emin(emin);
  mpfr_set_emax(emax);
}

// kTargetMpfr op; the Python side calls its kernels with two output buffers.
template <MpfrFn Fn>
Operation MakeMpfr(const char* name) {
  Operation op{name, FuncID::kUnary, kTargetMpfr, 0, {}};
  op.data[op.ndata++] =
      FuncData{reinterpret_cast<uintptr_t>(&MpfrRefResidual<Fn, float>), 0,
               TypeID::kFloat32};
  op.data[op.ndata++] =
      FuncData{reinterpret_cast<uintptr_t>(&MpfrRefResidual<Fn, double>), 0,
               TypeID::kFloat64};
  return op;
}

}  // namespace npsr::py

#endif  // NPSR_PY_PYEXT_MPFR_H_
