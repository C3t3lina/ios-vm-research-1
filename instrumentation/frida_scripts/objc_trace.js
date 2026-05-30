/* =========================================================================
 * objc_trace.js - Hooks de Objective-C para analisis de apps iOS (Frida).
 *
 * Util tanto en el Simulador de Xcode (analisis de apps) como en el iOS
 * emulado en QEMU T8030. Solo lectura / telemetria (fair-use).
 *
 * Valida sintaxis:  node --check objc_trace.js
 * Carga:            frida -U -f <bundle> -l objc_trace.js
 *
 * NOTA sobre clases: en iOS las clases Foundation son NSString, NSURL, etc.
 * "String" (sin NS) es de Swift y NO existe en el runtime ObjC clasico.
 * ========================================================================= */
'use strict';

function log(msg) { console.log(`[objc] ${msg}`); }

if (typeof ObjC === 'undefined' || !ObjC.available) {
    log('Objective-C Runtime NO disponible en este proceso.');
} else {
    /* --- Hook de ejemplo: NSString -initWithString: --- */
    try {
        const NSString = ObjC.classes.NSString;
        const sel = '- initWithString:';
        if (NSString && NSString[sel]) {
            Interceptor.attach(NSString[sel].implementation, {
                onEnter(args) {
                    // args[0]=self, args[1]=selector, args[2]=NSString param
                    try {
                        const param = new ObjC.Object(args[2]);
                        log(`NSString initWithString: -> "${param.toString()}"`);
                    } catch (e) { /* puntero no convertible */ }
                },
                onLeave(retval) {
                    // aqui se podria inspeccionar/mutar retval (no lo hacemos)
                }
            });
            log('hook instalado en -[NSString initWithString:]');
        }
    } catch (err) {
        log('Error al enganchar NSString: ' + err.message);
    }

    /* --- Helper generico: traza todos los metodos de una clase --- */
    function traceClass(className) {
        const klass = ObjC.classes[className];
        if (!klass) { log(`clase no encontrada: ${className}`); return; }
        const methods = klass.$ownMethods;
        log(`trazando ${methods.length} metodos de ${className}`);
        methods.forEach(m => {
            try {
                Interceptor.attach(klass[m].implementation, {
                    onEnter() { log(`${className} ${m}`); }
                });
            } catch (e) { /* algunos metodos no son hookables */ }
        });
    }

    /* exponer al host para trazar clases bajo demanda */
    rpc.exports = {
        trace(name) { traceClass(name); return true; },
        classes(prefix) {
            return Object.keys(ObjC.classes)
                .filter(c => !prefix || c.indexOf(prefix) === 0)
                .slice(0, 200);
        },
    };

    log('agente ObjC listo. Usa rpc trace("NSURLSession") para trazar clases.');
}
