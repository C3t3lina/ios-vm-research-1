#!/usr/bin/env python3
"""
host/run_lab.py - Arnes de ejecucion del laboratorio T8030.

Orquesta el ciclo completo:
  1. (opcional) parchea el Device Tree con boot-args de lab via dt-patch.
  2. detecta el acelerador disponible (HVF en Mac Apple Silicon, si no TCG).
  3. lanza qemu-system-aarch64 con la maquina t8030.
  4. espera activa a que el puerto de frida-server (27042) levante en el guest.
  5. deja QEMU corriendo y te dice como enganchar Frida.

No requiere frida en el host para lanzar el lab (solo para instrumentar luego).

Ejemplo:
  ./run_lab.py \
      --qemu ../../build/QEMUAppleSilicon/build/qemu-system-aarch64 \
      --kernel out_firmware/kernelcache.raw \
      --dtb out_firmware/device_tree.bin \
      --ramdisk out_firmware/ramdisk.dmg \
      --trustcache out_firmware/tc.bin \
      --patch-bootargs "-v debug=0x14e amfi_get_out_of_my_way=1"
"""
import argparse
import os
import platform
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

FRIDA_PORT = 27042


def detect_accel():
    """Devuelve ('hvf'|'tcg', motivo) segun el host."""
    sysname = platform.system()
    machine = platform.machine()
    if sysname == "Darwin" and machine == "arm64":
        return "hvf", "Mac Apple Silicon detectado: HVF (Hypervisor.framework)"
    if sysname == "Darwin":
        return "tcg", "Mac Intel: HVF no acelera codigo ARM -> TCG"
    return "tcg", f"{sysname}/{machine}: sin HVF -> TCG (emulacion por software)"


def maybe_patch_dt(repo_root, dtb, bootargs):
    """Si se pidio, parchea el DT con dt-patch y devuelve la ruta nueva."""
    if not bootargs:
        return dtb
    dt_patch = repo_root / "native" / "dt-patch"
    if not dt_patch.exists():
        print("[*] Compilando dt-patch...")
        subprocess.run(["make", "dt-patch"], cwd=repo_root / "native", check=True)
    out = str(Path(dtb).with_suffix(".lab.bin"))
    print(f"[*] Parcheando DT con boot-args: \"{bootargs}\"")
    subprocess.run([str(dt_patch), dtb, out, "--boot-args", bootargs], check=True)
    return out


def build_cmd(args, accel):
    cmd = [
        args.qemu, "-M", "t8030",
        "-kernel", args.kernel,
        "-dtb", args.dtb,
        "-cpu", "max", "-smp", str(args.smp), "-m", args.mem,
        "-serial", "mon:stdio",
        "-netdev", f"user,id=net0,hostfwd=tcp::{FRIDA_PORT}-:{FRIDA_PORT}",
        "-device", "virtio-net-device,netdev=net0",
    ]
    if args.ramdisk:
        cmd += ["-initrd", args.ramdisk]
    if accel == "hvf":
        cmd += ["-accel", "hvf"]
    # nota: el fork t8030 puede requerir flags propios (trustcache-filename,
    # ramdisk-filename como propiedades de -M). Se pasan via --extra.
    if args.extra:
        cmd += args.extra
    return cmd


def wait_for_port(host, port, timeout):
    """Espera activa a que el puerto del guest este aceptando conexiones."""
    print(f"[*] Esperando frida-server en {host}:{port} (max {timeout}s)...")
    start = time.time()
    while time.time() - start < timeout:
        try:
            with socket.create_connection((host, port), timeout=1):
                return True
        except OSError:
            time.sleep(1)
    return False


def main():
    ap = argparse.ArgumentParser(description="Arnes de ejecucion del lab T8030.")
    ap.add_argument("--qemu", required=True, help="ruta a qemu-system-aarch64 (fork)")
    ap.add_argument("--kernel", required=True, help="kernelcache descomprimido")
    ap.add_argument("--dtb", required=True, help="Device Tree binario")
    ap.add_argument("--ramdisk", help="ramdisk/initrd (dmg)")
    ap.add_argument("--smp", type=int, default=6)
    ap.add_argument("--mem", default="4G")
    ap.add_argument("--patch-bootargs", help="parchea el DT con estos boot-args via dt-patch")
    ap.add_argument("--wait", type=int, default=120, help="segundos a esperar a frida-server")
    ap.add_argument("--no-wait", action="store_true", help="no esperar al puerto")
    ap.add_argument("--dry-run", action="store_true", help="solo imprime el comando")
    ap.add_argument("--extra", nargs=argparse.REMAINDER, default=[],
                    help="flags extra para QEMU (todo lo que siga)")
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parents[2]

    # validaciones tempranas
    if not args.dry_run and not shutil.which(args.qemu) and not Path(args.qemu).exists():
        print(f"[!] QEMU no encontrado: {args.qemu}", file=sys.stderr)
        sys.exit(1)
    for f in [args.kernel, args.dtb] + ([args.ramdisk] if args.ramdisk else []):
        if not Path(f).exists():
            print(f"[!] No existe: {f}", file=sys.stderr)
            sys.exit(1)

    accel, reason = detect_accel()
    print(f"[*] Acelerador: {accel.upper()}  ({reason})")

    args.dtb = maybe_patch_dt(repo_root, args.dtb, args.patch_bootargs)
    cmd = build_cmd(args, accel)

    print("[*] Comando QEMU:")
    print("    " + " ".join(cmd))
    if args.dry_run:
        return

    print("[*] Lanzando QEMU... (Ctrl-C para terminar)\n")
    proc = subprocess.Popen(cmd)

    try:
        if not args.no_wait:
            if wait_for_port("127.0.0.1", FRIDA_PORT, args.wait):
                print(f"\n[+] frida-server ARRIBA en 127.0.0.1:{FRIDA_PORT}")
                print("[+] Ahora en otra terminal:")
                print(f"    python3 host/run_frida.py -H 127.0.0.1:{FRIDA_PORT} "
                      f"-n SpringBoard -s frida_scripts/ios_recon.js")
            else:
                print("\n[!] Timeout: frida-server no levanto. Revisa el boot "
                      "por la consola serie (¿panic? usa scripts/panic_tracker.py).")
        proc.wait()
    except KeyboardInterrupt:
        print("\n[*] Terminando QEMU...")
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    main()
