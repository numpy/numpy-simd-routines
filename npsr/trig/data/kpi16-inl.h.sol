// Generates lookup table for sin(k·π/16) and cos(k·π/16) values
// Used in the high-precision trigonometric implementation for range reduction
//
// This table supports the algorithm where input x is reduced to:
//   x = n*(π/16) + r, where |r| < π/16
// Then sin(x) and cos(x) are reconstructed using angle addition formulas
//
// Parameters:
//   pT    - Type descriptor (Float64 in this case)
//   pFunc - Function to evaluate (sin or cos)
//   pBy   - Divisor for π (16 in this case, giving π/16 intervals)
procedure PiDivTable_(pT, pFunc, pBy) {
  var r, i, pi_by;
  pi_by = pi / pBy;
  r = [||];
  
  // Generate function values at k*π/16 for k = 0, 1, ..., 15
  for i from 0 to pBy - 1 do {
    r = r :. pT.kRound(pFunc(i * pi_by));
  };
  
  // Format as C array with 4 elements per line
  return CArrayT(pT, r, 4);
};

// Generates packed low-precision parts of sin and cos values
// This packing scheme saves memory by storing two 32-bit values in one 64-bit word
//
// The packing works as follows for double (64-bit):
// - sin_low occupies bits [31:0] (lower 32 bits)
// - cos_low occupies bits [63:32] (upper 32 bits)
//
// This is why in the C++ code:
// - cos_lo can be used directly (it's already in the upper bits)
// - sin_lo needs to be extracted with a 32-bit left shift
procedure PiDivPackLowTable_(pT, pFunc0, pFunc1, pBy) {
  var r, i, digits, $;
  $.pi_by = pi / pBy;
  r = [||];
  
  // First, compute the low precision parts (residuals after high precision)
  for i from 0 to pBy - 1 do {
    $.hi0 = pT.kRound(pFunc0(i * $.pi_by));  // High precision sin
    $.hi1 = pT.kRound(pFunc1(i * $.pi_by));  // High precision cos
    // Low precision parts: exact value minus high precision part
    $.hi0_low = pT.kRound(pFunc0(i * $.pi_by) - $.hi0);  // sin_low
    $.hi1_low = pT.kRound(pFunc1(i * $.pi_by) - $.hi1);  // cos_low
    r = r @ [|$.hi0_low, $.hi1_low|];
  }; 
  
  // Convert to binary representation for bit manipulation
  digits = ToDigits(pT, r);
  $.half_size = pT.kSize / 2;  // 32 for double
  $.lower_bits = 2^$.half_size;  // Mask for lower 32 bits
  
  r = [||];
  // Pack pairs of values into single 64-bit words
  for i from 0 to length(digits) - 1 by 2 do {
    $.hi0 = digits[i];     // sin_low bits
    $.hi1 = digits[i + 1]; // cos_low bits
    $.pack = mod(RightShift($.hi0, $.half_size), $.lower_bits);
    $.pack = $.pack + $.hi1 - mod($.hi1, $.lower_bits);
    r = r :. $.pack; 
  };
  
  // Convert back from binary representation
  r = FromDigits(pT, r);
  return CArrayT(pT, r, 4);
};

Append(
  "inline constexpr auto kKPi16Table = MakeLut<double>(",
  "// High parts of sin(k·π/16) where k = 0, 1, ..., 15",
  PiDivTable_(Float64, sin(x), 16) @ ",",
  "// High parts of cos(k·π/16) where k = 0, 1, ..., 15",
  PiDivTable_(Float64, cos(x), 16) @ ",",
  "// Lower parts of sin(k·π/16) and cos(k·π/16) packed together",
  "// Format: bits [63:32] = cos_low, bits [31:0] = sin_low",
  "// This packing saves 16×8 = 128 bytes of memory",
  PiDivPackLowTable_(Float64, sin(x), cos(x), 16),
  "",
  ");"
);

WriteHighwayHeader("npsr::HWY_NAMESPACE::trig");

