suppressmessage(186, 185, 184);

procedure ApproxLut4_(pT, pFunc, pFuncDriv) {
  var r, i, $;
  
  $.num_lut = match pT.kSize
    with 64: (2^9)
    default: (2^8);

  $.low_round = match pT.kSize 
    with 64: ([|24, RZ|])
    default: ([|pT.kDigits, RN|]);
  $.scale = 2.0 * pi / $.num_lut;
  
  r = [||];
  for i from 0 to $.num_lut - 1 do {
    $.angle = i * $.scale;
    $.exact = pFunc($.angle);
    $.high = pT.kRound($.exact);
    $.low = pT.kRound(round($.exact - $.high, $.low_round[0], $.low_round[1]));
    
    $.deriv_exact = pFuncDriv($.angle);
    $.k = ceil(log2(abs($.deriv_exact)));
    if ($.deriv_exact < 0) then $.k = -$.k;
    
    $.sigma = 2.0^$.k;
    $.deriv = pT.kRound($.deriv_exact - $.sigma);
    r = r @ [|$.deriv, $.sigma, $.high, $.low|];
  };
  return ToStringCArray(r, pT.kCSFX, 4);
};

Append(
  "template <typename T> constexpr char kSinApproxTable[] = {};",
  "template <> constexpr float kSinApproxTable<float>[] = ",
  ApproxLut4_(Float32, sin(x), cos(x)),
  "",
  "template <> constexpr double kSinApproxTable<double>[] = ",
  ApproxLut4_(Float64, sin(x), cos(x)),
  ""
);
Append(
  "template <typename T> constexpr char kCosApproxTable[] = {};",
  "template <> constexpr float kCosApproxTable<float>[] = ",
  ApproxLut4_(Float32, cos(x), -sin(x)),
  "",
  "template <> constexpr double kCosApproxTable<double>[] = ",
  ApproxLut4_(Float64, cos(x), -sin(x)),
  ""
);

WriteCPPHeader("npsr::trig::data");
