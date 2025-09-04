// Suppress rounding mode information messages that are expected during constant generation
suppressmessage(185, 186); // suppress expected info round-up, round-down

// Helper procedure to format constants array for C++ output
// Takes a type descriptor followed by constant values and formats them
// as a C array with 4 elements per line
procedure KArray_(pArgs = ...) {
  var pT;
  pT = head(pArgs);
  return CArrayT(pT, Constants @ tail(pArgs), 4) @ ";";
};

// Generate C++ header with various π-related constants for Cody-Waite reduction
// These constants enable accurate computation of r = x - n*π with extended precision
//
// The Cody-Waite method splits π into multiple parts:
//   r = x - n*π₁ - n*π₂ - n*π₃ ...
// where each πᵢ has limited precision to ensure exact multiplication
//
// Different versions are provided for:
// - FMA (Fused Multiply-Add) vs non-FMA architectures
// - float vs double precision
// - Various precision requirements
Append(
  // Generic template declaration
  "template <typename T, bool FMA> inline constexpr char kPi[] = {};",
  
  // Float π constants for Low precision implementation
  "template <> inline constexpr float kPi<float, true>[] = " @
  KArray_(Float32, pi, [|RN, 24, 24, 24|]),  // FMA: 3x24-bit pieces (full precision each)
  
  "template <> inline constexpr float kPi<float, false>[] = " @
  KArray_(Float32, pi, [|RD, 11, 11, 11|], [|RN, 24|]), // no FMA: 3x11-bit + 1x24-bit
  // The 11-bit pieces ensure n*πᵢ is exact (no rounding) for |n| < 2^13
  
  // Double π constants for Low precision implementation
  "template <> inline constexpr double kPi<double, true>[] = " @
  KArray_(Float64, pi, [|RN, 53|], [|RD, 53|], [|RU, 53|]), // FMA: Different roundings for error compensation
  
  "template <> inline constexpr double kPi<double, false>[] = " @
  KArray_(Float64, pi, [|RN, 24, 24, 24|], [|RN, 53|]), // no FMA: 3x24-bit + 1x53-bit
  // The 24-bit pieces ensure n*πᵢ is exact for |n| < 2^29
  "",
  
  // Special 35-bit precision π for specific algorithms
  "template <bool FMA> inline constexpr double kPiPrec35[] = " @
  KArray_(Float64, pi, [|RN, 35|], [|RD, 53|]),
  
  "template <> inline constexpr double kPiPrec35<false>[] = " @
  KArray_(Float64, pi, [|RN, 24, 24, 24|]),
  "",
  
  // 2π constants for angle wrapping
  "template <typename T> inline constexpr char kPiMul2[] = {};",
  "template <> inline constexpr float kPiMul2<float>[] = " @
  KArray_(Float32, pi*2, [|RN, 24, 24|]),  // 2x24-bit pieces
  
  "template <> inline constexpr double kPiMul2<double>[] = " @
  KArray_(Float64, pi*2, [|RN, 53, 53|]),  // 2x53-bit pieces
  "" 
);

// Non-FMA version of π/16 for High precision implementation
// Special handling: components are reordered [0,2,3,1] for proper evaluation
// Without FMA, multiplication order matters to minimize rounding errors
vNFma = Constants(pi/16, [|RN, 27, 27|], [|RN, 29|], [|RN, 53|]);
Append(
  "template <bool FMA> inline constexpr double kPiDiv16Prec29[] = " @
  KArray_(Float64, pi/16, [|RN, 53|], [|RN, 29|], [|RN, 53|]),
  
  // Non-FMA version reorders components: [0], [2], [3], [1]
  // This ordering ensures proper evaluation without FMA:
  // r = x - n*π₁/16 - n*π₃/16 - n*π₄/16 - n*π₂/16
  "template <> inline constexpr double kPiDiv16Prec29<false>[] = " @
  CArray([|vNFma[0], vNFma[2], vNFma[3], vNFma[1]|], 4) @ ";",
  "",
  
  // Simple scalar constants
  "template <typename T> inline constexpr char kInvPi = '_';",
  "template <> inline constexpr float kInvPi<float> = " @ single(1/pi) @ "f;",  
  "template <> inline constexpr double kInvPi<double> = " @ double(1/pi) @ ";",
  "",
  
  "template <typename T> inline constexpr char kHalfPi = '_';",
  "template <> inline constexpr float kHalfPi<float> = " @ single(pi/2) @ "f;",
  "template <> inline constexpr double kHalfPi<double> = " @ double(pi/2) @ ";",
  "",
  
  "template <typename T> inline constexpr char k16DivPi = '_';",
  "template <> inline constexpr float k16DivPi<float> = " @ single(16/pi) @ "f;",
  "template <> inline constexpr double k16DivPi<double> = " @ double(16/pi) @ ";",
  ""
); 
// Dump();

WriteCPPHeader("npsr::trig::data");

