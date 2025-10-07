// Suppress rounding mode information messages and NaN warnings for boundary angles
suppressmessage(184, 185, 186); // suppress info no rounding, round-up, round-down
suppressmessage(419); // expected nan when angle is at specific multiples causing derivative singularities

// Generates a 4-element lookup table for fast trigonometric function approximation.
// 
// This procedure creates optimized lookup tables that store:
// - Function values split into high/low precision components
// - Derivative information with power-of-2 scaling for efficient interpolation
//
// The table format per angle is: [deriv, sigma, high, low]
// where:
//   deriv = actual_derivative - sigma (reduces storage requirements)
//   sigma = 2^k, where k = ceil(log2(|actual_derivative|))
//   high  = main part of function value
//   low   = residual for extended precision
//
// Parameters:
//   pT        - Type descriptor with .kSize, .kDigits, .kRound
//   pFunc     - Function to approximate (sin or cos)
//   pFuncDriv - Derivative of pFunc (cos or -sin)
procedure ApproxLut4_(pT, pFunc, pFuncDriv) {
  var r, i, $;
  // Table size: 512 entries for 64-bit, 256 for 32-bit
  // More entries for double precision to maintain accuracy
  // These sizes balance table memory usage with interpolation accuracy:
  // - 256 entries = 1.4° spacing for float (sufficient for 24-bit mantissa)
  // - 512 entries = 0.7° spacing for double (needed for 53-bit mantissa)
  $.num_lut = match pT.kSize
    with 64: (2^9)
    default: (2^8);
    
  // Low part rounding configuration:
  // - 64-bit: Round to 24 bits with round-to-zero (faster, sufficient for residual)
  // - 32-bit: Use full precision with round-to-nearest
  $.low_round = match pT.kSize 
    with 64: ([|24, RZ|])
    default: ([|pT.kDigits, RN|]);
    
  // Scale factor to convert table index to angle in radians
  $.scale = 2.0 * pi / $.num_lut;
  
  r = [||];
  for i from 0 to $.num_lut - 1 do {
    // Sample angle uniformly distributed from 0 to 2π
    $.angle = i * $.scale;
    
    // Compute exact function value
    $.exact = pFunc($.angle);
    
    // Split into high and low parts for extended precision
    // High part gets the main value rounded to type precision
    $.high = pT.kRound($.exact);
    
    // Low part stores the residual, rounded to reduced precision
    // This allows accurate reconstruction: value ≈ high + low
    $.low = pT.kRound(round($.exact - $.high, $.low_round[0], $.low_round[1]));
    
    // Compute derivative for interpolation
    $.deriv_exact = pFuncDriv($.angle);
    
    // Find power-of-2 scale factor closest to derivative magnitude
    // This allows efficient storage and reconstruction
    $.k = ceil(log2(abs($.deriv_exact)));
    if ($.deriv_exact < 0) then $.k = -$.k;
    
    // Sigma is the power-of-2 scale factor
    $.sigma = 2.0^$.k;
    
    // Store derivative minus sigma (typically a small value)
    // Actual derivative = sigma + stored_deriv
    $.deriv = pT.kRound($.deriv_exact - $.sigma);
    
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
  ApproxLut4_(Float32, sin(x), cos(x)),  // sin table with cos derivative
  "",
  "template <> inline constexpr double kSinApproxTable<double>[] = ",
  ApproxLut4_(Float64, sin(x), cos(x)),
  "",
  "template <typename T> inline constexpr char kCosApproxTable[] = {};",
  "template <> inline constexpr float kCosApproxTable<float>[] = ",
  ApproxLut4_(Float32, cos(x), -sin(x)), // cos table with -sin derivative
  "",
  "template <> inline constexpr double kCosApproxTable<double>[] = ",
  ApproxLut4_(Float64, cos(x), -sin(x)),
  ""
);

WriteCPPHeader("npsr::trig::data");
