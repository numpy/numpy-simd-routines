let
  pkgs = import <nixpkgs> { };
  arch_pkgs = {
    native = pkgs;
    aarch64 = pkgs.pkgsCross.aarch64-multiplatform;
    s390x = pkgs.pkgsCross.s390x;
    ppc64le = pkgs.pkgsCross.powernv;
    armhf = pkgs.pkgsCross.armv7l-hf-multiplatform;
  };
  arch = if builtins.getEnv "NPSR_ARCH" == "" then "native" else builtins.getEnv "NPSR_ARCH";
  venv_path = ".direnv/py/" + arch;
  pkgsCross = arch_pkgs.${arch};
in
pkgsCross.mkShell {
  name = "Numpy SIMD Routines ${arch}";
  packages = with pkgsCross; [
    sollya
    python312
    python312Packages.virtualenv
  ];
  shellHook = ''
    if test ! -d "${venv_path}"; then
      python -m venv ${venv_path}
      source ${venv_path}/bin/activate
      python -m pip install --upgrade pip
      python -m pip install "spin==0.13" 
    else
      source ${venv_path}/bin/activate
    fi
  '';
}
