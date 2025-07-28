var header;
for header in [|"constants", "high", "approx", "reduction"|] do {
  Append(
    "#include \"npsr/trig/data/" @ header @ ".h\""
  );
};

WriteCPPHeader();


