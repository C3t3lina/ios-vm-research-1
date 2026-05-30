#!/usr/bin/env python3
"""
scripts/panic_tracker.py
Hito Semanas 3-4: parser de logs de QEMU para mapear MSRs / MMIO no
implementados durante el boot de XNU. Ejecuta QEMU con:

    qemu-system-aarch64 ... -d guest_errors,unimp 2> qemu.log

y luego:  ./panic_tracker.py qemu.log

Genera la tabla de prioridades de registros a implementar y, opcionalmente,
un CSV (--csv salida.csv) para seguimiento entre semanas.
"""
import re
import sys
import argparse
from collections import Counter

# Los forks (ChefKiss / Aleph / TrungNguyen) emiten formatos algo distintos.
# Cubrimos las variantes mas comunes. NOTA: en regex hay que escapar \d.
MSR_PATTERNS = [
    # "invalid MSR read: reg=0x..., op0=.., op1=.., crn=.., crm=.., op2=.."
    re.compile(r"MSR\s+(?:read|write).*?reg=(0x[0-9a-fA-F]+|\d+)", re.IGNORECASE),
    # forma alterna: "unimplemented sysreg ... s3_4_c15_c2_1"
    re.compile(r"\b(s\d_\d+_c\d+_c\d+_\d+)\b", re.IGNORECASE),
]
MMIO_PATTERNS = [
    re.compile(r"(?:read|write)\s+to\s+unimplemented.*?(?:address|addr)\s+(0x[0-9a-fA-F]+)",
               re.IGNORECASE),
    re.compile(r"unassigned\s+(?:mem|device)\s+(?:read|write).*?(0x[0-9a-fA-F]+)",
               re.IGNORECASE),
]
PANIC_HINTS = ("panic", "kernel debugger", "double panic", "halting")


def first_match(line, patterns):
    for pat in patterns:
        m = pat.search(line)
        if m:
            return m.group(1).lower()
    return None


def parse_qemu_log(log_path):
    msrs, mmio = [], []
    panic = False
    panic_lines = []

    print(f"[*] Analizando log de QEMU: {log_path}")
    with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
        for n, line in enumerate(f, 1):
            low = line.lower()
            if any(h in low for h in PANIC_HINTS):
                panic = True
                panic_lines.append((n, line.strip()))

            r = first_match(line, MSR_PATTERNS)
            if r:
                msrs.append(r)
            a = first_match(line, MMIO_PATTERNS)
            if a:
                mmio.append(a)

    return msrs, mmio, panic, panic_lines


def report(msrs, mmio, panic, panic_lines, csv_path=None):
    print("\n=== RESULTADOS DEL DIAGNOSTICO DEL BOOT ===")
    if panic:
        print(f"[!] ALERTA: Kernel Panic detectado ({len(panic_lines)} lineas).")
        for n, txt in panic_lines[:3]:
            print(f"      L{n}: {txt[:100]}")
    else:
        print("[?] Sin panic explicito (posible cuelgue/espera silenciosa).")

    msr_c, mmio_c = Counter(msrs), Counter(mmio)

    print(f"\n[+] Registros MSR/sysreg no implementados (unicos: {len(msr_c)}):")
    for reg, c in msr_c.most_common():
        print(f"    - {reg:<22} accedido {c}x")

    print(f"\n[+] Direcciones MMIO de perifericos faltantes (unicas: {len(mmio_c)}):")
    for addr, c in mmio_c.most_common():
        print(f"    - {addr:<14} accedido {c}x")

    print("\n[>] PRIORIDAD: implementa primero lo de mayor frecuencia "
          "(suele bloquear el avance del boot).")

    if csv_path:
        with open(csv_path, "w", encoding="utf-8") as out:
            out.write("tipo,identificador,frecuencia\n")
            for reg, c in msr_c.most_common():
                out.write(f"msr,{reg},{c}\n")
            for addr, c in mmio_c.most_common():
                out.write(f"mmio,{addr},{c}\n")
        print(f"\n[+] CSV escrito en {csv_path}")


def main():
    ap = argparse.ArgumentParser(description="Mapea MSR/MMIO no implementados de un log de QEMU.")
    ap.add_argument("log", help="ruta al log (stderr de QEMU con -d guest_errors,unimp)")
    ap.add_argument("--csv", help="exporta resultados a CSV para seguimiento semanal")
    args = ap.parse_args()

    try:
        msrs, mmio, panic, plines = parse_qemu_log(args.log)
    except FileNotFoundError:
        print(f"[error] no existe el log: {args.log}", file=sys.stderr)
        sys.exit(1)
    report(msrs, mmio, panic, plines, args.csv)


if __name__ == "__main__":
    main()
