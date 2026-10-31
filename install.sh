#!/bin/bash

# Linux only. Not ported to Windows. macOS is untested.
# a2 does not call OpenSSL. Do not add libssl just because libcurl can use it.
set -e

# --- Variables ---
VTERM_REPO_URL="https://github.com/TragicWarrior/libvterm.git"

# --- Helper Functions ---
info() {
    echo -e "\033[1;34m[INFO]\033[0m $1"
}

error() {
    echo -e "\033[1;31m[ERROR]\033[0m $1" >&2
    exit 1
}

# --- Dependency Installation ---
install_dependencies() {
    info "Checking and installing system dependencies..."
    
    if ! command -v sudo &> /dev/null; then
        error "'sudo' command not found. Please install sudo or run this script as root."
    fi
    if ! command -v git &> /dev/null; then
        error "'git' command not found. Please install git."
    fi

    if command -v apt-get &> /dev/null; then
        info "Detected APT package manager (Debian/Ubuntu)."
        REQUIRED_PACKAGES="build-essential gcc make pkg-config libncursesw5-dev libjansson-dev libcurl4-openssl-dev libhunspell-dev hunspell git cmake curl"
        PACKAGES_TO_INSTALL=""
        for pkg in $REQUIRED_PACKAGES; do
            if ! dpkg -s "$pkg" &> /dev/null; then
                PACKAGES_TO_INSTALL="$PACKAGES_TO_INSTALL $pkg"
            fi
        done

        if [ -z "$PACKAGES_TO_INSTALL" ]; then
            info "All APT dependencies are already installed. Skipping."
        else
            info "Missing APT dependencies:$PACKAGES_TO_INSTALL"
            info "Running apt-get to install them..."
            sudo apt-get update
            sudo apt-get install -y $PACKAGES_TO_INSTALL
        fi
    elif command -v dnf &> /dev/null; then
        info "Detected DNF package manager (Fedora)."
        REQUIRED_PACKAGES="gcc make pkgconf-pkg-config ncurses-devel jansson-devel libcurl-devel hunspell-devel git cmake curl"
        PACKAGES_TO_INSTALL=""
        for pkg in $REQUIRED_PACKAGES; do
            if ! rpm -q "$pkg" &> /dev/null; then
                PACKAGES_TO_INSTALL="$PACKAGES_TO_INSTALL $pkg"
            fi
        done

        if [ -z "$PACKAGES_TO_INSTALL" ]; then
            info "All DNF dependencies are already installed. Skipping."
        else
            info "Missing DNF dependencies:$PACKAGES_TO_INSTALL"
            info "Running dnf to install them..."
            sudo dnf install -y $PACKAGES_TO_INSTALL
        fi
    elif command -v pacman &> /dev/null; then
        info "Detected Pacman package manager (Arch Linux)."
        REQUIRED_PACKAGES="base-devel ncurses jansson curl hunspell git cmake"
        PACKAGES_TO_INSTALL=""
        for pkg in $REQUIRED_PACKAGES; do
            if ! pacman -Q "$pkg" &> /dev/null; then
                PACKAGES_TO_INSTALL="$PACKAGES_TO_INSTALL $pkg"
            fi
        done
        
        if [ -z "$PACKAGES_TO_INSTALL" ]; then
            info "All Pacman dependencies are already installed. Skipping."
        else
            info "Missing Pacman dependencies:$PACKAGES_TO_INSTALL"
            info "Running pacman to install them..."
            sudo pacman -Syu --noconfirm $PACKAGES_TO_INSTALL
        fi
    elif command -v zypper &> /dev/null; then
        info "Detected Zypper package manager (openSUSE)."
        REQUIRED_PACKAGES="gcc make pkg-config ncurses-devel libjansson-devel libcurl-devel hunspell-devel git cmake curl"
        PACKAGES_TO_INSTALL=""
        for pkg in $REQUIRED_PACKAGES; do
            if ! rpm -q "$pkg" &> /dev/null; then
                PACKAGES_TO_INSTALL="$PACKAGES_TO_INSTALL $pkg"
            fi
        done

        if [ -z "$PACKAGES_TO_INSTALL" ]; then
            info "All Zypper dependencies are already installed. Skipping."
        else
            info "Missing Zypper dependencies:$PACKAGES_TO_INSTALL"
            info "Running zypper to install them..."
            sudo zypper install -y $PACKAGES_TO_INSTALL
        fi
    elif command -v apk &> /dev/null; then
        info "Detected APK package manager (Alpine Linux)."
        REQUIRED_PACKAGES="build-base pkgconf ncurses-dev jansson-dev curl-dev hunspell-dev git cmake curl"
        PACKAGES_TO_INSTALL=""
        for pkg in $REQUIRED_PACKAGES; do
            if [ -z "$(apk info -e "$pkg")" ]; then
                PACKAGES_TO_INSTALL="$PACKAGES_TO_INSTALL $pkg"
            fi
        done

        if [ -z "$PACKAGES_TO_INSTALL" ]; then
            info "All APK dependencies are already installed. Skipping."
        else
            info "Missing APK dependencies:$PACKAGES_TO_INSTALL"
            info "Running apk to install them..."
            sudo apk add --no-cache $PACKAGES_TO_INSTALL
        fi
    else
        error "Unsupported package manager. Please install dependencies manually."
    fi
    info "System dependencies check complete."
}

# --- Install libvterm from Source ---
install_libvterm_from_source() {
    info "Checking for libvterm installation..."

    local has_header=false
    local has_lib=false

    for h in /usr/include/vterm.h /usr/local/include/vterm.h /usr/include/*/vterm.h; do
        if [ -f "$h" ]; then
            has_header=true
            break
        fi
    done

    if [ -f "/usr/local/lib/libvterm.so" ] || \
       [ -f "/usr/local/lib64/libvterm.so" ] || \
       [ -f "/usr/lib/libvterm.so" ] || \
       [ -f "/usr/lib64/libvterm.so" ] || \
       [ -f "/usr/lib/x86_64-linux-gnu/libvterm.so" ] || \
       pkg-config --exists vterm 2>/dev/null || \
       pkg-config --exists libvterm 2>/dev/null; then
        has_lib=true
    fi

    if [ "$has_header" = true ] && [ "$has_lib" = true ]; then
        info "libvterm and development headers are already installed. Skipping."
        return
    fi

    info "Cloning and building libvterm from $VTERM_REPO_URL..."
    TMP_DIR=$(mktemp -d)
    git clone --depth 1 "$VTERM_REPO_URL" "$TMP_DIR"
    cd "$TMP_DIR"

    # Only create ncursesw compatibility symlink if directory doesn't already exist
    if [ ! -d "/usr/include/ncursesw" ] && [ -d "/usr/include" ]; then
        if [ -f "/usr/include/curses.h" ] || [ -f "/usr/include/ncurses.h" ]; then
            sudo ln -sfn /usr/include /usr/include/ncursesw 2>/dev/null || true
        fi
    fi

    info "Configuring project with CMake..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
    
    info "Compiling libvterm..."
    cmake --build build -j"$(nproc 2>/dev/null || echo 2)"
    
    info "Installing libvterm on the system..."
    sudo cmake --install build
    
    cd - > /dev/null
    rm -rf "$TMP_DIR"
    
    info "Updating system dynamic library cache..."
    if [ -d "/etc/ld.so.conf.d" ] && [ ! -f "/etc/ld.so.conf.d/local.conf" ] && [ ! -f "/etc/ld.so.conf.d/libc.conf" ]; then
        echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/local.conf > /dev/null 2>&1 || true
    fi
    sudo ldconfig

    info "libvterm installed successfully."
}

# stb_image.h is vendored in the repository. Only fetch it if this tree does not have it.
install_stb_image() {
    if [ -f "a2_files/stb_image.h" ]; then
        info "a2_files/stb_image.h already present. Not downloading another copy."
        return
    fi
    info "a2_files/stb_image.h is missing. Downloading stb_image.h..."
    mkdir -p a2_files
    curl -sL "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" -o a2_files/stb_image.h
    info "stb_image.h installed in a2_files/."
}

# --- Compile Project ---
compile_project() {
    info "Compiling a2 and its plugins..."
    info "Plugins are written to ~/.a2/plugins because that is where the editor loads them."
    make clean
    make
    info "Installing under /usr/local (binary, syntaxes, themes, help texts, ds.a2)..."
    sudo make install
    info "Installed. Run 'a2' from anywhere."
}

# --- Main Execution ---
main() {
    install_dependencies
    install_libvterm_from_source
    install_stb_image
    compile_project
}

main "$@"
