{
  description = "Flake for direnv";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.systems.url = "github:nix-systems/default";
  inputs.flake-utils = {
    url = "github:numtide/flake-utils";
    inputs.systems.follows = "systems";
  };

  outputs =
    { nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pkgs.mkShell {
          # Uncomment relevant sections!

          # Packages e.g. used in cli
          packages = with pkgs; [
            (pkgs.callPackage ./esp/esp32-toolchain.nix { })

            bison
            cargo-espflash
            cargo-generate
            cmake
            esp-generate
            espflash
            espup
            flex
            git
            gnumake
            gperf
            ncurses5
            ninja
            pkg-config
            probe-rs
            wget
            gcc-unwrapped

            (python3.withPackages (
              p: with p; [
                pip
                virtualenv
              ]
            ))
          ];

          # Dependencies used during runtime
          # pkgs of hosts architecture, e.g. added to "$NIX_LD_FLAGS"
          buildInputs = with pkgs; [

            libusb1
          ];

          # Dependencies used during compiletime
          # pkgs of buildPlatform's architecture, added to "$PATH"
          nativeBuildInputs = with pkgs; [

            libusb1
          ];

          shellHook = ''
            export IDF_PATH=$(pwd)/esp/esp-idf
            export PATH=$IDF_PATH/tools:$PATH
            export IDF_PYTHON_ENV_PATH=$(pwd)/.python_env
            export LD_LIBRARY_PATH=${pkgs.lib.makeLibraryPath [ pkgs.libusb1 ]}

            . $IDF_PATH/export.sh

            if [ ! -e $IDF_PYTHON_ENV_PATH ]; then
              python -m venv $IDF_PYTHON_ENV_PATH
              . $IDF_PYTHON_ENV_PATH/bin/activate
              pip install -r $IDF_PATH/requirements.txt
            else
              . $IDF_PYTHON_ENV_PATH/bin/activate
            fi
          '';
        };
      }
    );
}
