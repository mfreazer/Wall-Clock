// Executes the target time-zone probe inside the isolated emulator chroot.
import { SimulatorWorker } from './package/dist/index.js';
import { readFileSync } from 'node:fs';
const flash = new Uint8Array(new SharedArrayBuffer(4 * 1024 * 1024)).fill(0xff);
for (const [path, offset] of [['/build/bootloader.bin', 0x1000], ['/build/partitions.bin', 0x8000],
                              ['/boot_app0.bin', 0xe000], ['/build/firmware.bin', 0x10000]]) {
  const image = readFileSync(path);
  if (offset + image.length > flash.length) throw new Error(`${path} exceeds flash`);
  flash.set(image, offset);
}
const sim = new SimulatorWorker();
let uart = '';
let runtimeError;
sim._onUART = byte => { uart += String.fromCharCode(byte); };
sim._onError = error => { runtimeError = error; };
try {
  await sim.init('ESP32', {flashSizeMB:4, mmuPages:64, strapValue:0x13, budget:2_000_000}, flash);
  sim.run();
  for (let i=0; i<100 && !/TZ_TARGET_(PASS|FAIL) \d+/.test(uart); ++i) {
    await new Promise(resolve => setTimeout(resolve,250));
    sim.pollUart();
    if (runtimeError) throw new Error(`emulator runtime error: ${runtimeError}`);
  }
  if (!/^TZ_TARGET_PASS 19\r?$/m.test(uart) || uart.includes('TZ_CASE_FAIL') || uart.includes('TZ_TARGET_FAIL')) {
    throw new Error('target time-zone probe did not pass all 19 cases');
  }
  console.log('TIMEZONE PROBE PASSED');
} catch (error) {
  console.error('TIMEZONE PROBE FAILED:', error);
  process.exitCode = 1;
} finally {
  sim.terminate();
  console.log('UART OUTPUT START\n' + uart + '\nUART OUTPUT END');
}
