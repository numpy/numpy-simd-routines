// Generates lookup tables for high-precision argument reduction in trigonometric functions
//
// For large arguments, we need to compute x mod 2π (or x mod π/2) accurately.
// This is done by multiplying x by 4/π and extracting the fractional part.
// The table stores precomputed shifted values of 4/π for different exponents.
//
// The technique is based on Payne-Hanek reduction.
//
// Parameters:
//   pT      - Type descriptor (Float32 or Float64)
//   pOffset - Additional shift offset (70 for float, 137 for double)
//             These magic constants position the bits of 4/π correctly
//             for the extended precision multiplication scheme
procedure ReductionTable_(pT, pOffset) {
  var r, i, j, $;
  SetDisplay(decimal);
  SetPrec(pT.kDigits * 3);  // Triple precision to avoid rounding errors
  // Mask for extracting chunks of the specified bit size
  $.mask = 2^pT.kSize;  // 2^32 for float, 2^64 for double
  // The constant 4/π is key to the reduction algorithm
  // x mod 2π = fractional_part(x * 4/π) * π/2
  $.scalar = 4 / pi;
  r = [||];
  for i from 0 to pT.kMaxExpBiased + 1 do {
    // Calculate the effective shift for this exponent
    // The shift positions the bits of 4/π to align with the mantissa of x
    // Float: exp_shift = i - 127 + 70 = i - 57
    // Double: exp_shift = i - 1023 + 137 = i - 886
    $.exp_shift = i - pT.kBias + pOffset;
    // Shift 4/π left by exp_shift bits to get the relevant portion
    // This gives us the bits of 4/π that will multiply with x's mantissa
    $._int = LeftShift($.scalar, $.exp_shift);
    // Extract three chunks for extended precision
    // Each chunk is either 32 or 64 bits depending on the type
    $.chunks = [||];
    for j in [|pT.kSize * 2, pT.kSize, 0|] do {
      $.rshift = RightShift($._int, j);
      // Mask to get only the lower bits (32 or 64)
      $.apply_mask = mod($.rshift, $.mask);
      $.chunks = $.chunks @ [|$.apply_mask|]; 
    };
    // Format: [high_chunk, middle_chunk, low_chunk]
    r = r @ $.chunks;
  };
  r = CArrayTU(pT, r, 3) @ ";";
  RestorePrec();
  RestoreDisplay();
  return r;
};

Append(
  "template <typename T> inline constexpr T kLargeReductionTable[] = {};",
  // The offset 70 means we extract 4/π bits starting from position (exp - 57)
  // This aligns with the fractional extraction: 9 + 5 + 18 + 14 = 46 = 2×23 bits
  "template <> inline constexpr uint32_t kLargeReductionTable<float>[] = " @
  ReductionTable_(Float32, 70),
  "",
  // The offset 137 means we extract 4/π bits starting from position (exp - 886)
  // This aligns with the fractional extraction: 12 + 28 + 24 + 40 = 104 = 2×52 bits
  "template <> inline constexpr uint64_t kLargeReductionTable<double>[] = " @
  ReductionTable_(Float64, 137),
  ""
);

WriteCPPHeader("npsr::trig::data");

