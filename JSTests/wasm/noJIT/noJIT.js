//@ skip if !$isWasmPlatform || $addressBits <= 32
//@ runDefaultWasm("--useJIT=0")
if (typeof WebAssembly == "undefined")
    throw new Error("Expect WebAssembly global object is defined");
