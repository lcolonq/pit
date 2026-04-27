{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, ... }@inputs:
    inputs.flake-utils.lib.eachDefaultSystem
      (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          pit = pkgs.pkgsMusl.stdenv.mkDerivation {
            pname = "pit";
            version = "git";
            src = ./.;
            hardeningDisable = ["all"];
            installPhase = ''
              make prefix=$out install
            '';
          };
          wasm32-clang = pkgs.writeShellScriptBin "wasm32-clang" ''
            ${pkgs.llvmPackages.clang-unwrapped}/bin/clang -I${pkgs.llvmPackages.clang}/resource-root/include --target=wasm32-unknown-unknown "$@"
          '';
          pit-wasm = pkgs.stdenv.mkDerivation {
            pname = "pit";
            version = "git";
            src = ./.;
            hardeningDisable = ["all"];
            buildInputs = [ wasm32-clang ];
            buildPhase = ''
              make CC=wasm32-clang libcolonq-pit.a
            '';
            installPhase = ''
              make CC=wasm32-clang prefix=$out install-core
            '';
          };
          pit-windows = pkgs.pkgsCross.mingwW64.stdenv.mkDerivation {
            pname = "pit";
            version = "git";
            src = ./.;
            hardeningDisable = ["all"];
            buildPhase = ''
              make CC=x86_64-w64-mingw32-cc AR=x86_64-w64-mingw32-ar EXE=pit.exe
            '';
            installPhase = ''
              make CC=x86_64-w64-mingw32-cc AR=x86_64-w64-mingw32-ar EXE=pit.exe prefix=$out install
            '';
          };
        in {
          packages = {
            inherit pit;
            default = pit;
            wasm = pit-wasm;
            windows = pit-windows;
          };
          devShells.default = pkgs.mkShell {
            hardeningDisable = ["all"];
            NIX_ENFORCE_NO_NATIVE = "0";
            buildInputs = [
              pkgs.musl
              pkgs.valgrind
              pkgs.universal-ctags
              wasm32-clang
            ];
          };
        }
      );
}
