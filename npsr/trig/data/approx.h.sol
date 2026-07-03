// Suppress rounding mode information messages for irrational table values
suppressmessage(184, 185, 186); // suppress info no rounding, round-up, round-down
suppressmessage(160);           // inequality decided by faithful evaluation

// Generates a 4-element lookup table for fast trigonometric function approximation.
//
// The table format per angle is: [deriv, sigma, high, low]
// where:
//   sigma + deriv = f'(angle)  sigma = sign * 2^k nearest the derivative;
//                              being a power of two, sigma*z is exact for any
//                              small remainder z, confining the interpolation
//                              rounding error to the tiny deriv*z term
//   high  + low   = f(angle)   double-float split of the function value
//
// The values reproduce Intel SVML's AVX-512 high-accuracy sin breakpoint
// tables byte-for-byte (SVML packs the fields in a different, per-precision
// order; this table keeps one order for both precisions). Two conventions
// are per-precision, pinned down by byte comparison against SVML:
//   - low rounding:  float RN full precision / double RZ 24-bit
//   - sigma cut:     bump 2^k -> 2^(k+1) when |derivative| > cut * 2^k,
//                    cut = sqrt(2) for float / 1.5 for double
//
// Two structural tricks keep every rounding and comparison decidable
// (Sollya brackets irrational values but can never prove one is exactly
// zero, nor decide a comparison whose sides are exactly equal):
//   - Quadrant reduction: evaluate sin/cos only in [0, pi/2) and select by
//     quadrant. Where f or f' is exactly 0/+-1 (angle a multiple of pi/2)
//     the reduced angle is the exact rational 0, so sin(0)/cos(0) evaluate
//     symbolically and those entries store clean +0.0.
//   - Cut nudge: scaling the float cut by (1 - 2^-200) resolves the exact
//     tie |derivative| = 2^(-1/2) upward like SVML does; no other entry on
//     either grid comes anywhere near a cut point.
//
// Parameters:
//   pT    - Type descriptor with .kSize, .kDigits, .kRound
//   pQuad - Quadrant offset of the function to approximate:
//           0 for sin (derivative cos), 1 for cos (derivative -sin),
//           using cos(x) = sin(x + pi/2)
procedure ApproxLut4_(pT, pQuad) {
  var r, i, $;
  // Table size: 512 entries for 64-bit, 256 for 32-bit
  // These sizes balance table memory usage with interpolation accuracy:
  // - 256 entries = 1.4° spacing for float (sufficient for 24-bit mantissa)
  // - 512 entries = 0.7° spacing for double (needed for 53-bit mantissa)
  $.num_lut = match pT.kSize
    with 64: (2^9)
    default: (2^8);

  // Low part rounding: full precision RN for float, truncated 24-bit for double
  $.low_round = match pT.kSize
    with 64: ([|24, RZ|])
    default: ([|pT.kDigits, RN|]);

  // Sigma cut: geometric nearest power of two for float, linear for double,
  // nudged below the float tie point (see header)
  $.cut = match pT.kSize
    with 64: (3 / 2)
    default: (sqrt(2));
  $.cut = $.cut * (1 - 2^(-200));

  r = [||];
  for i from 0 to $.num_lut - 1 do {
    // Quadrant reduction: angle = quad*pi/2 + reduced with reduced in [0, pi/2)
    $.quadrant = floor(4 * i / $.num_lut);
    $.reduced = 2 * pi * (i - $.quadrant * $.num_lut / 4) / $.num_lut;
    $.sin_red = sin($.reduced);
    $.cos_red = cos($.reduced);
    $.sin_by_quad = [| $.sin_red, $.cos_red, -$.sin_red, -$.cos_red |];
    $.cos_by_quad = [| $.cos_red, -$.sin_red, -$.cos_red, $.sin_red |];

    // Shift by the function's quadrant offset:
    // f(angle) = sin(angle + pQuad*pi/2), f'(angle) = cos(angle + pQuad*pi/2)
    $.quadrant = $.quadrant + pQuad;
    if ($.quadrant > 3) then $.quadrant = $.quadrant - 4;

    // Exact function value, split into high and low parts:
    // value ≈ high + low with high rounded to type precision
    $.exact = $.sin_by_quad[$.quadrant];
    $.high = pT.kRound($.exact);
    $.low = pT.kRound(round($.exact - $.high, $.low_round[0], $.low_round[1]));

    // Exact derivative for interpolation
    $.deriv_exact = $.cos_by_quad[$.quadrant];

    if ($.deriv_exact == 0) then {
      // Derivative term vanishes (angle at pi/2 or 3pi/2 relative to f)
      $.sigma = 0;
      $.deriv = 0;
    } else {
      // Power-of-2 scale factor nearest the derivative under the cut rule;
      // actual derivative = sigma + stored deriv
      $.k = floor(log2(abs($.deriv_exact)));
      $.sigma = 2.0^$.k;
      if (abs($.deriv_exact) > $.cut * $.sigma) then $.sigma = 2 * $.sigma;
      if ($.deriv_exact < 0) then $.sigma = -$.sigma;
      $.deriv = pT.kRound($.deriv_exact - $.sigma);
    };

    r = r @ [|$.deriv, $.sigma, $.high, $.low|];
  };

  // Format as C array with 4 elements per table entry
  return CArrayT(pT, r, 4) @ ";";
};

// Generate C++ header content with specialized lookup tables
// for both float and double precision sine and cosine
// Template declarations (empty for unsupported types)
Append(
  "template <typename T> inline constexpr char kSinApproxTable[] = {};",
  "template <> inline constexpr float kSinApproxTable<float>[] = ",
  ApproxLut4_(Float32, 0),  // sin table with cos derivative
  "",
  "template <> inline constexpr double kSinApproxTable<double>[] = ",
  ApproxLut4_(Float64, 0),
  "",
  "template <typename T> inline constexpr char kCosApproxTable[] = {};",
  "template <> inline constexpr float kCosApproxTable<float>[] = ",
  ApproxLut4_(Float32, 1),  // cos table with -sin derivative
  "",
  "template <> inline constexpr double kCosApproxTable<double>[] = ",
  ApproxLut4_(Float64, 1),
  ""
);

WriteCPPHeader("npsr::trig::data");
