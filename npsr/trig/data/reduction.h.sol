// Precompute int(2^exp × 4/π) with ~96-bit precision (f32) or ~192-bit precision (f64)
// and split them into three chunks: 32-bit chunks for single precision, 64-bit chunks for double precision.
// 
// This generates a lookup table for large range reduction in trigonometric functions.
// The table is used to compute mantissa × (2^exp × 4/π) using wider integer multiplications for precision:
// - f32: 16×16 → 32-bit multiplications
// - f64: 32×32 → 64-bit multiplications
// 
// For input x = mantissa × 2^exp, the algorithm becomes:
// x × 4/π = mantissa × table_lookup[exp], providing high precision without floating-point errors.
// 
// Args:
//     float_size: 32 for f32 or 64 for f64
procedure ReductionTuble_(pT, pOffset) {
  var r, i, j, $;
  SetDisplay(decimal);
  SetPrec(pT.kDigits * 3);
  $.mask = 2^pT.kSize;
  $.scalar = 4 / pi;
  r = [||];
  for i from 0 to pT.kMaxExpBiased + 1 do {
    $.exp_shift = i - pT.kBias + pOffset;
    $._int = LeftShift($.scalar, $.exp_shift);
    $.chunks = [||];
    for j in [|pT.kSize * 2, pT.kSize, 0|] do {
      $.rshift = RightShift($._int, j);
      $.apply_mask = mod($.rshift, $.mask);
      $.chunks = $.chunks @ [|$.apply_mask|]; 
    };
    r = r @ $.chunks;
  };
  r = ToStringCArray(r, pT.kCUintSFX, 3);
  RestorePrec();
  RestoreDisplay();
  return r;
};
 
Append(
  "template <typename T> constexpr T kLargeReductionTable[] = {};",
  "template <> constexpr uint32_t kLargeReductionTable<float>[] = " @
  ReductionTuble_(Float32, 70) @ ";",
  "",
  "template <> constexpr uint64_t kLargeReductionTable<double>[] = " @
  ReductionTuble_(Float64, 137) @ ";",
  ""
);
WriteCPPHeader("npsr::trig::data");
