#!/usr/bin/env bash
# Build native Fedora RPM + no-root user tarball for Gorgon 2D Editor.
# Usage:
#   ./Tools/Editor/Installer/linux/build-fedora-rpm.sh --build --version 4.2.0
#   ./Tools/Editor/Installer/linux/build-fedora-rpm.sh --bin-dir build/FedoraRelease/Bin --output-dir dist/fedora
set -euo pipefail

VERSION="4.2.0"
BIN_DIR=""
OUTPUT_DIR=""
DO_BUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)    VERSION="$2"; shift 2 ;;
        --bin-dir)    BIN_DIR="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --build)      DO_BUILD=1; shift ;;
        -h|--help)
            echo "Usage: $0 [--build] [--version VER] [--bin-dir DIR] [--output-dir DIR]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALLER_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"

if [[ -z "$BIN_DIR" ]]; then
    BIN_DIR="${REPO_ROOT}/build/FedoraRelease/Bin"
fi
if [[ -z "$OUTPUT_DIR" ]]; then
    OUTPUT_DIR="${REPO_ROOT}/dist/fedora"
fi

if [[ $DO_BUILD -eq 1 ]]; then
    echo "==> Configuring & building GorgonEditor + GorgonPlayer (Release, Vulkan)..."
    cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build/FedoraRelease" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DGraphicsLibrary=Vulkan \
        -DBUILD_EDITOR=ON \
        -DDOCUMENTATION=OFF
    cmake --build "${REPO_ROOT}/build/FedoraRelease" --target GorgonEditor GorgonPlayer --parallel
fi

for bin in GorgonEditor GorgonPlayer; do
    if [[ ! -x "${BIN_DIR}/${bin}" ]]; then
        echo "ERROR: Missing executable: ${BIN_DIR}/${bin} (pass --build or --bin-dir)" >&2
        exit 1
    fi
done

mkdir -p "${OUTPUT_DIR}"
STAGE_DIR="$(mktemp -d)"
trap 'rm -rf "${STAGE_DIR}"' EXIT

# Stage payload
cp -p "${BIN_DIR}/GorgonEditor" "${STAGE_DIR}/GorgonEditor"
cp -p "${BIN_DIR}/GorgonPlayer" "${STAGE_DIR}/GorgonPlayer"
strip --strip-unneeded "${STAGE_DIR}/GorgonEditor" "${STAGE_DIR}/GorgonPlayer" 2>/dev/null || true
cp -a "${REPO_ROOT}/Examples/EditorDemo/Scene" "${STAGE_DIR}/DemoScene"
cp -p "${REPO_ROOT}/Tools/Editor/README.md" "${STAGE_DIR}/README.md"
cp -p "${INSTALLER_DIR}/gorgon-editor.png" "${STAGE_DIR}/gorgon-editor.png"
cp -p "${SCRIPT_DIR}/gorgon-editor.desktop" "${STAGE_DIR}/gorgon-editor.desktop"
cp -p "${SCRIPT_DIR}/gorgon-scene.xml" "${STAGE_DIR}/gorgon-scene.xml"

# RPM version cannot contain hyphens; normalize e.g. 4.2.0-beta1 -> 4.2.0~beta1
RPM_VERSION="${VERSION//-/'~'}"

# 1. Build native Fedora RPM
RPM_TOPDIR="${STAGE_DIR}/rpmbuild"
mkdir -p "${RPM_TOPDIR}"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

echo "==> Building Fedora RPM (version ${RPM_VERSION})..."
rpmbuild -bb "${SCRIPT_DIR}/gorgon-editor.spec" \
    --define "_topdir ${RPM_TOPDIR}" \
    --define "_version ${RPM_VERSION}" \
    --define "_stagedir ${STAGE_DIR}"

RPM_FILE="$(find "${RPM_TOPDIR}/RPMS" -name '*.rpm' | head -n 1)"
if [[ -z "$RPM_FILE" ]]; then
    echo "ERROR: rpmbuild did not produce an .rpm file" >&2
    exit 1
fi
cp -p "${RPM_FILE}" "${OUTPUT_DIR}/"
RPM_BASENAME="$(basename "${RPM_FILE}")"
echo "==> RPM ready: ${OUTPUT_DIR}/${RPM_BASENAME}"

# 2. Build portable no-root user tarball (installs into ~/.local)
PORTABLE_NAME="GorgonEditor-${VERSION}-fedora-$(uname -m)"
PORTABLE_DIR="${STAGE_DIR}/${PORTABLE_NAME}"
mkdir -p "${PORTABLE_DIR}"
cp -p "${STAGE_DIR}/GorgonEditor" "${STAGE_DIR}/GorgonPlayer" "${STAGE_DIR}/README.md" \
      "${STAGE_DIR}/gorgon-editor.png" "${STAGE_DIR}/gorgon-scene.xml" "${PORTABLE_DIR}/"
cp -a "${STAGE_DIR}/DemoScene" "${PORTABLE_DIR}/DemoScene"

cat > "${PORTABLE_DIR}/install-user.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/gorgon-editor"
BIN_DIR="$HOME/.local/bin"
DESK_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICON_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"
MIME_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/mime/packages"

mkdir -p "$APP_DIR" "$BIN_DIR" "$DESK_DIR" "$ICON_DIR" "$MIME_DIR"
cp -p "$SRC/GorgonEditor" "$SRC/GorgonPlayer" "$SRC/README.md" "$APP_DIR/"
chmod +x "$APP_DIR/GorgonEditor" "$APP_DIR/GorgonPlayer"
rm -rf "$APP_DIR/DemoScene" && cp -a "$SRC/DemoScene" "$APP_DIR/DemoScene"
ln -sf "$APP_DIR/GorgonEditor" "$BIN_DIR/gorgon-editor"
ln -sf "$APP_DIR/GorgonPlayer" "$BIN_DIR/gorgon-player"
cp -p "$SRC/gorgon-editor.png" "$ICON_DIR/gorgon-editor.png"
cp -p "$SRC/gorgon-scene.xml" "$MIME_DIR/gorgon-scene.xml"

cat > "$DESK_DIR/gorgon-editor.desktop" <<DESK
[Desktop Entry]
Type=Application
Version=1.0
Name=Gorgon 2D Editor
Name[tr]=Gorgon 2D Editör
GenericName=2D Scene Editor
Comment=2D Scene Editor built with Gorgon
Exec=$APP_DIR/GorgonEditor %f
Icon=gorgon-editor
Terminal=false
Categories=Development;Graphics;2DGraphics;IDE;
MimeType=application/x-gorgon-scene;
StartupNotify=true
Actions=OpenDemo;

[Desktop Action OpenDemo]
Name=Open Demo Scene
Name[tr]=Demo Sahnesini Aç
Exec=$APP_DIR/GorgonEditor $APP_DIR/DemoScene/DemoScene.gscene.json
DESK

update-desktop-database "$DESK_DIR" 2>/dev/null || true
update-mime-database "${XDG_DATA_HOME:-$HOME/.local/share}/mime" 2>/dev/null || true
echo "Installed Gorgon 2D Editor to $APP_DIR"
echo "Launch from your application menu or run: $BIN_DIR/gorgon-editor"
EOF

cat > "${PORTABLE_DIR}/uninstall-user.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
rm -rf "$DATA_HOME/gorgon-editor"
rm -f "$HOME/.local/bin/gorgon-editor" "$HOME/.local/bin/gorgon-player"
rm -f "$DATA_HOME/applications/gorgon-editor.desktop"
rm -f "$DATA_HOME/icons/hicolor/256x256/apps/gorgon-editor.png"
rm -f "$DATA_HOME/mime/packages/gorgon-scene.xml"
update-desktop-database "$DATA_HOME/applications" 2>/dev/null || true
update-mime-database "$DATA_HOME/mime" 2>/dev/null || true
echo "Uninstalled Gorgon 2D Editor."
EOF

chmod +x "${PORTABLE_DIR}/install-user.sh" "${PORTABLE_DIR}/uninstall-user.sh"
TAR_FILE="${OUTPUT_DIR}/${PORTABLE_NAME}.tar.gz"
tar -czf "${TAR_FILE}" -C "${STAGE_DIR}" "${PORTABLE_NAME}"
echo "==> Portable tarball ready: ${TAR_FILE}"

(cd "${OUTPUT_DIR}" && sha256sum "${RPM_BASENAME}" "${PORTABLE_NAME}.tar.gz" > SHA256SUMS-fedora.txt)
cat "${OUTPUT_DIR}/SHA256SUMS-fedora.txt"
