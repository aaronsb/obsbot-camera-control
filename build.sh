#!/bin/bash

# OBSBOT Control - Build and Install Script
# This script helps you build and optionally install the application

set -e

# Check if we're in an interactive terminal
if [ -t 1 ]; then
    # Colors for output
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m' # No Color
else
    # No colors for non-interactive output
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

# Configuration
INSTALL_DIR="${XDG_BIN_HOME:-$HOME/.local/bin}"
DESKTOP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICON_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"
BUILD_DIR="build"
BIN_DIR="bin"
PROJECT_NAME="obsbot"

# Print colored message
print_msg() {
    local color=$1
    shift
    echo -e "${color}$@${NC}"
}

# Show usage information
show_usage() {
    echo -e "${GREEN}OBSBOT Control - Build Script${NC}"
    echo -e ""
    echo -e "${YELLOW}Usage:${NC}"
    echo -e "  ./build.sh <command> [--confirm]"
    echo -e ""
    echo -e "${YELLOW}Commands:${NC}"
    echo -e "  ${BLUE}build${NC}"
    echo -e "    Compiles the project in the build/ directory."
    echo -e ""
    echo -e "    What it does:"
    echo -e "    - Creates build/ directory if it doesn't exist"
    echo -e "    - Runs CMake to configure the project"
    echo -e "    - Compiles both GUI and CLI applications"
    echo -e "    - Binaries will be in bin/obsbot-gui and bin/obsbot-cli"
    echo -e ""
    echo -e "    ${YELLOW}Example:${NC} ./build.sh build --confirm"
    echo -e ""
    echo -e "  ${BLUE}install${NC}"
    echo -e "    Builds the project and installs binaries to your local bin directory."
    echo -e ""
    echo -e "    What it does:"
    echo -e "    - Runs the build process (as above)"
    echo -e "    - Copies binaries to ${INSTALL_DIR}"
    echo -e "    - Makes them executable"
    echo -e "    - Installs desktop launcher (appears in your application menu)"
    echo -e "    - Installs application icon"
    echo -e "    - Checks if install directory is in your PATH"
    echo -e "    - Offers to add to PATH if needed (optional, requires your approval)"
    echo -e ""
    echo -e "    ${YELLOW}Example:${NC} ./build.sh install --confirm"
    echo -e ""
    echo -e "  ${BLUE}clean${NC}"
    echo -e "    Removes the build/ directory for a fresh start."
    echo -e ""
    echo -e "    ${YELLOW}Example:${NC} ./build.sh clean --confirm"
    echo -e ""
    echo -e "  ${BLUE}help${NC}"
    echo -e "    Shows this help message."
    echo -e ""
    echo -e "${YELLOW}Options:${NC}"
    echo -e "  ${BLUE}--confirm${NC}"
    echo -e "    Required flag to actually execute the command. Without this flag,"
    echo -e "    the script will only show what it would do without making changes."
    echo -e ""
    echo -e "${YELLOW}Notes:${NC}"
    echo -e "- Install directory: ${INSTALL_DIR}"
    echo -e "- Build directory: ${BUILD_DIR}"
    echo -e "- The script will prompt for confirmation before making PATH changes"
    echo -e "- You can safely run commands without --confirm to see what will happen"
    echo -e ""
}

# Check if directory is in PATH
check_path() {
    if [[ ":$PATH:" == *":$1:"* ]]; then
        return 0
    else
        return 1
    fi
}

# Offer to add directory to PATH
offer_path_update() {
    local dir=$1

    print_msg "$YELLOW" "\n⚠️  The install directory is not in your PATH:"
    print_msg "$BLUE" "   $dir"

    echo ""
    print_msg "$YELLOW" "To use the installed applications from anywhere, add this directory to your PATH."
    echo ""
    print_msg "$NC" "Add the following line to your shell configuration file:"
    echo ""
    print_msg "$GREEN" "  For Bash (add to ~/.bashrc):"
    print_msg "$BLUE" "    export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo ""
    print_msg "$GREEN" "  For Zsh (add to ~/.zshrc):"
    print_msg "$BLUE" "    export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo ""

    read -p "Would you like me to add this to your shell config automatically? [y/N] " -n 1 -r
    echo

    if [[ $REPLY =~ ^[Yy]$ ]]; then
        # Detect shell
        local shell_config=""
        if [ -n "$BASH_VERSION" ]; then
            shell_config="$HOME/.bashrc"
        elif [ -n "$ZSH_VERSION" ]; then
            shell_config="$HOME/.zshrc"
        else
            # Try to detect from SHELL variable
            case "$SHELL" in
                */bash)
                    shell_config="$HOME/.bashrc"
                    ;;
                */zsh)
                    shell_config="$HOME/.zshrc"
                    ;;
                *)
                    print_msg "$RED" "❌ Could not detect shell type. Please add to PATH manually."
                    return 1
                    ;;
            esac
        fi

        print_msg "$YELLOW" "\n📝 I will add the following line to $shell_config:"
        print_msg "$BLUE" "   export PATH=\"\$HOME/.local/bin:\$PATH\""
        echo ""
        read -p "Proceed? [y/N] " -n 1 -r
        echo

        if [[ $REPLY =~ ^[Yy]$ ]]; then
            # Create backup
            cp "$shell_config" "$shell_config.backup.$(date +%Y%m%d_%H%M%S)"
            print_msg "$GREEN" "✓ Created backup: $shell_config.backup.*"

            # Add PATH export
            echo "" >> "$shell_config"
            echo "# Added by OBSBOT Control install script" >> "$shell_config"
            echo "export PATH=\"\$HOME/.local/bin:\$PATH\"" >> "$shell_config"

            print_msg "$GREEN" "✓ Added to $shell_config"
            print_msg "$YELLOW" "\n⚠️  You need to restart your shell or run:"
            print_msg "$BLUE" "   source $shell_config"
        else
            print_msg "$YELLOW" "Skipped. Add to PATH manually when ready."
        fi
    else
        print_msg "$YELLOW" "Skipped. Add to PATH manually when ready."
    fi
}

# Detect Linux distribution
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    elif [ -f /etc/arch-release ]; then
        echo "arch"
    elif [ -f /etc/debian_version ]; then
        echo "debian"
    elif [ -f /etc/redhat-release ]; then
        echo "rhel"
    else
        echo "unknown"
    fi
}

# Get package install command for detected distro
get_install_cmd() {
    local package=$1
    local distro=$(detect_distro)

    case "$distro" in
        arch|manjaro|endeavouros)
            case "$package" in
                cmake) echo "sudo pacman -S cmake" ;;
                build-essential) echo "sudo pacman -S base-devel" ;;
                pkg-config) echo "sudo pacman -S pkgconf" ;;
                qt6-base-dev) echo "sudo pacman -S qt6-base" ;;
                qt6-multimedia-dev) echo "sudo pacman -S qt6-multimedia" ;;
                lsof) echo "sudo pacman -S lsof" ;;
                *) echo "sudo pacman -S $package" ;;
            esac
            ;;
        debian|ubuntu|mint|pop)
            case "$package" in
                *) echo "sudo apt install $package" ;;
            esac
            ;;
        rhel|fedora|centos|rocky|almalinux)
            case "$package" in
                build-essential) echo "sudo dnf groupinstall 'Development Tools'" ;;
                qt6-base-dev) echo "sudo dnf install qt6-qtbase-devel" ;;
                qt6-multimedia-dev) echo "sudo dnf install qt6-qtmultimedia-devel" ;;
                pkg-config) echo "sudo dnf install pkgconfig" ;;
                *) echo "sudo dnf install $package" ;;
            esac
            ;;
        *)
            # Unknown distro - show all options
            case "$package" in
                cmake)
                    echo "Arch: sudo pacman -S cmake | Debian/Ubuntu: sudo apt install cmake | Fedora: sudo dnf install cmake"
                    ;;
                build-essential)
                    echo "Arch: sudo pacman -S base-devel | Debian/Ubuntu: sudo apt install build-essential | Fedora: sudo dnf groupinstall 'Development Tools'"
                    ;;
                pkg-config)
                    echo "Arch: sudo pacman -S pkgconf | Debian/Ubuntu: sudo apt install pkg-config | Fedora: sudo dnf install pkgconfig"
                    ;;
                qt6-base-dev)
                    echo "Arch: sudo pacman -S qt6-base | Debian/Ubuntu: sudo apt install qt6-base-dev | Fedora: sudo dnf install qt6-qtbase-devel"
                    ;;
                qt6-multimedia-dev)
                    echo "Arch: sudo pacman -S qt6-multimedia | Debian/Ubuntu: sudo apt install qt6-multimedia-dev | Fedora: sudo dnf install qt6-qtmultimedia-devel"
                    ;;
                lsof)
                    echo "Arch: sudo pacman -S lsof | Debian/Ubuntu: sudo apt install lsof | Fedora: sudo dnf install lsof"
                    ;;
                *)
                    echo "Package: $package"
                    ;;
            esac
            ;;
    esac
}

# Check for build dependencies
check_dependencies() {
    local distro=$(detect_distro)
    local distro_name=""

    case "$distro" in
        arch|manjaro|endeavouros) distro_name="Arch Linux" ;;
        debian) distro_name="Debian" ;;
        ubuntu) distro_name="Ubuntu" ;;
        fedora) distro_name="Fedora" ;;
        rhel|centos|rocky|almalinux) distro_name="Red Hat/CentOS" ;;
        *) distro_name="Unknown" ;;
    esac

    print_msg "$BLUE" "🔍 Checking build dependencies..."
    if [ "$distro_name" != "Unknown" ]; then
        print_msg "$BLUE" "Detected: $distro_name"
    fi
    echo ""

    local all_ok=true

    # Check for cmake
    if command -v cmake &> /dev/null; then
        local cmake_version=$(cmake --version | head -1 | awk '{print $3}')
        print_msg "$GREEN" "  ✓ CMake ($cmake_version)"
    else
        print_msg "$RED" "  ✗ CMake - NOT FOUND"
        print_msg "$YELLOW" "    Install: $(get_install_cmd cmake)"
        all_ok=false
    fi

    # Check for make
    if command -v make &> /dev/null; then
        print_msg "$GREEN" "  ✓ Make"
    else
        print_msg "$RED" "  ✗ Make - NOT FOUND"
        print_msg "$YELLOW" "    Install: $(get_install_cmd build-essential)"
        all_ok=false
    fi

    # Check for C++ compiler
    if command -v g++ &> /dev/null; then
        local gxx_version=$(g++ --version | head -1 | awk '{print $3}')
        print_msg "$GREEN" "  ✓ C++ Compiler (g++ $gxx_version)"
    elif command -v clang++ &> /dev/null; then
        local clang_version=$(clang++ --version | head -1 | awk '{print $3}')
        print_msg "$GREEN" "  ✓ C++ Compiler (clang++ $clang_version)"
    else
        print_msg "$RED" "  ✗ C++ Compiler - NOT FOUND"
        print_msg "$YELLOW" "    Install: $(get_install_cmd build-essential)"
        all_ok=false
    fi

    # Check for pkg-config
    if command -v pkg-config &> /dev/null; then
        print_msg "$GREEN" "  ✓ pkg-config"
    else
        print_msg "$RED" "  ✗ pkg-config - NOT FOUND"
        print_msg "$YELLOW" "    Install: $(get_install_cmd pkg-config)"
        all_ok=false
    fi

    # Check for Qt6 Core
    if pkg-config --exists Qt6Core 2>/dev/null; then
        local qt6_version=$(pkg-config --modversion Qt6Core)
        print_msg "$GREEN" "  ✓ Qt6 Core ($qt6_version)"
    else
        print_msg "$RED" "  ✗ Qt6 Core - NOT FOUND"
        print_msg "$YELLOW" "    Install: $(get_install_cmd qt6-base-dev)"
        all_ok=false
    fi

    # Check for Qt6 Widgets
    if pkg-config --exists Qt6Widgets 2>/dev/null; then
        print_msg "$GREEN" "  ✓ Qt6 Widgets"
    else
        print_msg "$RED" "  ✗ Qt6 Widgets - NOT FOUND"
        print_msg "$YELLOW" "    Install: $(get_install_cmd qt6-base-dev)"
        all_ok=false
    fi

    # Check for Qt6 Multimedia
    if pkg-config --exists Qt6Multimedia 2>/dev/null; then
        print_msg "$GREEN" "  ✓ Qt6 Multimedia"
    else
        print_msg "$RED" "  ✗ Qt6 Multimedia - NOT FOUND"
        print_msg "$YELLOW" "    Install: $(get_install_cmd qt6-multimedia-dev)"
        all_ok=false
    fi

    # Check for optional but recommended tools
    echo ""
    print_msg "$BLUE" "Optional dependencies:"

    if command -v lsof &> /dev/null; then
        print_msg "$GREEN" "  ✓ lsof (for camera usage detection)"
    else
        print_msg "$YELLOW" "  ⚠ lsof - NOT FOUND (optional, but recommended)"
        print_msg "$BLUE" "    Install: $(get_install_cmd lsof)"
    fi

    echo ""
    if [ "$all_ok" = true ]; then
        print_msg "$GREEN" "✓ All required dependencies are installed!"
        return 0
    else
        print_msg "$RED" "✗ Some required dependencies are missing."
        print_msg "$YELLOW" "\nPlease install the missing packages and try again."
        return 1
    fi
}

# Build the project
do_build() {
    # Check dependencies first
    if ! check_dependencies; then
        echo ""
        print_msg "$RED" "❌ Cannot build: missing dependencies"
        exit 1
    fi

    echo ""
    print_msg "$GREEN" "🔨 Building OBSBOT Control..."

    # Create build directory
    if [ ! -d "$BUILD_DIR" ]; then
        print_msg "$BLUE" "Creating build directory..."
        mkdir -p "$BUILD_DIR"
    fi

    cd "$BUILD_DIR"

    if [ -f "CMakeCache.txt" ]; then
        CACHE_HOME=$(grep -m1 '^CMAKE_HOME_DIRECTORY:INTERNAL=' CMakeCache.txt | cut -d'=' -f2-)
        SOURCE_HOME=$(cd .. && pwd)
        if [ -n "$CACHE_HOME" ] && [ "$CACHE_HOME" != "$SOURCE_HOME" ]; then
            print_msg "$YELLOW" "Detected stale CMake cache from: $CACHE_HOME"
            print_msg "$BLUE" "Cleaning $BUILD_DIR/ to regenerate build files..."
            cd ..
            rm -rf "$BUILD_DIR"
            mkdir -p "$BUILD_DIR"
            cd "$BUILD_DIR"
        fi
    fi

    print_msg "$BLUE" "Running CMake..."
    cmake ..

    print_msg "$BLUE" "Compiling with $(nproc) cores..."
    make -j$(nproc)

    cd ..

    print_msg "$GREEN" "✓ Build complete!"
    print_msg "$NC" "\nBinaries are in:"
    print_msg "$BLUE" "  - $BIN_DIR/obsbot-gui (GUI application)"
    print_msg "$BLUE" "  - $BIN_DIR/obsbot-cli (CLI tool)"
}

# Install the project
do_install() {
    # Build first
    do_build

    print_msg "$GREEN" "\n📦 Installing to $INSTALL_DIR..."

    # Create install directory if it doesn't exist
    if [ ! -d "$INSTALL_DIR" ]; then
        print_msg "$YELLOW" "Install directory doesn't exist."
        print_msg "$BLUE" "Creating: $INSTALL_DIR"
        mkdir -p "$INSTALL_DIR"
    fi

    # Copy GUI binary (always installed)
    print_msg "$BLUE" "Installing GUI application..."
    cp "$BIN_DIR/obsbot-gui" "$INSTALL_DIR/"
    chmod +x "$INSTALL_DIR/obsbot-gui"

    local installed_apps="  - obsbot-gui"

    # Ask about CLI installation
    echo ""
    read -p "Install CLI tool as well? [y/N] " -n 1 -r
    echo

    if [[ $REPLY =~ ^[Yy]$ ]]; then
        print_msg "$BLUE" "Installing CLI tool..."
        cp "$BIN_DIR/obsbot-cli" "$INSTALL_DIR/"
        chmod +x "$INSTALL_DIR/obsbot-cli"
        installed_apps="$installed_apps\n  - obsbot-cli"
    else
        print_msg "$YELLOW" "Skipping CLI installation."
    fi

    # Install desktop launcher
    echo ""
    print_msg "$BLUE" "Installing desktop launcher..."
    mkdir -p "$DESKTOP_DIR"
    mkdir -p "$ICON_DIR"

    # Create desktop file with full path to executable
    sed "s|Exec=obsbot-gui|Exec=$INSTALL_DIR/obsbot-gui|g" \
        "obsbot-control.desktop" > "$DESKTOP_DIR/obsbot-control.desktop"
    chmod +x "$DESKTOP_DIR/obsbot-control.desktop"

    cp "resources/icons/camera.svg" "$ICON_DIR/obsbot-control.svg"

    # Update desktop database if available
    if command -v update-desktop-database &> /dev/null; then
        update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
    fi

    print_msg "$GREEN" "\n✓ Installation complete!"
    print_msg "$NC" "\nInstalled applications:"
    echo -e "$BLUE$installed_apps$NC"
    print_msg "$NC" "\nDesktop launcher installed - check your application menu!"

    # Check if install dir is in PATH
    if ! check_path "$INSTALL_DIR"; then
        offer_path_update "$INSTALL_DIR"
    else
        print_msg "$GREEN" "\n✓ $INSTALL_DIR is already in your PATH"
        print_msg "$NC" "You can run the applications from anywhere:"
        print_msg "$BLUE" "  obsbot-gui"
    fi

    print_msg "$BLUE" "\n==> Virtual camera support is available but not enabled"
    print_msg "$BLUE" "==> To enable: sudo systemctl enable --now obsbot-virtual-camera.service"
    print_msg "$BLUE" "==> Or load manually: sudo modprobe v4l2loopback video_nr=42 card_label=\"OBSBOT Virtual Camera\" exclusive_caps=1"
}

# Clean build directory
do_clean() {
    if [ -d "$BUILD_DIR" ]; then
        print_msg "$YELLOW" "🧹 Removing build directory..."
        rm -rf "$BUILD_DIR"
        print_msg "$GREEN" "✓ Clean complete!"
    else
        print_msg "$YELLOW" "Build directory doesn't exist. Nothing to clean."
    fi
}

# Main script logic
main() {
    if [ $# -eq 0 ]; then
        show_usage
        exit 0
    fi

    local command=$1
    local confirm=false

    # Check for --confirm flag
    if [ $# -gt 1 ] && [ "$2" == "--confirm" ]; then
        confirm=true
    fi

    case "$command" in
        build)
            if [ "$confirm" = false ]; then
                print_msg "$YELLOW" "⚠️  DRY RUN MODE - No changes will be made"
                echo ""
                print_msg "$NC" "This will:"
                print_msg "$BLUE" "  1. Create build/ directory"
                print_msg "$BLUE" "  2. Run CMake to configure the project"
                print_msg "$BLUE" "  3. Compile GUI and CLI applications"
                print_msg "$BLUE" "  4. Place binaries in build/"
                echo ""
                print_msg "$YELLOW" "To actually build, run:"
                print_msg "$GREEN" "  ./build.sh build --confirm"
                exit 0
            fi
            do_build
            ;;

        install)
            if [ "$confirm" = false ]; then
                print_msg "$YELLOW" "⚠️  DRY RUN MODE - No changes will be made"
                echo ""
                print_msg "$NC" "This will:"
                print_msg "$BLUE" "  1. Build the project (see: ./build.sh build)"
                print_msg "$BLUE" "  2. Create $INSTALL_DIR if needed"
                print_msg "$BLUE" "  3. Copy binaries to $INSTALL_DIR"
                print_msg "$BLUE" "  4. Make binaries executable"
                print_msg "$BLUE" "  5. Install desktop launcher to $DESKTOP_DIR"
                print_msg "$BLUE" "  6. Install icon to $ICON_DIR"
                print_msg "$BLUE" "  7. Check if $INSTALL_DIR is in PATH"
                print_msg "$BLUE" "  8. Offer to add to PATH if needed (with your approval)"
                echo ""
                print_msg "$YELLOW" "To actually install, run:"
                print_msg "$GREEN" "  ./build.sh install --confirm"
                exit 0
            fi
            do_install
            ;;

        clean)
            if [ "$confirm" = false ]; then
                print_msg "$YELLOW" "⚠️  DRY RUN MODE - No changes will be made"
                echo ""
                print_msg "$NC" "This will:"
                print_msg "$BLUE" "  1. Remove the entire build/ directory"
                echo ""
                print_msg "$YELLOW" "To actually clean, run:"
                print_msg "$GREEN" "  ./build.sh clean --confirm"
                exit 0
            fi
            do_clean
            ;;

        help|--help|-h)
            show_usage
            ;;

        *)
            print_msg "$RED" "❌ Unknown command: $command"
            echo ""
            show_usage
            exit 1
            ;;
    esac
}

main "$@"
