#!/usr/bin/env bash
# ===========================================================================
# prepare_lab.sh - Conecta el toolkit de Device Tree con el lab de Frida.
#
# Inyecta en el Device Tree los boot-args necesarios para que el iOS emulado
# arranque en modo permisivo para instrumentacion dinamica (lab/fair-use):
#   -v               verbose (logs por consola serie)
#   debug=0x14e      mascaras de debug del kernel
#   amfi_get_out_of_my_way=1   relaja AMFI (solo entorno de investigacion)
#   cs_enforcement_disable=1   relaja code signing (solo lab)
#
# AVISO: estos flags solo tienen efecto en un kernel de development/research
# y en un entorno emulado controlado. NO eluden protecciones en devices
# de produccion. Uso estrictamente academico.
# ===========================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DT_PATCH="$ROOT/native/dt-patch"
IN="${1:-}"
OUT="${2:-device_tree.patched.bin}"
ARGS="${3:--v debug=0x14e amfi_get_out_of_my_way=1 cs_enforcement_disable=1}"

if [[ ! -x "$DT_PATCH" ]]; then
  echo "[*] dt-patch no compilado, compilando..."
  ( cd "$ROOT/native" && make dt-patch )
fi

if [[ -z "$IN" || ! -f "$IN" ]]; then
  echo "Uso: $0 <device_tree.bin> [salida.bin] [\"boot-args\"]"
  echo "  device_tree.bin extraido de TU IPSW con scripts/01_extract_ipsw.sh"
  exit 1
fi

echo "[*] Inyectando boot-args de laboratorio en el Device Tree..."
"$DT_PATCH" "$IN" "$OUT" --boot-args "$ARGS"

echo
echo "[OK] Device Tree de lab listo: $OUT"
echo "     Arranca QEMU con  -dtb $OUT  (ver docs/lab_setup.md)"
echo "     Luego:  instrumentation/host/run_frida.py -H 127.0.0.1:27042 ..."
