#!/bin/bash
#
# OBSBOT Control - Quick Installer
#
# This script clones the repository and runs the automated build/install process.
# It will check dependencies and show what needs to be installed if anything is missing.
#

set -e

# Colors for output
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

print_msg() {
    local color=$1
    shift
    echo -e "${color}$@${NC}"
}

print_msg "$GREEN" "═══════════════════════════════════════════════════════"
print_msg "$GREEN" "  OBSBOT Control - Quick Installer"
print_msg "$GREEN" "═══════════════════════════════════════════════════════"
echo ""

# Determine installation directory
INSTALL_BASE="${HOME}/src"
PROJECT_DIR="${INSTALL_BASE}/obsbot-camera-control"

print_msg "$BLUE" "📂 Installation directory: $PROJECT_DIR"
echo ""

# Check if git is installed
if ! command -v git &> /dev/null; then
    print_msg "$RED" "❌ Git is not installed"
    print_msg "$YELLOW" "Please install git first:"
    echo ""
    print_msg "$NC" "  Arch:         sudo pacman -S git"
    print_msg "$NC" "  Debian/Ubuntu: sudo apt install git"
    print_msg "$NC" "  Fedora:       sudo dnf install git"
    echo ""
    exit 1
fi

# Create base directory if needed
if [ ! -d "$INSTALL_BASE" ]; then
    print_msg "$YELLOW" "Creating directory: $INSTALL_BASE"
    mkdir -p "$INSTALL_BASE"
fi

# Clone or update repository
if [ -d "$PROJECT_DIR" ]; then
    print_msg "$YELLOW" "⚠️  Directory already exists: $PROJECT_DIR"
    echo ""
    read -p "Update existing installation? [Y/n] " -n 1 -r </dev/tty
    echo ""

    if [[ $REPLY =~ ^[Nn]$ ]]; then
        print_msg "$YELLOW" "Installation cancelled."
        exit 0
    fi

    print_msg "$BLUE" "📥 Updating repository..."
    cd "$PROJECT_DIR"

    # Check for uncommitted changes
    if ! git diff-index --quiet HEAD -- 2>/dev/null; then
        print_msg "$RED" "❌ ERROR: Repository has uncommitted changes!"
        print_msg "$YELLOW" "Please commit or stash your changes before updating:"
        echo ""
        print_msg "$BLUE" "  git status              # See what changed"
        print_msg "$BLUE" "  git add -A              # Stage all changes"
        print_msg "$BLUE" "  git commit -m 'msg'     # Commit changes"
        echo ""
        print_msg "$YELLOW" "Or to discard local changes:"
        print_msg "$BLUE" "  git reset --hard HEAD   # WARNING: Loses uncommitted work!"
        echo ""
        exit 1
    fi

    git pull
else
    print_msg "$BLUE" "📥 Cloning repository..."
    git clone https://github.com/aaronsb/obsbot-camera-control.git "$PROJECT_DIR"
    cd "$PROJECT_DIR"
fi

echo ""
print_msg "$GREEN" "✓ Repository ready"
echo ""

# Branch selection
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
print_msg "$BLUE" "Current branch: $CURRENT_BRANCH"
echo ""
read -p "Switch to a different branch? [y/N] " -n 1 -r </dev/tty
echo ""

if [[ $REPLY =~ ^[Yy]$ ]]; then
    # Fetch latest remote branches
    print_msg "$BLUE" "Fetching remote branches..."
    git fetch --all --quiet

    # Get all branches (local and remote)
    print_msg "$BLUE" "Available branches:"
    echo ""

    # Local branches
    print_msg "$YELLOW" "Local branches:"
    local_branches=($(git branch --format='%(refname:short)'))
    for i in "${!local_branches[@]}"; do
        branch="${local_branches[$i]}"
        if [ "$branch" = "$CURRENT_BRANCH" ]; then
            print_msg "$GREEN" "  $((i+1)). $branch (current)"
        else
            print_msg "$NC" "  $((i+1)). $branch"
        fi
    done

    local_count=${#local_branches[@]}

    # Remote branches (excluding HEAD and already tracked branches)
    print_msg "$YELLOW" "Remote branches:"
    remote_branches=($(git branch -r --format='%(refname:short)' | grep -v 'HEAD' | grep -v "^origin/$CURRENT_BRANCH$" | sed 's/^origin\///'))
    for i in "${!remote_branches[@]}"; do
        branch="${remote_branches[$i]}"
        # Only show if not already in local branches
        if [[ ! " ${local_branches[@]} " =~ " ${branch} " ]]; then
            print_msg "$NC" "  $((i+local_count+1)). origin/$branch"
        fi
    done

    echo ""
    read -p "Enter branch number (or 'c' to cancel): " branch_choice </dev/tty

    if [[ $branch_choice =~ ^[0-9]+$ ]]; then
        if [ "$branch_choice" -le "$local_count" ]; then
            # Local branch
            selected_branch="${local_branches[$((branch_choice-1))]}"
            if [ "$selected_branch" != "$CURRENT_BRANCH" ]; then
                print_msg "$BLUE" "Switching to local branch: $selected_branch"
                git checkout "$selected_branch"
            else
                print_msg "$YELLOW" "Already on branch $selected_branch"
            fi
        else
            # Remote branch
            remote_idx=$((branch_choice-local_count-1))
            if [ "$remote_idx" -ge 0 ] && [ "$remote_idx" -lt "${#remote_branches[@]}" ]; then
                selected_branch="${remote_branches[$remote_idx]}"
                print_msg "$BLUE" "Checking out remote branch: origin/$selected_branch"
                git checkout -b "$selected_branch" "origin/$selected_branch" 2>/dev/null || git checkout "$selected_branch"
            else
                print_msg "$RED" "Invalid selection"
                exit 1
            fi
        fi
    elif [[ $branch_choice =~ ^[Cc]$ ]]; then
        print_msg "$YELLOW" "Staying on branch: $CURRENT_BRANCH"
    else
        print_msg "$RED" "Invalid input"
        exit 1
    fi
    echo ""
fi

# Run the build script
print_msg "$BLUE" "🔨 Starting build and installation..."
print_msg "$YELLOW" "Note: You'll be prompted to optionally install the CLI tool"
echo ""

# Give user a chance to cancel
sleep 2

# Run the build script with --confirm
if ./build.sh install --confirm; then
    echo ""
    print_msg "$GREEN" "═══════════════════════════════════════════════════════"
    print_msg "$GREEN" "  ✓ Installation Complete!"
    print_msg "$GREEN" "═══════════════════════════════════════════════════════"
    echo ""
    print_msg "$BLUE" "The application should now appear in your system menu."
    print_msg "$NC" "Or run from terminal:"
    print_msg "$GREEN" "  obsbot-gui"
    echo ""
    print_msg "$BLUE" "Repository location: $PROJECT_DIR"
    echo ""
else
    echo ""
    print_msg "$RED" "═══════════════════════════════════════════════════════"
    print_msg "$RED" "  ✗ Installation Failed"
    print_msg "$RED" "═══════════════════════════════════════════════════════"
    echo ""
    print_msg "$YELLOW" "The build script reported errors above."
    print_msg "$YELLOW" "Install any missing dependencies and try again:"
    echo ""
    print_msg "$GREEN" "  cd $PROJECT_DIR"
    print_msg "$GREEN" "  ./build.sh install --confirm"
    echo ""
    exit 1
fi
