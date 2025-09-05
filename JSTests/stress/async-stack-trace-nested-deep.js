//@ requireOptions("--useAsyncStackTrace=1")

const source = "async-stack-trace-nested-deep.js";

function nop() {}

function shouldThrowAsync(run, errorType, message, stackFunctions) {
    let actual;
    var hadError = false;
    run().then(
        function (value) {
            actual = value;
        },
        function (error) {
            hadError = true;
            actual = error;
        },
    );
    drainMicrotasks();
    if (!hadError) {
        throw new Error("Expected " + run + "() to throw " + errorType.name + ", but did not throw.");
    }
    if (!(actual instanceof errorType)) {
        throw new Error("Expected " + run + "() to throw " + errorType.name + ", but threw '" + actual + "'");
    }
    if (message !== void 0 && actual.message !== message) {
        throw new Error("Expected " + run + "() to throw '" + message + "', but threw '" + actual.message + "'");
    }

    const stackTrace = actual.stack;
    if (!stackTrace) {
        throw new Error("Expected error to have stack trace, but it was undefined");
    }

    const stackLines = stackTrace.split('\n').filter(line => line.trim());

    let stackLineIndex = 0;
    for (let i = 0; i < stackFunctions.length; i++) {
        const [expectedFunction, expectedLocation] = stackFunctions[i];
        const isNativeCode = expectedLocation === "[native code]" 
        const stackLine = stackLines[i];

        let found = false;

        if (isNativeCode) {
            if (stackLine === `${expectedFunction}@[native code]`)
                found = true;
        } else {
            if (stackLine === `${expectedFunction}@${source}:${expectedLocation}`)
                found = true;
            else if (stackLine === `${expectedFunction}@${source}`)
                found = true;
        }

        if (!found) {
            throw new Error(
                `Expected stack trace to contain '${expectedFunction}' at '${expectedLocation}', but got '${stackLine}'` +
                `\nActual stack trace:\n${stackTrace}\n`
            );
        }
    }
}

async function level1() {

    nop();
    await level2();
}

async function level2() {
    nop();

    await level3();

    nop();
}

async function level3() {
    await level4();

    nop();
}

async function level4() {

    nop();


    await level5();
}

async function level5() {
    nop();

    await level6();
}

async function level6() {
    nop();
    await level7();

}

async function level7() {

    nop();
    await level8();
}

async function level8() {
    await level9();
}

async function level9() {

    await level10();

}

async function level10() {
    await level11();
}

async function level11() {

    await level12();
}

async function level12() {
    nop();
    await level13();

}

async function level13() {

    await level14();
    nop();
}

async function level14() {
    await level15();

    nop();
}

async function level15() {

    nop();
    await level16();
}

async function level16() {
    await level17();
}

async function level17() {
    nop();

    await level18();
}

async function level18() {

    await level19();
    nop();
}

async function level19() {
    await level20();

}

async function level20() {

    nop();
    await level21();
}

async function level21() {
    await level22();
    nop();

}

async function level22() {

    await level23();
}

async function level23() {
    nop();
    await level24();

}

async function level24() {

    await level25();
    nop();
}

async function level25() {
    await level26();
}

async function level26() {
    nop();

    await level27();
}

async function level27() {

    await level28();
    nop();
}

async function level28() {
    await level29();

}

async function level29() {

    nop();
    await level30();
}

async function level30() {
    await level31();
    nop();

}

async function level31() {

    await level32();
}

async function level32() {
    nop();
    await level33();

}

async function level33() {

    await level34();
    nop();
}

async function level34() {
    await level35();
}

async function level35() {
    nop();

    await level36();
}

async function level36() {

    await level37();
    nop();
}

async function level37() {
    await level38();

}

async function level38() {

    nop();
    await level39();
}

async function level39() {
    await level40();
    nop();

}

async function level40() {

    await level41();
}

async function level41() {
    nop();
    await level42();

}

async function level42() {

    await level43();
    nop();
}

async function level43() {
    await level44();
}

async function level44() {
    nop();

    await level45();
}

async function level45() {

    await level46();
    nop();
}

async function level46() {
    await level47();

}

async function level47() {

    nop();
    await level48();
}

async function level48() {
    await level49();
    nop();

}

async function level49() {

    await level50();
}

async function level50() {
    nop();
    await level51();

}

async function level51() {

    await level52();
    nop();
}

async function level52() {
    await level53();
}

async function level53() {
    nop();

    await level54();
}

async function level54() {

    await level55();
    nop();
}

async function level55() {
    await level56();

}

async function level56() {

    nop();
    await level57();
}

async function level57() {
    await level58();
    nop();

}

async function level58() {

    await level59();
}

async function level59() {
    nop();
    await level60();

}

async function level60() {

    await level61();
    nop();
}

async function level61() {
    await level62();
}

async function level62() {
    nop();

    await level63();
}

async function level63() {

    await level64();
    nop();
}

async function level64() {
    await level65();

}

async function level65() {

    nop();
    await level66();
}

async function level66() {
    await level67();
    nop();

}

async function level67() {

    await level68();
}

async function level68() {
    nop();
    await level69();

}

async function level69() {

    await level70();
    nop();
}

async function level70() {
    await level71();
}

async function level71() {
    nop();

    await level72();
}

async function level72() {

    await level73();
    nop();
}

async function level73() {
    await level74();

}

async function level74() {

    nop();
    await level75();
}

async function level75() {
    await level76();
    nop();

}

async function level76() {

    await level77();
}

async function level77() {
    nop();
    await level78();

}

async function level78() {

    await level79();
    nop();
}

async function level79() {
    await level80();
}

async function level80() {
    nop();

    await level81();
}

async function level81() {

    await level82();
    nop();
}

async function level82() {
    await level83();

}

async function level83() {

    nop();
    await level84();
}

async function level84() {
    await level85();
    nop();

}

async function level85() {

    await level86();
}

async function level86() {
    nop();
    await level87();

}

async function level87() {

    await level88();
    nop();
}

async function level88() {
    await level89();
}

async function level89() {
    nop();

    await level90();
}

async function level90() {

    await level91();
    nop();
}

async function level91() {
    await level92();

}

async function level92() {

    nop();
    await level93();
}

async function level93() {
    await level94();
    nop();

}

async function level94() {

    await level95();
}

async function level95() {
    nop();
    await level96();

}

async function level96() {

    await level97();
    nop();
}

async function level97() {
    await level98();
}

async function level98() {
    nop();

    await level99();
}

async function level99() {

    await level100();
    nop();
}

async function level100() {
    await level101();

}

async function level101() {

    nop();
    await delayedOperation(1);

    await delayedOperation(2);
    nop();

    await problematicFunction();
}

async function delayedOperation(id) {

    nop();
    await id;
}

async function problematicFunction() {


    nop();
    throw new Error("error");
}

for (let i = 0; i < testLoopCount; i++) {
    shouldThrowAsync(
        async function test () {
            await level1();
        }, Error, "error",
        [
            ["problematicFunction", "645:20"],
            ["problematicFunction", "641:36"],
            ["level101", "632:30"],
            ["level100", "620:19"],
            ["level99", "615:19"],
            ["level98", "610:18"],
            ["level97", "604:18"],
            ["level96", "599:18"],
            ["level95", "593:18"],
            ["level94", "588:18"],
            ["level93", "581:18"],
            ["level92", "577:18"],
            ["level91", "570:18"],
            ["level90", "565:18"],
            ["level89", "560:18"],
            ["level88", "554:18"],
            ["level87", "549:18"],
            ["level86", "543:18"],
            ["level85", "538:18"],
            ["level84", "531:18"],
            ["level83", "527:18"],
            ["level82", "520:18"],
            ["level81", "515:18"],
            ["level80", "510:18"],
            ["level79", "504:18"],
            ["level78", "499:18"],
            ["level77", "493:18"],
            ["level76", "488:18"],
            ["level75", "481:18"],
            ["level74", "477:18"],
            ["level73", "470:18"],
            ["level72", "465:18"],
            ["level71", "460:18"],
            ["level70", "454:18"],
            ["level69", "449:18"],
            ["level68", "443:18"],
            ["level67", "438:18"],
            ["level66", "431:18"],
            ["level65", "427:18"],
            ["level64", "420:18"],
            ["level63", "415:18"],
            ["level62", "410:18"],
            ["level61", "404:18"],
            ["level60", "399:18"],
            ["level59", "393:18"],
            ["level58", "388:18"],
            ["level57", "381:18"],
            ["level56", "377:18"],
            ["level55", "370:18"],
            ["level54", "365:18"],
            ["level53", "360:18"],
            ["level52", "354:18"],
            ["level51", "349:18"],
            ["level50", "343:18"],
            ["level49", "338:18"],
            ["level48", "331:18"],
            ["level47", "327:18"],
            ["level46", "320:18"],
            ["level45", "315:18"],
            ["level44", "310:18"],
            ["level43", "304:18"],
            ["level42", "299:18"],
            ["level41", "293:18"],
            ["level40", "288:18"],
            ["level39", "281:18"],
            ["level38", "277:18"],
            ["level37", "270:18"],
            ["level36", "265:18"],
            ["level35", "260:18"],
            ["level34", "254:18"],
            ["level33", "249:18"],
            ["level32", "243:18"],
            ["level31", "238:18"],
            ["level30", "231:18"],
            ["level29", "227:18"],
            ["level28", "220:18"],
            ["level27", "215:18"],
            ["level26", "210:18"],
            ["level25", "204:18"],
            ["level24", "199:18"],
            ["level23", "193:18"],
            ["level22", "188:18"],
            ["level21", "181:18"],
            ["level20", "177:18"],
            ["level19", "170:18"],
            ["level18", "165:18"],
            ["level17", "160:18"],
            ["level16", "154:18"],
            ["level15", "150:18"],
            ["level14", "142:18"],
            ["level13", "137:18"],
            ["level12", "131:18"],
            ["level11", "126:18"],
            ["level10", "121:18"],
            ["level9", "116:18"],
            ["level8", "111:17"],
            ["level7", "107:17"],
            ["level6", "100:17"],
            ["level5", "95:17"],
            ["level4", "89:17"],
            ["level3", "79:17"],
            ["level2", "73:17"],
            ["level1", "67:17"],
            ["drainMicrotasks", "[native code]"],
            ["shouldThrowAsync", "19:20"],
            ["global code", "649:21"]
        ],
    );
    drainMicrotasks();
}
