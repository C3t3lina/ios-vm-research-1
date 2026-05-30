# Lab de análisis dinámico: T8030 (QEMU) + Frida

Pipeline completo para instrumentar iOS **emulado** sin hardware ni Corellium.

```
 dt-patch (boot-args) ──► QEMU T8030 ──► iOS 14 a SpringBoard
                                              │  (frida-server dentro)
                                              ▼
                              run_frida.py (host) ──► hooks / traces
```

## Realidad y límites (honestidad técnica)
- Target soportado por la emulación open source: **iPhone 11 (T8030), iOS 14.0
  beta 5**. NO el iPhone 17 / A19 Pro (ver README principal).
- `frida-server` debe estar **dentro** del rootfs del iOS emulado. En un
  device real de lab, se instala vía jailbreak. En el emulado, se añade al
  ramdisk/rootfs durante la preparación de la imagen.
- Los boot-args permisivos solo surten efecto en kernels de development y en
  el entorno emulado controlado. No eluden nada en producción.

## Paso 1 — Preparar el Device Tree
```bash
# extraido de TU IPSW legal (scripts/01_extract_ipsw.sh)
./instrumentation/prepare_lab.sh out_firmware/device_tree.bin dt.lab.bin
```

## Paso 2 — Arrancar QEMU con red para Frida
El puerto por defecto de frida-server es 27042. Lo redirigimos a localhost:
```bash
qemu-system-aarch64 -M t8030 \
  -kernel kernelcache.raw \
  -dtb dt.lab.bin \
  -initrd ramdisk.dmg \
  -serial mon:stdio -m 4G -smp 6 \
  -netdev user,id=net0,hostfwd=tcp::27042-:27042 \
  -device virtio-net,netdev=net0
```

## Paso 3 — Instrumentar desde el host
```bash
cd instrumentation
pip install frida-tools
python3 host/run_frida.py -H 127.0.0.1:27042 -n SpringBoard \
    -s frida_scripts/ios_recon.js -o recon.jsonl
```

## Paso 4 — Analizar la salida
`recon.jsonl` contiene un evento por línea (módulos, accesos a ficheros,
NSLog, llamadas cripto). Procesable con `jq` o pandas para tu writeup.

## Roadmap del módulo de instrumentación
- [x] Agente de reconocimiento (solo lectura): módulos, ficheros, cripto.
- [ ] Tracer de syscalls del kernel XNU.
- [ ] Dump de clases ObjC y métodos en caliente.
- [ ] Correlación frida ↔ panics del kernel (cierra el bucle con panic_tracker).
