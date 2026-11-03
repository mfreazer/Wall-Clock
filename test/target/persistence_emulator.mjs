// ESP32 emulator test for a completed NVS write across a simulated reboot.
// Credentials are synthetic and flash exists only in this process's memory.
import { SimulatorWorker } from './package/dist/index.js';
import { readFileSync } from 'node:fs';

const flash = new Uint8Array(new SharedArrayBuffer(4 * 1024 * 1024)).fill(0xff);
const partitions = readFileSync('/build/partitions.bin');
let nvs;
for (let i = 0; i + 32 <= partitions.length; i += 32) {
  if (partitions.readUInt16LE(i) !== 0x50aa) break;
  const name = partitions.subarray(i + 12, i + 28).toString('ascii').replace(/\0.*$/, '');
  if (name === 'nvs' && partitions[i + 2] === 1 && partitions[i + 3] === 2) {
    if (nvs) throw new Error('duplicate nvs partition');
    nvs = { offset: partitions.readUInt32LE(i + 4), size: partitions.readUInt32LE(i + 8) };
  }
}
if (!nvs || nvs.offset !== 0x9000 || nvs.size !== 0x5000) throw new Error('unexpected NVS partition layout');
for (const [path, offset] of [['/build/bootloader.bin', 0x1000], ['/build/partitions.bin', 0x8000],
                              ['/boot_app0.bin', 0xe000], ['/build/firmware.bin', 0x10000]]) {
  const image = readFileSync(path);
  if (offset + image.length > flash.length) throw new Error(`${path} exceeds flash`);
  flash.set(image, offset);
}

async function boot(phase) {
  const sim = new SimulatorWorker();
  let uart = '', runtimeError;
  sim._onUART = byte => { uart += String.fromCharCode(byte); };
  sim._onError = error => { runtimeError = error; };
  async function waitFor(text) {
    for (let i = 0; i < 100; ++i) {
      await new Promise(resolve => setTimeout(resolve, 250));
      sim.pollUart();
      if (runtimeError) throw new Error(`emulator runtime error: ${runtimeError}`);
      if (uart.includes(text)) return;
    }
    throw new Error(`missing UART output: ${text}`);
  }
  try {
    await sim.init('ESP32', {flashSizeMB:4, mmuPages:64, strapValue:0x13, budget:2_000_000}, flash);
    sim.run();
    await waitFor('Provision: WIFI <ssid><TAB><password>');
    if (phase === 'write') {
      if (!uart.includes('wifi NOT_FOUND')) throw new Error('blank NVS did not report missing credentials');
      const before = Buffer.from(flash.slice(nvs.offset, nvs.offset + nvs.size));
      sim.sendUart('WIFI SandboxTest\tpassword123\n');
      await waitFor('Wi-Fi credentials saved');
      const after = Buffer.from(flash.slice(nvs.offset, nvs.offset + nvs.size));
      if (before.equals(after) || !after.includes(Buffer.from('SandboxTest')) || !after.includes(Buffer.from('password123'))) {
        throw new Error('synthetic credential pair was not observed in emulated NVS');
      }
    } else if (phase === 'reboot') {
      if (uart.includes('Invalid stored Wi-Fi credentials') || uart.includes('wifi NOT_FOUND') || uart.includes('ssid NOT_FOUND')) {
        throw new Error('stored credentials were not read successfully');
      }
    } else if (!uart.includes('wifi NOT_FOUND')) {
      throw new Error('erased-NVS negative control did not report missing credentials');
    }
    console.log(`PERSISTENCE PHASE PASSED: ${phase}`);
  } catch (error) {
    console.error(`PERSISTENCE PHASE FAILED: ${phase}`, error);
    console.log('UART OUTPUT START\n' + uart + '\nUART OUTPUT END');
    throw error;
  } finally {
    sim.terminate();
  }
}
try {
  await boot('write');
  await boot('reboot');
  flash.fill(0xff, nvs.offset, nvs.offset + nvs.size);
  await boot('erased-control');
  console.log('PERSISTENCE TEST PASSED: completed NVS write, simulated reboot, erased control');
} catch {
  process.exitCode = 1;
}
