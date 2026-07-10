// Aggregates the trig data headers. Hand-written: it only forwards includes,
// so there is nothing for Sollya to generate.
//
// Intentionally NOT guarded with #ifndef: it pulls in the Highway target-toggled
// header kpi16-inl.h, which must be re-included once per SIMD target via
// hwy/foreach_target.h. An include-once guard would suppress all but the first
// target pass. The include-once children (constants/approx/reduction) carry
// their own guards and no-op on re-entry.
#include "npsr/lut-inl.h"
#include "npsr/trig/data/constants.h"
#include "npsr/trig/data/kpi16-inl.h"
#include "npsr/trig/data/approx.h"
#include "npsr/trig/data/reduction.h"
