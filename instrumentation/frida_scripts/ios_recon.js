/* =========================================================================
 * ios_recon.js - Script de reconocimiento dinamico para iOS (Frida).
 *
 * Pensado para engancharse a un iOS emulado en QEMU T8030 (o device real
 * en lab). Agente de "solo lectura": observa, NO modifica comportamiento,
 * para mantenernos en el marco de investigacion/fair-use.
 *
 * Carga:  frida -U -f <bundle-id> -l ios_recon.js   (o ver host/run_frida.py)
 *
 * Funciones:
 *   - Inventario de modulos cargados (dyld image list)
 *   - Hook de open()/openat() para ver accesos a ficheros
 *   - Hook de NSLog para capturar logs de la app
 *   - Traza de llamadas a funciones criptograficas comunes
 *   - rpc.exports para control desde el host Python
 * ========================================================================= */
'use strict';

const CONFIG = {
    logFileAccess: true,
    logCrypto: true,
    logNSLog: true,
    // filtra ruido: solo loguea rutas que contengan alguno de estos
    fileFilter: [], // vacio = todas
};

function ts() { return new Date().toISOString().substr(11, 12); }
function emit(tag, msg, extra) {
    send({ type: 'recon', tag: tag, ts: ts(), msg: msg, extra: extra || null });
}

/* ---- 1. Inventario de modulos ---------------------------------------- */
function listModules() {
    const mods = Process.enumerateModules().map(m => ({
        name: m.name, base: m.base.toString(), size: m.size, path: m.path,
    }));
    emit('modules', `cargados ${mods.length} modulos`, mods.slice(0, 50));
    return mods;
}

/* ---- 2. Accesos a ficheros (open/openat) ----------------------------- */
function hookFileAccess() {
    if (!CONFIG.logFileAccess) return;
    ['open', 'openat'].forEach(fn => {
        const p = Module.findExportByName(null, fn);
        if (!p) return;
        Interceptor.attach(p, {
            onEnter(args) {
                // open: args[0]=path ; openat: args[1]=path
                const pathArg = fn === 'open' ? args[0] : args[1];
                try {
                    const path = pathArg.readUtf8String();
                    if (!path) return;
                    if (CONFIG.fileFilter.length &&
                        !CONFIG.fileFilter.some(f => path.indexOf(f) !== -1)) return;
                    emit('file', `${fn}("${path}")`);
                } catch (e) { /* puntero no legible */ }
            }
        });
    });
}

/* ---- 3. NSLog (logs de la app) --------------------------------------- */
function hookNSLog() {
    if (!CONFIG.logNSLog) return;
    const p = Module.findExportByName(null, 'NSLog');
    if (!p) return;
    Interceptor.attach(p, {
        onEnter(args) {
            try {
                const ObjC_ok = (typeof ObjC !== 'undefined') && ObjC.available;
                if (ObjC_ok) {
                    const fmt = new ObjC.Object(args[0]).toString();
                    emit('nslog', fmt);
                }
            } catch (e) { /* ignore */ }
        }
    });
}

/* ---- 4. Funciones criptograficas comunes ----------------------------- */
function hookCrypto() {
    if (!CONFIG.logCrypto) return;
    const targets = ['CCCrypt', 'SecKeyCreateSignature', 'CC_SHA256'];
    targets.forEach(name => {
        const p = Module.findExportByName(null, name);
        if (!p) return;
        Interceptor.attach(p, {
            onEnter() { emit('crypto', `llamada a ${name}()`); }
        });
        emit('hook', `instalado hook en ${name}`);
    });
}

/* ---- API expuesta al host Python ------------------------------------- */
rpc.exports = {
    modules() { return listModules(); },
    ranges() {
        return Process.enumerateRanges('r-x').slice(0, 100)
            .map(r => ({ base: r.base.toString(), size: r.size, prot: r.protection }));
    },
    ping() { return 'pong'; },
};

/* ---- arranque -------------------------------------------------------- */
emit('init', `agente iniciado en PID ${Process.id} (${Process.arch})`);
listModules();
hookFileAccess();
hookNSLog();
hookCrypto();
emit('ready', 'hooks instalados; observando...');
