# iOS-VM-Research — Fase 1: Viabilidad Académica

Proyecto de investigación open source sobre **emulación de XNU/iOS en QEMU
(target T8030 / iPhone 11)**, con foco en el **análisis y mutación del Apple
Device Tree** como herramienta de diagnóstico de *boot panics*.

> No es un clon de Corellium. Es trabajo académico de seguridad (fair use)
> construido **sobre proyectos open source existentes**, no desde cero.

## Estado del arte sobre el que nos apoyamos
- **ChefKissInc/QEMUAppleSilicon** — iPhone 11 arranca hasta SpringBoard (fork mantenido de qemu-t8030).
- **TrungNguyen1909/qemu-t8030** — base original de emulación T8030.
- **alephsecurity/xnu-qemu-arm64** — primeros 12 registros MSR de Apple + iOS a shell.
- **cylance/macos-arm64-emulation (dtetool)** — inspiración para el parcheo del Device Tree.

## Por qué el Device Tree
En la investigación de Cylance, el Device Tree fue responsable de **más de la
mitad de los panics de boot**. Es el cuello de botella real y un punto de
entrada de bajo riesgo y alto valor para una primera contribución.

## Estructura
```
scripts/00_setup_env.sh     Compila QEMU (fork) + ipsw + lzfse (Linux/macOS)
scripts/01_extract_ipsw.sh  Extrae kernelcache/DeviceTree de TU IPSW legal
native/                     Parser+mutador de Apple Device Tree en C (núcleo)
  include/apple_dt.h        API + descripción del formato binario
  src/apple_dt.c            Parser, búsqueda, dump, mutador, serializador
  src/main.c                CLI: dt-parse <bin> [/path]
  tools/dt_patch.c          CLI: dt-patch (inyecta boot-args / props)
  tools/dt_apply.c          CLI: dt-apply (motor de diffs declarativo)
  tools/mkdt_sample.c       Genera un DT de prueba para validar sin IPSW
  tools/test_dt.c           Suite de tests (round-trip, mutación, upsert, delete)
examples/lab.patches        Parches declarativos de ejemplo para dt-apply
scripts/panic_tracker.py    Mapea MSR/MMIO no implementados desde log QEMU
instrumentation/            Análisis dinámico con Frida sobre iOS emulado
  prepare_lab.sh            Inyecta boot-args de lab en el DT (usa dt-patch)
  frida_scripts/ios_recon.js     Agente de reconocimiento (módulos/ficheros/cripto)
  frida_scripts/objc_trace.js    Hooks de Objective-C (apps / Simulador o T8030)
  frida_scripts/syscall_tracer.js  Tracer de syscalls/filesystem/red (XNU)
  host/run_lab.py           Arnés: lanza QEMU (HVF/TCG) + espera frida-server
  host/run_frida.py         Arnés que inyecta el agente y recoge eventos
  host/analyze_trace.py     Resume telemetría y correlaciona con panics
docs/lab_setup.md           Pipeline T8030 + Frida
docs/workflow_ipsw.md       Workflow completo de punta a punta con IPSW real
docs/                       Notas de investigación
```

## Empezar hoy (sin IPSW todavía)
```bash
cd native
make test          # prueba de humo: parser + dump
make test-mutator  # suite completa con AddressSanitizer + UBSan (17 checks)
make test-patch    # e2e: dt-patch crea boot-args en /chosen y re-verifica
```

### Inyectar boot-args en un Device Tree
```bash
./dt-patch in_dt.bin out_dt.bin --boot-args "-v debug=0x14e keepsyms=1"
./dt-patch in_dt.bin out_dt.bin --set-prop-str /chosen mi-prop "valor"
```
`dt-patch` usa *upsert*: crea la propiedad si no existe (caso típico de
`boot-args`, que iBoot inyecta en arranque y no siempre está en el DT estático).

## Con un IPSW real (obtenido legalmente por ti)
```bash
./scripts/00_setup_env.sh
./scripts/01_extract_ipsw.sh /ruta/a/iPhone11,8_14.0_18A5351d_Restore.ipsw
cd native && make
./dt-parse ../out_firmware/device_tree.bin /chosen
./dt-parse ../out_firmware/device_tree.bin /arm-io   # perifericos del SoC
```

## Lab dinámico (T8030 + Frida)
Instrumentación de iOS **emulado** sin hardware ni Corellium. El toolkit de
Device Tree y Frida se complementan: `dt-patch` inyecta los boot-args que
Frida necesita para enganchar el kernel emulado.
```bash
./instrumentation/prepare_lab.sh out_firmware/device_tree.bin dt.lab.bin
# arranca QEMU con -dtb dt.lab.bin (ver docs/lab_setup.md), luego:
python3 instrumentation/host/run_frida.py -H 127.0.0.1:27042 \
    -n SpringBoard -s instrumentation/frida_scripts/ios_recon.js
```
> Soportado: iPhone 11 / iOS 14 (límite del estado del arte open source).

## Hoja de ruta (12 semanas)
- **Mes 1** — Reproducir builds existentes; capturar logs de boot; mapear
  registros MSR no implementados; documentar el formato del DT.
- **Mes 2** — Parser de DT propio (✅ hecho): dump, búsqueda y **mutación**
  (aplicar diffs y re-serializar, estilo dtetool).
- **Mes 3** — Correlacionar panics ↔ nodos faltantes; probar mutaciones contra
  QEMU; medir progreso de boot; redactar writeup y abrir PRs upstream.

## Aviso legal
- **No** se distribuye firmware de Apple. El IPSW lo aportas tú, obtenido
  legalmente desde tu cuenta/dispositivo.
- Uso estrictamente de investigación e interoperabilidad/seguridad.
- iOS/XNU, Apple Silicon y Device Tree son propiedad de Apple Inc.
```
