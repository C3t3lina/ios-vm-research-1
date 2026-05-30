#!/usr/bin/env python3
"""
host/run_frida.py - Arnes de orquestacion para instrumentar iOS con Frida.

Conecta con un frida-server corriendo dentro del iOS emulado (QEMU T8030)
o un device en lab, inyecta un agente JS y recoge los eventos en consola
y en un JSONL para analisis posterior.

Requisitos en el HOST:   pip install frida-tools
Requisitos en el TARGET: frida-server arrancado (ver docs/lab_setup.md)

Ejemplos:
  # spawnear una app por bundle id sobre USB/red
  ./run_frida.py -f com.apple.Preferences -s ../frida_scripts/ios_recon.js

  # adjuntarse a un proceso ya corriendo
  ./run_frida.py -n SpringBoard -s ../frida_scripts/ios_recon.js

  # target remoto (QEMU con frida-server escuchando en host:port)
  ./run_frida.py -H 127.0.0.1:27042 -n SpringBoard -s ../frida_scripts/ios_recon.js
"""
import argparse
import json
import sys
import time
from pathlib import Path

try:
    import frida
except ImportError:
    print("[!] Falta el modulo 'frida'. Instala con: pip install frida-tools",
          file=sys.stderr)
    sys.exit(1)


def make_message_handler(jsonl_path):
    out = open(jsonl_path, "a", encoding="utf-8") if jsonl_path else None

    def on_message(message, data):
        if message.get("type") == "send":
            payload = message.get("payload", {})
            tag = payload.get("tag", "?")
            ts = payload.get("ts", "")
            msg = payload.get("msg", "")
            print(f"  [{ts}] ({tag}) {msg}")
            extra = payload.get("extra")
            if extra and tag == "modules":
                print(f"      -> {len(extra)} modulos (mostrando primeros): "
                      f"{', '.join(m['name'] for m in extra[:8])} ...")
            if out:
                out.write(json.dumps(payload) + "\n")
                out.flush()
        elif message.get("type") == "error":
            print(f"  [!] ERROR en el agente: {message.get('description')}",
                  file=sys.stderr)
            if message.get("stack"):
                print(message["stack"], file=sys.stderr)

    return on_message


def get_device(host):
    if host:
        print(f"[*] Conectando a target remoto {host} ...")
        return frida.get_device_manager().add_remote_device(host)
    print("[*] Usando dispositivo USB (-U) ...")
    return frida.get_usb_device(timeout=10)


def main():
    ap = argparse.ArgumentParser(description="Arnes Frida para iOS (lab T8030/real).")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("-f", "--spawn", metavar="BUNDLE_ID", help="lanza la app por bundle id")
    g.add_argument("-n", "--attach", metavar="PROC", help="adjunta a un proceso por nombre")
    ap.add_argument("-s", "--script", required=True, help="ruta al agente .js")
    ap.add_argument("-H", "--host", help="target remoto host:port (frida-server)")
    ap.add_argument("-o", "--out", default="recon.jsonl", help="fichero JSONL de salida")
    ap.add_argument("--duration", type=int, default=0,
                    help="segundos a observar antes de salir (0 = hasta Ctrl-C)")
    args = ap.parse_args()

    script_src = Path(args.script).read_text(encoding="utf-8")

    device = get_device(args.host)

    if args.spawn:
        print(f"[*] Spawneando {args.spawn} ...")
        pid = device.spawn([args.spawn])
        session = device.attach(pid)
    else:
        print(f"[*] Adjuntando a {args.attach} ...")
        session = device.attach(args.attach)
        pid = None

    script = session.create_script(script_src)
    script.on("message", make_message_handler(args.out))
    script.load()
    print(f"[+] Agente cargado. Salida -> {args.out}")

    # sanity check via RPC
    try:
        print(f"[*] RPC ping -> {script.exports_sync.ping()}")
    except Exception as e:
        print(f"[!] RPC no disponible: {e}")

    if pid is not None:
        device.resume(pid)
        print("[*] Proceso reanudado.")

    try:
        if args.duration > 0:
            time.sleep(args.duration)
        else:
            print("[*] Observando... Ctrl-C para terminar.")
            sys.stdin.read()
    except KeyboardInterrupt:
        pass
    finally:
        print("\n[*] Cerrando sesion.")
        try:
            session.detach()
        except Exception:
            pass


if __name__ == "__main__":
    main()
