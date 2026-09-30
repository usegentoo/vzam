<p align="center">
  <img src="https://raw.githubusercontent.com/usegentoo/vzam/main/preview.png" alt="vzam preview" width="700">
</p>

<h1 align="center">vzam</h1>

<p align="center">
  <b>A lightning-fast, lightweight utility targeted for users of the vxwm window manager.</b>
</p>

<p align="center">
  <a href="#features"><img src="https://img.shields.io/badge/status-active-success.svg" alt="Status"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a>
  <a href="#"><img src="https://img.shields.io/badge/platform-Linux-informational?logo=linux" alt="Platform"></a>
</p>

---

## About

**vzam** is built for minimalism and speed. Designed with a zero-bloat philosophy, it provides a clean, highly customizable experience specifically targeted for users running the **vxwm** window manager and efficient keyboard-driven Linux workflows.

---

## Features

* **vxwm Synergy:** Tailored functionality designed to complement the vxwm environment.
* **Lightweight & Fast:** Minimal resource overhead and instant responsiveness.
* **Source-Configurable:** Easily customize behavior, keybindings, and aesthetics via `config.h`.
* **Dependency-Lite:** Built using standard system libraries without heavy desktop environment requirements.
* **Keyboard-First:** Optimized for smooth, seamless navigation.

---

## Dependencies

Make sure you have the necessary development tools and libraries installed on your system:

* A standard C compiler (`gcc` or `clang`)
* `make`
* `libX11` (or your target display libraries)

### Quick Install for Dependencies

```
# Arch Linux
sudo pacman -S base-devel libx11

# Debian / Ubuntu
sudo apt update
sudo apt install build-essential libx11-dev

# Gentoo
sudo emerge --ask x11-libs/libX11 sys-devel/make sys-devel/gcc

# Nix / NixOS (Temporary shell with dependencies)
nix-shell -p gnumake gcc xorg.libX11
```
---
## Installation

Clone the repository, compile the source, and install it to your system:


# Clone the repository
```
git clone [https://github.com/usegentoo/vzam.git](https://github.com/usegentoo/vzam.git)
cd vzam
```
---
# Compile from source
```
make
```
# Install 
sudo make install
```
---
## Configuration

vzam is configured directly through its source files for ultimate performance:

    Open config.h in your text editor.

    Modify keybindings, colors, or options to fit your setup.

    Recompile and reinstall your changes:

```

sudo make clean install

```
---

## Contributing

Contributions, bug reports, and feature requests are always welcome. Feel free to open an issue or submit a pull request.
License

Distributed under the MIT License. See LICENSE for more details.
