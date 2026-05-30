#!/usr/bin/env python3
"""
host/analyze_trace.py - Analiza el JSONL producido por run_frida.py y, si se
le pasa un log de QEMU, correlaciona la ultima actividad observada con un
panic del kernel (cierra el bucle Frida <-> panic_tracker).

Uso:
  ./analyze_trace.py recon.jsonl
  ./analyze_trace.py recon.jsonl --qemu-log qemu.log
"""
import argparse
import json
import sys
from collections import Counter, defaultdict


def load_jsonl(path):
    events = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return events


def summarize(events):
    by_type = Counter(e.get("type", "?") for e in events)
    print("=== RESUMEN DE TELEMETRIA ===")
    for t, c in by_type.most_common():
        print(f"  {t:12} {c}")

    # filesystem por ruta
    fs_paths = Counter()
    cats = defaultdict(Counter)
    for e in events:
        if e.get("type") == "syscall":
            cats[e.get("cat", "?")][e.get("name", "?")] += 1
            if e.get("cat") == "fs" and e.get("detail"):
                fs_paths[e["detail"]] += 1
        elif e.get("type") == "recon" and e.get("tag") == "file":
            # del ios_recon.js: 'open("...")'
            fs_paths[e.get("msg", "")] += 1

    if cats:
        print("\n=== SYSCALLS POR CATEGORIA ===")
        for cat, names in cats.items():
            print(f"  [{cat}]")
            for n, c in names.most_common():
                print(f"      {n:14} {c}x")

    if fs_paths:
        print("\n=== TOP RUTAS / FICHEROS ===")
        for p, c in fs_paths.most_common(15):
            print(f"  {c:4}x {p}")

    return events


def correlate_panic(events, qemu_log):
    """Si el log tiene un panic, muestra las ultimas N actividades antes de el."""
    panic = False
    with open(qemu_log, encoding="utf-8", errors="ignore") as f:
        for line in f:
            low = line.lower()
            if "panic" in low or "kernel debugger" in low:
                panic = True
                break
    print("\n=== CORRELACION CON QEMU ===")
    if not panic:
        print("  [?] No se detecto panic en el log de QEMU.")
        return
    print("  [!] Panic detectado. Ultimas 10 actividades observadas por Frida")
    print("      antes del cierre (candidatas a disparar el fallo):")
    for e in events[-10:]:
        if e.get("type") == "syscall":
            print(f"      - ({e.get('cat')}) {e.get('name')}: {e.get('detail')}")
        elif e.get("type") == "recon":
            print(f"      - ({e.get('tag')}) {e.get('msg')}")
    print("\n  Sugerencia: corre tambien scripts/panic_tracker.py sobre el log")
    print("  para ver que registro/periferico fallo a nivel de hardware.")


def main():
    ap = argparse.ArgumentParser(description="Analiza telemetria Frida y correlaciona panics.")
    ap.add_argument("jsonl", help="fichero JSONL de run_frida.py")
    ap.add_argument("--qemu-log", help="log de QEMU para correlacionar panic")
    args = ap.parse_args()

    try:
        events = load_jsonl(args.jsonl)
    except FileNotFoundError:
        print(f"[!] no existe: {args.jsonl}", file=sys.stderr)
        sys.exit(1)

    print(f"[*] {len(events)} eventos cargados de {args.jsonl}\n")
    summarize(events)
    if args.qemu_log:
        correlate_panic(events, args.qemu_log)


if __name__ == "__main__":
    main()
