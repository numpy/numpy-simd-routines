// Highway target detection, re-expanded per target by foreach_target.h: each
// pass records only compile-time HWY_NATIVE_FMA / HWY_HAVE_FLOAT64 (no SIMD
// issued, so registrars run safely even on targets this CPU cannot execute).
#if defined(NPSR_PY_HWY_TARGET_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_PY_HWY_TARGET_INL_H_
#undef NPSR_PY_HWY_TARGET_INL_H_
#else
#define NPSR_PY_HWY_TARGET_INL_H_
#endif

#include <cstdint>
#include <vector>

#include "hwy/highway.h"
#include "hwy/targets.h"

namespace npsr::py {
// Declarations only: every target pass re-enters this file; definitions live
// in the HWY_ONCE block below.
struct TargetInfo;
void RegisterTarget(int64_t target_bit, bool has_fma, bool has_float64);
std::vector<TargetInfo>& TargetRegistry();
}  // namespace npsr::py

HWY_BEFORE_NAMESPACE();
namespace npsr::py::HWY_NAMESPACE {
struct TargetRegistrar {
  TargetRegistrar() {
    ::npsr::py::RegisterTarget(HWY_TARGET, HWY_NATIVE_FMA != 0,
                               HWY_HAVE_FLOAT64 != 0);
  }
};
static const TargetRegistrar kTargetRegistrar;
}  // namespace npsr::py::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace npsr::py {

// One detected target this CPU can run, with its compile-time capabilities.
struct TargetInfo {
  int64_t bit;
  const char* name;
  bool has_fma;
  bool has_float64;
};

// Targets both compiled here and runnable on this CPU, in registration order.
// Meyers singleton: alive before any registrar runs.
std::vector<TargetInfo>& TargetRegistry() {
  static std::vector<TargetInfo> registry;
  return registry;
}

void RegisterTarget(int64_t target_bit, bool has_fma, bool has_float64) {
  // Registrars run for every compiled target; keep only what
  // SupportedTargets() confirms is runnable.
  if ((hwy::SupportedTargets() & target_bit) == 0) return;
  TargetRegistry().push_back(
      {target_bit, hwy::TargetName(target_bit), has_fma, has_float64});
}

}  // namespace npsr::py
#endif  // HWY_ONCE

#endif  // NPSR_PY_HWY_TARGET_INL_H_
