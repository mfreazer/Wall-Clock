// Runs inside the isolated sandbox; esp32-emu 1.2.0 is fetched separately.
import { SimulatorWorker } from './package/dist/index.js';
import { readFileSync } from 'node:fs';

const flash = new Uint8Array(new SharedArrayBuffer(4 * 1024 * 1024)).fill(0xff);
for (const [path, offset] of [
  ['/build/bootloader.bin', 0x1000],
  ['/build/partitions.bin', 0x8000],
  ['/boot_app0.bin', 0xe000],
  ['/build/firmware.bin', 0x10000],
]) {
  const image = readFileSync(path);
  if (offset + image.length > flash.length) throw new Error(`${path} exceeds flash`);
  flash.set(image, offset);
}
const sim = new SimulatorWorker();
let uart = '';
let runtimeError;
sim._onUART = (byte) => { uart += String.fromCharCode(byte); };
sim._onError = (error) => { runtimeError = error; };
async function waitFor(text, attempts) {
  for (let i = 0; i < attempts; ++i) {
    await new Promise((resolve) => setTimeout(resolve, 250));
    sim.pollUart();
    if (runtimeError) throw new Error(`emulator runtime error: ${runtimeError}`);
    if (uart.includes(text)) return;
  }
  throw new Error(`did not observe UART output: ${text}`);
}
try {
  await sim.init('ESP32', {flashSizeMB: 4, mmuPages: 64, strapValue: 0x13, budget: 2_000_000}, flash);
  sim.run();
  await waitFor('Provision: WIFI <ssid><TAB><password>', 100);
  sim.sendUart('WIFI test\tshort\n');
  await waitFor('Invalid Wi-Fi credentials (SSID 1-32 bytes, password empty or 8-63 bytes)', 40);
  if (uart.includes('Wi-Fi credentials saved')) throw new Error('invalid credentials were saved');
  console.log('SMOKE TEST PASSED: boot prompt and invalid-credential rejection');
} catch (error) {
  console.error('SMOKE TEST FAILED:', error);
  process.exitCode = 1;
} finally {
  sim.terminate();
  console.log('UART OUTPUT START\n' + uart + '\nUART OUTPUT END');
}
