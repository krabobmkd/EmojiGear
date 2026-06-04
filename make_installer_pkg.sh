#!/bin/sh
# make_installer_pkg.sh
#
# Assembles the EmojiGear installer package directory from the built
# binaries and project sources.  Run this on Linux after a successful
# cmake --build buildAmiga.
#
# Usage:
#   ./make_installer_pkg.sh [output_dir]
#
# Default output: ./EmojiGear_pkg  (deleted and recreated each run)
#
# The resulting directory can be archived as an LHA for distribution:
#   cd EmojiGear_pkg && lha a ../EmojiGear_3.1.lha *
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PKG="${1:-$SCRIPT_DIR/EmojiGear_pkg}"

BUILD="$SCRIPT_DIR/buildAmiga"
SRC="$SCRIPT_DIR/EmojiGear"
FONTS_SRC="$SCRIPT_DIR/Fonts"
EXAMPLES_SRC="$SCRIPT_DIR/examples_text"
INSTALLER_SRC="$SCRIPT_DIR/Installer"
SDK_SRC="$SCRIPT_DIR/sdk"

# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------
check_file() {
    if [ ! -f "$1" ]; then
        echo "ERROR: required file not found: $1" >&2
        echo "       Run 'cmake --build buildAmiga' first." >&2
        exit 1
    fi
}

check_file "$BUILD/libutf8rastport/utf8rastport.library"
check_file "$BUILD/UniButtonGadget/unibutton.gadget"
check_file "$BUILD/UniTextEditorGadget/unitexteditor.gadget"
check_file "$BUILD/EmojiGear"
check_file "$BUILD/MUImojiGear/MUImojiGear"
check_file "$BUILD/MUImojiGear/MUImojiGear.info"
check_file "$BUILD/examples/testMUIUniTextEditor"
check_file "$BUILD/examples/TestUtf8DrawText"
check_file "$BUILD/examples/testunibutton"
check_file "$SRC/EmojiGearL.info"
check_file "$SRC/EmojiGearH.info"
check_file "$SCRIPT_DIR/LICENSE"
check_file "$SRC/EmojiGear.guide"
check_file "$INSTALLER_SRC/Install"
check_file "$INSTALLER_SRC/Install.info"
check_file "$INSTALLER_SRC/EmojiGear.guide.info"
check_file "$INSTALLER_SRC/EmojiGear.readme.info"

# ---------------------------------------------------------------------------
# (Re)create package directory tree
# ---------------------------------------------------------------------------
echo "Creating package directory: $PKG"
rm -rf "$PKG"
mkdir -p "$PKG/bin"
mkdir -p "$PKG/Fonts"
mkdir -p "$PKG/examples_text"

# ---------------------------------------------------------------------------
# Installer script and icon
# ---------------------------------------------------------------------------
echo "Copying installer..."
cp "$INSTALLER_SRC/Install"      "$PKG/Install"
cp "$INSTALLER_SRC/Install.info" "$PKG/Install.info"

# ---------------------------------------------------------------------------
# Shared libraries and gadgets (go into bin/ inside the package;
# the Installer script copies them to their Amiga system locations)
# ---------------------------------------------------------------------------
echo "Copying shared components..."
cp "$BUILD/libutf8rastport/utf8rastport.library"    "$PKG/bin/utf8rastport.library"
cp "$BUILD/UniButtonGadget/unibutton.gadget"        "$PKG/bin/unibutton.gadget"
cp "$BUILD/UniTextEditorGadget/unitexteditor.gadget" "$PKG/bin/unitexteditor.gadget"

# ---------------------------------------------------------------------------
# Application binaries and icons
# ---------------------------------------------------------------------------
echo "Copying EmojiGear binary and icons..."
cp "$BUILD/EmojiGear"     "$PKG/bin/EmojiGear"
cp "$SRC/EmojiGearL.info" "$PKG/bin/EmojiGearL.info"
cp "$SRC/EmojiGearH.info" "$PKG/bin/EmojiGearH.info"

echo "Copying MUImojiGear binary and icon..."
cp "$BUILD/MUImojiGear/MUImojiGear"      "$PKG/bin/MUImojiGear"
cp "$BUILD/MUImojiGear/MUImojiGear.info" "$PKG/bin/MUImojiGear.info"

# ---------------------------------------------------------------------------
# Documentation
# ---------------------------------------------------------------------------
echo "Copying documentation..."
cp "$SCRIPT_DIR/LICENSE"                   "$PKG/LICENSE"
cp "$SRC/EmojiGear.guide"                 "$PKG/EmojiGear.guide"
cp "$INSTALLER_SRC/EmojiGear.guide.info"  "$PKG/EmojiGear.guide.info"
cp "$SCRIPT_DIR/EmojiGear.readme"         "$PKG/EmojiGear.readme"
cp "$INSTALLER_SRC/EmojiGear.readme.info" "$PKG/EmojiGear.readme.info"

# ---------------------------------------------------------------------------
# TrueType fonts
# ---------------------------------------------------------------------------
echo "Copying fonts..."
for ttf in "$FONTS_SRC"/*.ttf; do
    [ -f "$ttf" ] && cp "$ttf" "$PKG/Fonts/"
done

# ---------------------------------------------------------------------------
# Example text files (skip Amiga-FS metadata *.uaem sidecar files)
# ---------------------------------------------------------------------------
echo "Copying example text files..."
for f in "$EXAMPLES_SRC"/*; do
    case "$f" in
        *.uaem) ;;          # skip UAE metadata sidecars
        *)  [ -f "$f" ] && cp "$f" "$PKG/examples_text/" ;;
    esac
done

# ---------------------------------------------------------------------------
# SDK: copy tree into sdk/ inside the package (skip .uaem sidecar files).
# The Amiga Installer copies it to a user-chosen location.
# ---------------------------------------------------------------------------
echo "Copying SDK..."
(cd "$SDK_SRC" && find . -not -name "*.uaem") | while IFS= read -r f; do
    src="$SDK_SRC/$f"
    dst="$PKG/sdk/$f"
    if [ -d "$src" ]; then
        mkdir -p "$dst"
    else
        mkdir -p "$(dirname "$dst")"
        cp "$src" "$dst"
    fi
done

# ---------------------------------------------------------------------------
# SDK example binaries: copy compiled Amiga executables next to their sources
# so the sdk/examples/ directory ships both code and ready-to-run binaries.
# ---------------------------------------------------------------------------
echo "Copying SDK example binaries..."
mkdir -p "$PKG/sdk/examples"
cp "$BUILD/examples/testMUIUniTextEditor" "$PKG/sdk/examples/testMUIUniTextEditor"
cp "$BUILD/examples/TestUtf8DrawText"     "$PKG/sdk/examples/TestUtf8DrawText"
cp "$BUILD/examples/testunibutton"        "$PKG/sdk/examples/testunibutton"

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "Package ready in: $PKG"
echo ""
echo "Contents:"
find "$PKG" -not -type d | sort | sed "s|$PKG/||"
echo ""
echo "To create an LHA archive:"
echo "  cd \"$PKG\" && lha a ../EmojiGear_3.1.lha *"
