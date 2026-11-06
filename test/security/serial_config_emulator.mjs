// Exercise USB configuration and a completed-write reboot in the ESP32 emulator.
// Synthetic settings only; this is not physical serial or power-loss testing.
import { SimulatorWorker } from './package/dist/index.js';
import { readFileSync } from 'node:fs';

const flash = new Uint8Array(new SharedArrayBuffer(4 * 1024 * 1024)).fill(0xff);
for (const [path, offset] of [['/build/bootloader.bin', 0x1000], ['/build/partitions.bin', 0x8000],
  ['/boot_app0.bin', 0xe000], ['/build/firmware.bin', 0x10000]]) {
  const image = readFileSync(path);
  if (offset + image.length > flash.length) throw new Error(`${path} exceeds flash`);
  flash.set(image, offset);
}

async function run(phase) {
  const sim = new SimulatorWorker();
  let uart = '', error;
  sim._onUART = byte => { uart += String.fromCharCode(byte); };
  sim._onError = e => { error = e; };
  async function wait(text, start = 0) {
    for (let i = 0; i < 100; i++) {
      await new Promise(resolve => setTimeout(resolve, 200));
      sim.pollUart();
      if (error) throw error;
      if (uart.indexOf(text, start) >= 0) return;
    }
    throw new Error(`missing UART response ${text}`);
  }
  async function command(line, expected) {
    const start = uart.length;
    sim.sendUart(line + '\n');
    await wait(expected, start);
    return uart.slice(start);
  }
  async function readConfig() {
    const output = await command('CONFIG GET', 'CONFIG JSON ');
    // Wait for this complete JSON line, not an earlier response or just its prefix.
    const start = uart.lastIndexOf('CONFIG JSON ');
    for (let i = 0; i < 30; i++) {
      const end = uart.indexOf('\n', start);
      if (end >= 0) return JSON.parse(uart.slice(start + 12, end).trim());
      await new Promise(resolve => setTimeout(resolve, 100));
      sim.pollUart();
      if (error) throw error;
    }
    throw new Error(`incomplete CONFIG JSON response: ${output}`);
  }
  try {
    await sim.init('ESP32', {flashSizeMB:4, mmuPages:64, strapValue:0x13, budget:2_000_000}, flash);
    sim.run();
    await wait('Provision: WIFI <ssid><TAB><password>');
    const initial = await readConfig();
    if (phase === 'write') {
      if (initial.count !== 60) throw new Error('unexpected initial pixel count');
      initial.count = 72;
      const body = JSON.stringify(initial);
      await command(`CONFIG BEGIN ${body.length}`, 'CONFIG READY');
      for (let offset = 0; offset < body.length; offset += 60) {
        await command(`CONFIG CHUNK ${offset} ${body.slice(offset, offset + 60)}`, 'CONFIG CHUNK OK');
      }
      await command('CONFIG COMMIT', 'CONFIG SAVED');
      const result = await readConfig();
      if (result.count !== 72) throw new Error('saved count not returned');
      await command('CONFIG BEGIN 3', 'CONFIG READY');
      await command('CONFIG CHUNK 1 x', 'CONFIG ERR invalid offset');
      await command('CONFIG COMMIT', 'CONFIG ERR not started');
      const unchanged = await readConfig();
      if (unchanged.count !== 72) throw new Error('invalid update changed configuration');
    } else if (initial.count !== 72) {
      throw new Error('completed configuration not restored after reboot');
    }
    console.log(`SERIAL CONFIG PHASE PASSED: ${phase}`);
  } catch (e) {
    console.error(`SERIAL CONFIG PHASE FAILED: ${phase}`, e);
    console.log('UART OUTPUT START\n' + uart + '\nUART OUTPUT END');
    throw e;
  } finally {
    sim.terminate();
  }
}
try {
  await run('write');
  await run('reboot');
  console.log('SERIAL CONFIG TEST PASSED: update, negative control and simulated reboot');
} catch {
  process.exitCode = 1;
}
