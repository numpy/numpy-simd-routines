// MPFR reference oracles for all trig ops (numpy_sr.mpfr.*): one plugin
// library, no Highway targets; npsr_py_load comes from pyext.cc.
#include "numpy_sr/pyext-mpfr.h"

namespace npsr::py {
static RegisteredOperation ops{MakeMpfr<mpfr_sin>("sin"),
                               MakeMpfr<mpfr_cos>("cos")};
}  // namespace npsr::py
