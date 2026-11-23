{
  description = "Native Linux control app for OBSBOT cameras with PTZ, auto-framing, presets, and live preview";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-26.05";
  };

  outputs =
    { self, nixpkgs }:
    let
      # The vendored OBSBOT SDK (libdev.so) is only shipped for x86_64.
      systems = [ "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});

      package =
        {
          lib,
          stdenv,
          cmake,
          qt6,
          autoPatchelfHook,
          patchelf,
          lsof,
        }:

        stdenv.mkDerivation {
          pname = "obsbot-camera-control";
          version = "1.3.0";

          # Not lib.cleanSource: it drops *.so files, including the vendored SDK.
          src = lib.fileset.toSource {
            root = ./.;
            fileset = lib.fileset.unions [
              ./CMakeLists.txt
              ./src
              ./sdk
              ./resources
              ./obsbot-control.desktop
              ./README.md
              ./LICENSE
            ];
          };

          nativeBuildInputs = [
            cmake
            qt6.wrapQtAppsHook
            autoPatchelfHook
            patchelf
          ];

          buildInputs = [
            qt6.qtbase
            qt6.qtmultimedia
            stdenv.cc.cc.lib
          ];

          # CMakeLists.txt has no install rules and writes the binaries to
          # $src/bin, so install by hand, mirroring the PKGBUILD.
          installPhase = ''
            runHook preInstall

            cd ..

            install -Dm755 bin/obsbot-gui $out/bin/obsbot-gui
            install -Dm755 bin/obsbot-cli $out/bin/obsbot-cli

            install -Dm755 sdk/lib/libdev.so.1.0.2 $out/lib/libdev.so.1.0.2
            ln -s libdev.so.1.0.2 $out/lib/libdev.so.1
            ln -s libdev.so.1.0.2 $out/lib/libdev.so

            # The vendored libdev.so carries a RUNPATH pointing at an OBSBOT
            # developer's machine, and the binaries one pointing at the build tree.
            # autoPatchelfHook adds back the store paths they actually need.
            patchelf --remove-rpath $out/bin/obsbot-gui
            patchelf --remove-rpath $out/bin/obsbot-cli
            patchelf --remove-rpath $out/lib/libdev.so.1.0.2

            install -Dm644 obsbot-control.desktop $out/share/applications/obsbot-control.desktop
            install -Dm644 resources/icons/camera.svg \
              $out/share/icons/hicolor/scalable/apps/obsbot-control.svg
            install -Dm644 README.md $out/share/doc/obsbot-camera-control/README.md

            runHook postInstall
          '';

          # obsbot-cli doesn't use Qt; only wrap the GUI, and give it lsof for
          # detecting when the camera is in use by another application.
          dontWrapQtApps = true;
          postFixup = ''
            wrapQtApp $out/bin/obsbot-gui --prefix PATH : ${lib.makeBinPath [ lsof ]}
          '';

          meta = {
            description = "Native Linux control app for OBSBOT cameras with PTZ, auto-framing, presets, and live preview";
            homepage = "https://github.com/aaronsb/obsbot-camera-control";
            license = lib.licenses.mit;
            # libdev.so is a prebuilt binary from the OBSBOT SDK.
            sourceProvenance = with lib.sourceTypes; [
              fromSource
              binaryNativeCode
            ];
            platforms = [ "x86_64-linux" ];
            mainProgram = "obsbot-gui";
          };
        };
    in
    {
      packages = forAllSystems (pkgs: {
        obsbot-camera-control = pkgs.callPackage package { };
        default = self.packages.${pkgs.stdenv.hostPlatform.system}.obsbot-camera-control;
      });

      overlays.default = final: _prev: {
        obsbot-camera-control = final.callPackage package { };
      };

      nixosModules.default =
        {
          config,
          lib,
          pkgs,
          ...
        }:
        let
          cfg = config.programs.obsbot-camera-control;
        in
        {
          options.programs.obsbot-camera-control = {
            enable = lib.mkEnableOption "OBSBOT Camera Control";

            package = lib.mkOption {
              type = lib.types.package;
              default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
              defaultText = lib.literalExpression "obsbot-camera-control.packages.\${system}.default";
              description = "The obsbot-camera-control package to use.";
            };

            virtualCamera = {
              enable = lib.mkEnableOption "the OBSBOT virtual camera (a v4l2loopback device)";

              deviceNumber = lib.mkOption {
                type = lib.types.int;
                default = 42;
                description = "The /dev/videoN number for the virtual camera. The app expects 42.";
              };
            };
          };

          config = lib.mkIf cfg.enable (
            lib.mkMerge [
              { environment.systemPackages = [ cfg.package ]; }

              # Equivalent of the PKGBUILD's modprobe.d config and systemd unit.
              (lib.mkIf cfg.virtualCamera.enable {
                boot.extraModulePackages = [ config.boot.kernelPackages.v4l2loopback ];
                boot.kernelModules = [ "v4l2loopback" ];
                boot.extraModprobeConfig = ''
                  options v4l2loopback video_nr=${toString cfg.virtualCamera.deviceNumber} card_label="OBSBOT Virtual Camera" exclusive_caps=1
                '';
              })
            ]
          );
        };

      apps = forAllSystems (pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${pkgs.stdenv.hostPlatform.system}.default}/bin/obsbot-gui";
        };
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.default ];
        };
      });
    };
}
