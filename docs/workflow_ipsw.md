# Workflow completo con tu IPSW real (iPhone 11 / T8030)

Pipeline de punta a punta usando TODAS las herramientas del repo. Asume el
IPSW documentado para T8030: **iPhone11,8 iOS 14.0 (18A5351d)** o equivalente.

> Pre-requisito: ejecuta el lab en **Linux** (TCG) o, ideal, en un **Mac
> Apple Silicon** (el arnés activa HVF solo y va mucho más rápido).

---

## Fase 0 — Entorno
```bash
./scripts/00_setup_env.sh        # compila el fork QEMU + ipsw + lzfse
cd native && make && cd ..       # compila dt-parse, dt-patch, dt-apply
```

## Fase 1 — Extraer tu IPSW
```bash
./scripts/01_extract_ipsw.sh /ruta/iPhone11,8_14.0_18A5351d_Restore.ipsw out_firmware
# produce: out_firmware/device_tree.bin, kernelcache.raw, (ramdisk, trustcache)
```

## Fase 2 — Inspeccionar el Device Tree REAL
Ahora ya no es una muestra sintética: es el DT de Apple.
```bash
./native/dt-parse out_firmware/device_tree.bin            # arbol completo
./native/dt-parse out_firmware/device_tree.bin /chosen    # config de boot
./native/dt-parse out_firmware/device_tree.bin /arm-io    # perifericos del SoC
```
Guarda este dump: es tu mapa para diagnosticar panics.

## Fase 3 — Preparar el DT de laboratorio (motor de diffs)
Edita `examples/lab.patches` a tu gusto y aplica en lote:
```bash
./native/dt-apply out_firmware/device_tree.bin \
                  out_firmware/device_tree.lab.bin \
                  examples/lab.patches
```
(o el atajo rápido para solo boot-args: `./instrumentation/prepare_lab.sh ...`)

## Fase 4 — Arrancar el lab (arnés con HVF/TCG automático)
```bash
python3 instrumentation/host/run_lab.py \
    --qemu build/QEMUAppleSilicon/build/qemu-system-aarch64 \
    --kernel out_firmware/kernelcache.raw \
    --dtb   out_firmware/device_tree.lab.bin \
    --ramdisk out_firmware/ramdisk.dmg \
    --extra -- -M t8030,trustcache-filename=out_firmware/tc.bin \
               -append "" 2>&1 | tee qemu.log
```
El arnés espera a que `frida-server` (puerto 27042) levante en el guest.

> El `-M t8030` del fork requiere propiedades propias (kernel-filename,
> dtb-filename, ramdisk-filename, trustcache-filename). Si tu build las
> exige como parte de `-M`, pásalas con `--extra -- ...`. Consulta el wiki
> del fork para los nombres exactos de tu versión.

## Fase 5 — Diagnóstico si NO arranca (lo normal al principio)
Si ves un kernel panic en `qemu.log`:
```bash
python3 scripts/panic_tracker.py qemu.log --csv msr_table.csv
```
Esto te dice qué registro MSR o periférico MMIO falló. Si el panic menciona
un nodo del Device Tree, vuelve a Fase 3 y añade/corrige ese nodo en
`lab.patches` (ese es el ciclo de investigación de Cylance: panic → parchear
DT → reintentar).

## Fase 6 — Instrumentar (cuando llegue a SpringBoard)
```bash
# reconocimiento general
python3 instrumentation/host/run_frida.py -H 127.0.0.1:27042 \
    -n SpringBoard -s instrumentation/frida_scripts/ios_recon.js -o recon.jsonl

# tracer de syscalls/filesystem
python3 instrumentation/host/run_frida.py -H 127.0.0.1:27042 \
    -n SpringBoard -s instrumentation/frida_scripts/syscall_tracer.js -o trace.jsonl
```

## Fase 7 — Analizar y correlacionar (cierra el bucle)
```bash
python3 instrumentation/host/analyze_trace.py trace.jsonl --qemu-log qemu.log
```
Resume la telemetría y, si hubo panic, muestra las últimas actividades
observadas antes del fallo, junto a la pista de `panic_tracker.py`.

---

## El ciclo de investigación, en una frase
```
extraer → dt-parse → dt-apply (lab) → run_lab (HVF/TCG)
   → ¿panic? → panic_tracker → corregir lab.patches → reintentar
   → ¿SpringBoard? → Frida (recon + syscalls) → analyze_trace
```

## Límites honestos
- Target: iPhone 11 / iOS 14. NO iPhone 17 / A19 (ver README).
- `frida-server` debe estar dentro del rootfs emulado.
- Boot-args permisivos solo aplican a kernels de development en entorno
  emulado controlado; no eluden protecciones de producción.
