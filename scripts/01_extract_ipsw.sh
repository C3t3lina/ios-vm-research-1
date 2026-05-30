#!/usr/bin/env bash
# ===========================================================================
# Fase 1 - Hito Semana 2: extraccion de componentes de un IPSW (T8030)
#
# IMPORTANTE (legal): TU debes proporcionar un IPSW obtenido legalmente.
# No se descarga firmware de Apple aqui. Target documentado para T8030:
#   iPhone11,8 / iOS 14.0 (build 18A5351d)  -> DeviceTree.n104ap
#
# Extrae: kernelcache, DeviceTree, ramdisk y trustcache; los descomprime.
# ===========================================================================
set -euo pipefail

IPSW="${1:-}"
OUT="${2:-out_firmware}"

if [[ -z "$IPSW" || ! -f "$IPSW" ]]; then
  echo "Uso: $0 <ruta_al_archivo.ipsw> [dir_salida]"
  echo "  El IPSW debes obtenerlo TU legalmente desde tu cuenta/dispositivo."
  exit 1
fi

command -v ipsw  >/dev/null || { echo "Falta 'ipsw' (ejecuta 00_setup_env.sh)"; exit 1; }
command -v lzfse >/dev/null || { echo "Falta 'lzfse' (ejecuta 00_setup_env.sh)"; exit 1; }

mkdir -p "$OUT"; cd "$OUT"

echo "[*] Descomprimiendo IPSW..."
unzip -o "$IPSW" >/dev/null

# --- Device Tree -----------------------------------------------------------
DT_IM4P="$(find . -name 'DeviceTree.*.im4p' | head -n1)"
[[ -n "$DT_IM4P" ]] || { echo "No se encontro DeviceTree.*.im4p"; exit 1; }
echo "[*] DeviceTree: $DT_IM4P"
ipsw img4 extract "$DT_IM4P" || ipsw img4 dec "$DT_IM4P" || true
# El payload resultante suele venir ya crudo (DT no usa lzfse). Normalizamos nombre:
DT_RAW="$(find . -name 'DeviceTree.*.im4p.payload' -o -name 'DeviceTree.*.dtb' 2>/dev/null | head -n1)"
[[ -n "${DT_RAW:-}" ]] && cp "$DT_RAW" device_tree.bin && echo "[+] device_tree.bin listo"

# --- Kernelcache -----------------------------------------------------------
KC="$(find . -name 'kernelcache.*' ! -name '*.out' | head -n1)"
if [[ -n "$KC" ]]; then
  echo "[*] Kernelcache: $KC"
  ipsw img4 extract "$KC" || true
  PAY="$(find . -name "$(basename "$KC").payload" | head -n1)"
  if [[ -n "${PAY:-}" ]]; then
    lzfse -decode -i "$PAY" -o kernelcache.raw 2>/dev/null \
      && echo "[+] kernelcache.raw listo" \
      || cp "$PAY" kernelcache.raw
  fi
fi

echo
echo "[OK] Artefactos en: $(pwd)"
echo "     - device_tree.bin   (entrada para tools/dt-parse)"
echo "     - kernelcache.raw"
echo "Siguiente: compila el parser ->  cd native && make && ./dt-parse ../$OUT/device_tree.bin"
