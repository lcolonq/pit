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
        in {
          packages = {
            inherit pit;
            default = pit;
          };
          devShells.default = pkgs.mkShell {
            hardeningDisable = ["all"];
            NIX_ENFORCE_NO_NATIVE = "0";
            buildInputs = [
              pkgs.musl
              pkgs.valgrind
              pkgs.universal-ctags
            ];
          };
        }
      );
}
