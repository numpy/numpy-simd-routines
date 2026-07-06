// npsr Sin test library: one Operation registered per Highway target;
// npsr_py_load comes from pyext.cc, the MPFR oracle from mpfr.cc.
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "npsr/trig/tests/sin.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "numpy_sr/pyext-inl.h"

HWY_BEFORE_NAMESPACE();
namespace npsr::py::HWY_NAMESPACE {
struct SinFn {
  template <class P, class V>
  V operator()(P& p, V v) const {
    return ::npsr::HWY_NAMESPACE::Sin(p, v);
  }
};
static RegisteredOperation reg_sin{MakeUnaryOperation<SinFn>("sin")};
}  // namespace npsr::py::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();
