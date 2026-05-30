/* =========================================================================
 * syscall_tracer.js - Tracer de syscalls y filesystem para iOS (Frida).
 *
 * Engancha la capa de syscalls de XNU desde userland (libsystem_kernel)
 * y reporta actividad de filesystem / red / procesos. Pensado para correr
 * tras SpringBoard en el iOS emulado (T8030) o device de lab.
 *
 * Solo lectura / telemetria (fair-use). Salida via send() -> run_frida.py
 * la guarda en JSONL para correlacionar con panic_tracker.py.
 *
 * Valida sintaxis:  node --check syscall_tracer.js
 * ========================================================================= */
'use strict';

const CFG = {
    fs: true,      // open/stat/unlink...
    net: true,     // socket/connect
    proc: true,    // posix_spawn
    maxArgLen: 256,
};

function ts() { return Date.now(); }
function emit(cat, name, detail) {
    send({ type: 'syscall', cat: cat, name: name, ts: ts(), detail: detail || null });
}

function readStr(p) {
    try { return p.isNull() ? null : p.readUtf8String(CFG.maxArgLen); }
    catch (e) { return null; }
}

function hook(name, opts) {
    const addr = Module.findExportByName(null, name);
    if (!addr) return false;
    Interceptor.attach(addr, opts);
    return true;
}

const installed = [];

/* ---- Filesystem ------------------------------------------------------ */
if (CFG.fs) {
    [
        ['open',    a => readStr(a[0])],
        ['openat',  a => readStr(a[1])],
        ['stat',    a => readStr(a[0])],
        ['lstat',   a => readStr(a[0])],
        ['unlink',  a => readStr(a[0])],
        ['access',  a => readStr(a[0])],
        ['rename',  a => `${readStr(a[0])} -> ${readStr(a[1])}`],
    ].forEach(([fn, extract]) => {
        if (hook(fn, {
            onEnter(args) { emit('fs', fn, extract(args)); }
        })) installed.push(fn);
    });
}

/* ---- Red ------------------------------------------------------------- */
if (CFG.net) {
    if (hook('connect', {
        onEnter(args) {
            // args[1] = struct sockaddr*; leemos familia y, si AF_INET, IP:puerto
            try {
                const sa = args[1];
                const family = sa.add(1).readU8();   // sa_family (BSD: byte 1)
                let detail = `family=${family}`;
                if (family === 2) { // AF_INET
                    const port = (sa.add(2).readU8() << 8) | sa.add(3).readU8();
                    const ip = `${sa.add(4).readU8()}.${sa.add(5).readU8()}.` +
                               `${sa.add(6).readU8()}.${sa.add(7).readU8()}`;
                    detail = `${ip}:${port}`;
                }
                emit('net', 'connect', detail);
            } catch (e) { emit('net', 'connect', '<unreadable>'); }
        }
    })) installed.push('connect');
}

/* ---- Procesos -------------------------------------------------------- */
if (CFG.proc) {
    if (hook('posix_spawn', {
        onEnter(args) { emit('proc', 'posix_spawn', readStr(args[1])); }
    })) installed.push('posix_spawn');
}

rpc.exports = {
    installed() { return installed; },
    ping() { return 'pong'; },
};

emit('init', 'tracer', `hooks instalados: ${installed.join(', ')}`);
