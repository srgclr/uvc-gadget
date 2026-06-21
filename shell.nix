{ pkgs ? import <nixpkgs> {} }:

# buildFHSEnv wraps every command in a bubblewrap namespace that presents a
# standard FHS tree (/bin, /usr, etc.).  Buildroot hard-codes paths like
# /bin/true and /usr/bin/file in configure scripts; without this none of
# those checks pass on a bare NixOS host.
#
# Interactive use:
#   $(nix-build shell.nix --no-out-link)/bin/buildroot-env
#
# Automated use (see build.sh, which calls nix-build internally):
#   ./build.sh
pkgs.buildFHSEnv {
  name = "buildroot-env";

  targetPkgs = p: with p; [
    # Core build tools
    gnumake
    gcc
    binutils
    glibc.dev

    # Download
    wget
    curl

    # Scripting / interpreted
    python3
    perl
    bash

    # Kernel build requirements
    bison
    flex
    openssl
    bc

    # Build infrastructure
    pkg-config
    libtool
    autoconf
    automake
    gettext

    # File utilities
    file
    which
    cpio
    rsync
    unzip
    patch
    diffutils
    findutils
    gawk
    gnused
    gnugrep
    coreutils
    gzip
    bzip2
    xz
    lzop

    # For generating the SD-card image
    mtools
    dosfstools
    e2fsprogs
    util-linux
    genimage

    # ncurses for menuconfig
    ncurses

    # Misc
    texinfo
    git
  ];

  # profile runs inside the FHS env before the user's command
  profile = ''
    export FORCE_UNSAFE_CONFIGURE=1
    export PYTHONDONTWRITEBYTECODE=1
    export FILE=/usr/bin/file
  '';

  # Default interactive shell
  runScript = "bash";
}
