#!/usr/bin/env bash
# ===========================================================================
# Fase 1 - Hito Semana 1: bootstrap del entorno de investigacion
# Compila ChefKissInc/QEMUAppleSilicon (fork mantenido de qemu-t8030)
# y las herramientas necesarias para extraer firmware.
#
# Soporta Linux (apt) y macOS (brew). Uso academico / fair use.
# ===========================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
mkdir -p "$BUILD"

log() { printf '\033[1;32m[setup]\033[0m %s\n' "$*"; }
err() { printf '\033[1;31m[error]\033[0m %s\n' "$*" >&2; }

OS="$(uname -s)"

install_deps() {
  if [[ "$OS" == "Linux" ]]; then
    log "Instalando dependencias (apt)..."
    sudo apt-get update
    sudo apt-get install -y git build-essential cmake ninja-build pkg-config \
      libglib2.0-dev libfdt-dev libpixman-1-dev zlib1g-dev libtasn1-dev \
      python3 python3-pip gdb curl jq
  elif [[ "$OS" == "Darwin" ]]; then
    log "Instalando dependencias (brew)..."
    brew install git cmake ninja pkg-config glib pixman libtasn1 meson jq \
      gnutls libgcrypt coreutils python3
  else
    err "SO no soportado: $OS"; exit 1
  fi
  python3 -m pip install --user pyasn1 || true
}

build_lzfse() {
  if command -v lzfse >/dev/null 2>&1; then log "lzfse ya instalado"; return; fi
  log "Compilando lzfse..."
  cd "$BUILD"
  [[ -d lzfse ]] || git clone --depth 1 https://github.com/lzfse/lzfse
  cd lzfse && mkdir -p build && cd build
  cmake .. -G Ninja && ninja && sudo ninja install
}

fetch_ipsw_tool() {
  # blacktop/ipsw: extractor de IPSW/img4 multiplataforma (mas fiable que los scripts python)
  if command -v ipsw >/dev/null 2>&1; then log "ipsw ya instalado"; return; fi
  log "Instalando blacktop/ipsw..."
  if [[ "$OS" == "Darwin" ]]; then
    brew install blacktop/tap/ipsw
  else
    cd "$BUILD"
    LATEST=$(curl -s https://api.github.com/repos/blacktop/ipsw/releases/latest | jq -r '.tag_name')
    VER="${LATEST#v}"
    curl -L -o ipsw.tar.gz \
      "https://github.com/blacktop/ipsw/releases/download/${LATEST}/ipsw_${VER}_linux_x86_64.tar.gz"
    tar xzf ipsw.tar.gz ipsw
    sudo install -m 0755 ipsw /usr/local/bin/ipsw
  fi
}

build_qemu() {
  log "Clonando QEMUAppleSilicon (fork T8030 mantenido)..."
  cd "$BUILD"
  [[ -d QEMUAppleSilicon ]] || \
    git clone --depth 1 https://github.com/ChefKissInc/QEMUAppleSilicon
  cd QEMUAppleSilicon
  mkdir -p build && cd build
  log "Configurando QEMU (solo aarch64-softmmu, TCG)..."
  ../configure --target-list=aarch64-softmmu \
               --enable-lzfse --disable-werror --enable-slirp
  log "Compilando QEMU (esto tarda)..."
  make -j"$(getconf _NPROCESSORS_ONLN)"
  log "QEMU compilado: $PWD/qemu-system-aarch64"
}

main() {
  log "Host: $OS"
  install_deps
  build_lzfse
  fetch_ipsw_tool
  build_qemu
  log "Entorno listo. Siguiente: scripts/01_extract_ipsw.sh <ruta_al.ipsw>"
}
main "$@"
