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
            buildInputs = [
              pkgs.musl
              pkgs.valgrind
            ];
          };
        }
      );
}
