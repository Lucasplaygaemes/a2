#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
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
        REQUIRED_PACKAGES="build-essential libncursesw5-dev libjansson-dev libcurl4-openssl-dev libssl-dev git cmake"
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
        REQUIRED_PACKAGES="gcc ncurses-devel jansson-devel libcurl-devel openssl-devel git cmake glibc-devel"
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
        REQUIRED_PACKAGES="base-devel ncurses jansson libcurl-openssl openssl git cmake"
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
    else
        error "Unsupported package manager. Please install dependencies manually."
    fi
    info "System dependencies check complete."
}

# --- Install libvterm from Source ---
install_libvterm_from_source() {
    info "Checking for libvterm installation..."
    if [ -f "/usr/local/lib/libvterm.so" ]; then
        info "libvterm already seems to be installed. Skipping."
        return
    fi
    
    info "Cloning and installing libvterm from $VTERM_REPO_URL..."
    TMP_DIR=$(mktemp -d)
    git clone "$VTERM_REPO_URL" "$TMP_DIR"
    cd "$TMP_DIR"
    
    info "Configuring project with CMake..."
    cmake .
    
    info "Compiling libvterm..."
    make
    
    info "Installing libvterm on the system..."
    sudo make install
    
    cd - # Return to the original directory
    rm -rf "$TMP_DIR"
    
    info "Updating the system's library cache..."
    sudo ldconfig

    info "libvterm installed successfully."
}

# --- Compile Project ---
compile_project() {
    info "Compiling a2..."
    make clean
    make rebuild
    info "Compilation successful."
}

# --- Install Binaries ---
install_binaries() {
    info "Installing a2 binary to /usr/local/bin/..."
    if [ ! -f "a2" ]; then
        error "'a2' binary not found. Compilation may have failed."
    fi
    sudo cp a2 /usr/local/bin/a2

    info "Installing syntax files to /usr/local/share/a2/syntaxes/..."
    if [ ! -d "syntaxes" ]; then
        info "Warning: 'syntaxes' directory not found. Skipping syntax file installation."
    else
        sudo mkdir -p /usr/local/share/a2/syntaxes
        sudo cp -r syntaxes/* /usr/local/share/a2/syntaxes/
    fi
    
    info "Installation complete!"
    info "You can now run 'a2' from anywhere in your terminal."
}

# --- Main Execution ---
main() {
    install_dependencies
    install_libvterm_from_source
    compile_project
    install_binaries
}

main "$@"
