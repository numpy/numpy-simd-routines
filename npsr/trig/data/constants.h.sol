procedure ConstantsToArrayF32_(pArgs = ...) {
  return ToStringCArray(ConstantsFromArray(pArgs), "f", 4);
};
procedure ConstantsToArrayF64_(pArgs = ...) {
  return ToStringCArray(ConstantsFromArray(pArgs), "", 4);
};

Append(
  "template <typename T, bool FMA> constexpr char kPi[] = {};",

  "template <> constexpr float kPi<float, true>[] = " @
  ConstantsToArrayF32_(pi, [|RN, 24, 24, 24|]),
  "template <> constexpr float kPi<float, false>[] = " @
  ConstantsToArrayF32_(pi, [|RD, 11, 11, 11|], [|RN, 24|]), // no FMA
  
  
  "template <> constexpr double kPi<double, true>[] = " @
  ConstantsToArrayF64_(pi, [|RN, 53|], [|RD, 53|], [|RU, 53|]),
  "template <> constexpr double kPi<double, false>[] = " @
  ConstantsToArrayF64_(pi, [|RN, 24, 24, 24|], [|RN, 53|]), // no FMA
  
  ""
);

Append(
  "template <bool FMA> constexpr double kPiPrec35[] = " @
  ConstantsToArrayF64_(pi, [|RN, 35|], [|RD, 53|]),
  "template <> constexpr double kPiPrec35<false>[] = " @
  ConstantsToArrayF64_(pi, [|RN, 24, 24, 24|]),
  ""
);

Append(
  "template <typename T> constexpr char kPiMul2[] = {};",
  
  "template <> constexpr float kPiMul2<float>[] = " @
  ConstantsToArrayF32_(pi*2, [|RN, 24, 24|]),
  "template <> constexpr double kPiMul2<double>[] = " @
  ConstantsToArrayF64_(pi*2, [|RN, 53, 53|]),
  "" 
);

vNFma = Constants(pi/16, [|RN, 27, 27|], [|RN, 29|], [|RN, 53|]);
Append(
  "template <bool FMA> constexpr double kPiDiv16Prec29[] = " @
  ConstantsToArrayF64_(pi/16, [|RN, 53|], [|RN, 29|], [|RN, 53|]),
  "template <> constexpr double kPiDiv16Prec29<false>[] = " @
  ToStringCArray([|vNFma[0], vNFma[2], vNFma[3], vNFma[1]|], "", 4),
  ""
);

Append(
  "template <typename T> constexpr char kInvPi = '_';",
  "template <> constexpr float kInvPi<float> = " @
  single(1/pi) @ "f;",
  
  "template <> constexpr double kInvPi<double> = " @
  double(1/pi) @ ";",
  ""
); 

Append(
  "template <typename T> constexpr char kHalfPi = '_';",
  
  "template <> constexpr float kHalfPi<float> = " @
  single(pi/2) @ "f;",
  
  "template <> constexpr double kHalfPi<double> = " @
  double(pi/2) @ ";",
  "" 
);

Append(
  "template <typename T> constexpr char k16DivPi = '_';",
  
  "template <> constexpr float k16DivPi<float> = " @
  single(16/pi) @ "f;",

  "template <> constexpr double k16DivPi<double> = " @
  double(16/pi) @ ";",
  ""
); 

// Dump();

WriteCPPHeader("npsr::trig::data");

