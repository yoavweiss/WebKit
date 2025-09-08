//@ requireOptions("--useWasmSIMD=1")
//@ skip if !$isSIMDPlatform
import { instantiate } from "../wabt-wrapper.js"
import * as assert from "../assert.js"

/**
 * Convert a floating point value to WebAssembly text format
 * @param {number} val - JavaScript number value
 * @returns {string} - WebAssembly text format representation
 */
function floatToWasmText(val) {
    if (val === Number.POSITIVE_INFINITY) return 'inf';
    if (val === Number.NEGATIVE_INFINITY) return '-inf';
    if (Number.isNaN(val)) return 'nan';
    // Handle signed zero: -0.0 should be represented as "-0.0" in WebAssembly text
    if (val === 0 && 1/val === -Infinity) return '-0.0';
    return val.toString();
}

/**
 * Convert array to v128.const string based on instruction type
 * @param {Array} array - Input array
 * @param {string} instruction - SIMD instruction name
 * @returns {string} - v128.const string
 */
function arrayToV128Const(array, instruction) {
    if (instruction.startsWith('i8x16.') || instruction.startsWith('v128.')) {
        const hexValues = array.map(val => `0x${val.toString(16).padStart(2, '0').toUpperCase()}`);
        return `(v128.const i8x16 ${hexValues.join(' ')})`;
    } else if (instruction.startsWith('i16x8.')) {
        const hexValues = array.map(val => `0x${val.toString(16).padStart(4, '0').toUpperCase()}`);
        return `(v128.const i16x8 ${hexValues.join(' ')})`;
    } else if (instruction.startsWith('i32x4.')) {
        const hexValues = array.map(val => `0x${val.toString(16).padStart(8, '0').toUpperCase()}`);
        return `(v128.const i32x4 ${hexValues.join(' ')})`;
    } else if (instruction.startsWith('i64x2.')) {
        const hexValues = array.map(val => {
            const bigIntVal = typeof val === 'bigint' ? val : BigInt(val);
            return `0x${bigIntVal.toString(16).padStart(16, '0').toUpperCase()}`;
        });
        return `(v128.const i64x2 ${hexValues.join(' ')})`;
    } else if (instruction.startsWith('f32x4.')) {
        const wasmValues = array.map(floatToWasmText);
        return `(v128.const f32x4 ${wasmValues.join(' ')})`;
    } else if (instruction.startsWith('f64x2.')) {
        const wasmValues = array.map(floatToWasmText);
        return `(v128.const f64x2 ${wasmValues.join(' ')})`;
    }
    // Default fallback - assume it's already a string
    return array;
}

/**
 * Run SIMD instruction tests with given test data
 * @param {Array} testData - Array of test cases, each containing [instruction, input0, input1, expected]
 * @param {boolean} verbose - Whether to print verbose output
 * @param {string} testType - Description of test type for logging
 */
export async function runSIMDTests(testData, verbose = false, testType = "SIMD") {
    // Generate WebAssembly module
    let wat = `
(module
    (memory (export "memory") 1)
`;

    testData.forEach((test, index) => {
        const [instruction, input0, input1, expected] = test;
        const input0Str = Array.isArray(input0) ? arrayToV128Const(input0, instruction) : input0;
        const input1Str = Array.isArray(input1) ? arrayToV128Const(input1, instruction) : input1;
        const isUnaryOp = instruction.includes('.abs') || instruction.includes('.neg') || instruction.includes('.sqrt') || instruction.includes('.not');
        
        wat += `
    (func (export "test_${index}") (param $addr i32)
        (v128.store (local.get $addr)
            (${instruction} ${input0Str}${isUnaryOp ? '' : ' ' + input1Str}))
    )
`;
    });

    wat += `
)
`;

    if (verbose) {
        print("Generated WebAssembly text:");
        print(wat);
    }

    const instance = await instantiate(wat, {}, { simd: true });
    const memory = instance.exports.memory;
    const buffer = memory.buffer;
    const u8 = new Uint8Array(buffer);
    const u16 = new Uint16Array(buffer);
    const u32 = new Uint32Array(buffer);
    const u64 = new BigUint64Array(buffer);
    const f32 = new Float32Array(buffer);
    const f64 = new Float64Array(buffer);

    function clearMemory() {
        u8.fill(0);
    }

    for (let i = 0; i < wasmTestLoopCount; ++i) {
        testData.forEach((test, testIndex) => {
            const [instruction, input0, input1, expected] = test;
            
            if (verbose)
                print(`Testing ${instruction} test ${testIndex}...`);
            
            clearMemory();
            
            // Call the test function
            const testFunc = instance.exports[`test_${testIndex}`];
            testFunc(0);
            
            // Backtraces for table driven test cases is not helpful, so print test case context on failure.
            function assertEqWithContext(actual, expectedValue, lane, actualArray) {
                try {
                    assert.eq(actual, expectedValue);
                } catch (e) {
                    print(`\n=== TEST CASE FAILURE ===`);
                    print(`Test Index: ${testIndex}`);
                    print(`Instruction: ${instruction}`);
                    print(`Input 0: ${Array.isArray(input0) ? `[${input0.join(', ')}]` : input0}`);
                    print(`Input 1: ${Array.isArray(input1) ? `[${input1.join(', ')}]` : input1}`);
                    print(`Expected Array: [${expected.join(', ')}]`);
                    print(`Actual Array: [${Array.from(actualArray).join(', ')}]`);
                    print(`Lane: ${lane}`);
                    print(`Expected Value: ${expectedValue}`);
                    print(`Actual Value: ${actual}`);
                    print(`========================`);
                    throw e;
                }
            }

            // Verify the result using appropriate data type
            if (instruction.startsWith('i8x16.') || instruction.startsWith('v128.')) {
                for (let j = 0; j < 16; j++)
                    assertEqWithContext(u8[j], expected[j], j, u8.slice(0, 16));
            } else if (instruction.startsWith('i16x8.')) {
                for (let j = 0; j < 8; j++)
                    assertEqWithContext(u16[j], expected[j], j, u16.slice(0, 8));
            } else if (instruction.startsWith('i32x4.') ||
                        (instruction === 'f32x4.eq' || instruction === 'f32x4.ne' ||
                         instruction === 'f32x4.lt' || instruction === 'f32x4.gt' ||
                         instruction === 'f32x4.le' || instruction === 'f32x4.ge')) {
                for (let j = 0; j < 4; j++)
                    assertEqWithContext(u32[j], expected[j], j, u32.slice(0, 4));
            } else if (instruction.startsWith('f32x4.')) {
                for (let j = 0; j < 4; j++)
                    assertEqWithContext(f32[j], expected[j], j, f32.slice(0, 4));
            } else if (instruction.startsWith('i64x2.') ||
                        (instruction === 'f64x2.eq' || instruction === 'f64x2.ne' ||
                         instruction === 'f64x2.lt' || instruction === 'f64x2.gt' ||
                         instruction === 'f64x2.le' || instruction === 'f64x2.ge')) {
                for (let j = 0; j < 2; j++)
                    assertEqWithContext(u64[j], expected[j], j, u64.slice(0, 2));
            } else if (instruction.startsWith('f64x2.')) {
                for (let j = 0; j < 2; j++)
                    assertEqWithContext(f64[j], expected[j], j, f64.slice(0, 2));
            } else {
                assert.fail(`Unhandled instruction format: ${instruction}`);
            }
            
            if (verbose)
                print(`✓ ${instruction} test ${testIndex} passed`);
        });
    }
    
    if (verbose)
        print(`All ${testData.length} ${testType} tests passed!`);
}