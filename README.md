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

```bash
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

```bash
# Clone the repository
git clone https://github.com/YOUR_USERNAME/vzam.git
cd vzam

# Compile from source
make

# Install 
sudo make install

```

---

## Configuration

`vzam` is configured directly through its source files for ultimate performance:

1. Open `config.h` in your text editor.
2. Modify keybindings, colors, or options to fit your setup.
3. Recompile and reinstall your changes:

```bash
sudo make clean install

```

---

## Contributing

Contributions, bug reports, and feature requests are always welcome. Feel free to open an issue or submit a pull request.

---

## License

Distributed under the MIT License. See `LICENSE` for more details.
