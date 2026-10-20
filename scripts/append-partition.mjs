#!/usr/bin/env node
/**
 * Append one sized ota_N partition to a merged Mini Launcher flash image (local test helper).
 *
 * Usage:
 *   node scripts/append-partition.mjs <merged-launcher.bin> <app.bin> [out.bin]
 *
 * Reads the partition table at 0x8000, carves the next ota_* to fit the app
 * (64 KB aligned), writes the updated table + app image into out.bin (default: <launcher>-with-app.bin).
 */

import { readFileSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { basename, resolve } from "node:path";

const FLASH_SIZE = 0x800000;
const TABLE_OFF = 0x8000;
const TABLE_SIZE = 0xc00;
const ENTRY_SIZE = 32;
// ESP-IDF stores partition magic as little-endian u16 (bytes: aa 50).
const MAGIC = 0x50aa;
// MD5 sentinel is 16 bytes: eb eb + 14×ff, then 16-byte digest (gen_esp32part.py).
const MD5_MAGIC = 0xebeb;
const TYPE_APP = 0x00;
const SUB_FACTORY = 0x00;
const SUB_OTA_0 = 0x10;
const ALIGN = 0x10000;

function u16(v) {
  return v & 0xffff;
}
function u32(v) {
  return v >>> 0;
}

function readU16(buf, o) {
  return buf[o] | (buf[o + 1] << 8);
}
function readU32(buf, o) {
  return (buf[o] | (buf[o + 1] << 8) | (buf[o + 2] << 16) | (buf[o + 3] << 24)) >>> 0;
}
function writeU16(buf, o, v) {
  buf[o] = v & 0xff;
  buf[o + 1] = (v >> 8) & 0xff;
}
function writeU32(buf, o, v) {
  buf[o] = v & 0xff;
  buf[o + 1] = (v >> 8) & 0xff;
  buf[o + 2] = (v >> 16) & 0xff;
  buf[o + 3] = (v >>> 24) & 0xff;
}

function alignUp(n, a) {
  return (n + (a - 1)) & ~(a - 1);
}

function labelOf(entry) {
  return Buffer.from(entry.subarray(12, 28)).toString("utf8").replace(/\0+$/, "");
}

function parseTable(table) {
  const entries = [];
  let md5Offset = -1;
  for (let o = 0; o + ENTRY_SIZE <= table.length; o += ENTRY_SIZE) {
    const magic = readU16(table, o);
    if (magic === MD5_MAGIC) {
      md5Offset = o;
      break;
    }
    if (magic !== MAGIC) break;
    entries.push({
      offset: o,
      type: table[o + 2],
      subtype: table[o + 3],
      address: readU32(table, o + 4),
      size: readU32(table, o + 8),
      label: labelOf(table.subarray(o, o + ENTRY_SIZE)),
      flags: readU32(table, o + 28),
      raw: Buffer.from(table.subarray(o, o + ENTRY_SIZE)),
    });
  }
  return { entries, md5Offset };
}

function encodeEntry({ type, subtype, address, size, label, flags = 0 }) {
  const raw = Buffer.alloc(ENTRY_SIZE, 0);
  writeU16(raw, 0, MAGIC);
  raw[2] = type;
  raw[3] = subtype;
  writeU32(raw, 4, address);
  writeU32(raw, 8, size);
  Buffer.from(String(label).slice(0, 15), "utf8").copy(raw, 12);
  writeU32(raw, 28, flags);
  return raw;
}

function writeTable(entries) {
  const table = Buffer.alloc(TABLE_SIZE, 0xff);
  let o = 0;
  for (const e of entries) {
    const raw = e.raw || encodeEntry(e);
    raw.copy(table, o);
    o += ENTRY_SIZE;
  }
  const md5 = createHash("md5");
  md5.update(table.subarray(0, o));
  writeU16(table, o, MD5_MAGIC);
  table.fill(0xff, o + 2, o + 16);
  md5.digest().copy(table, o + 16);
  return table;
}

function isMerged(bin) {
  return bin.length > TABLE_OFF + 2 && readU16(bin, TABLE_OFF) === MAGIC;
}

/** Extract ESP app image from a merged flash dump or return app-only as-is. */
export function extractAppImage(bin) {
  if (!isMerged(bin)) {
    if (bin[0] !== 0xe9) throw new Error("Not an ESP app image (missing 0xe9 magic)");
    return Buffer.from(bin);
  }
  const { entries } = parseTable(bin.subarray(TABLE_OFF, TABLE_OFF + TABLE_SIZE));
  const app = entries.find((e) => e.type === TYPE_APP && e.subtype === SUB_FACTORY)
    || entries.find((e) => e.type === TYPE_APP);
  if (!app) throw new Error("No app partition in merged image");
  const end = Math.min(bin.length, app.address + app.size);
  let image = Buffer.from(bin.subarray(app.address, end));
  // Trim trailing 0xFF padding from the partition slot.
  let len = image.length;
  while (len > 0 && image[len - 1] === 0xff) len--;
  if (len === 0 || image[0] !== 0xe9) throw new Error("App partition has no valid image");
  return image.subarray(0, len);
}

export function measureAppSize(image) {
  return alignUp(image.length, ALIGN);
}

export function appendOtaPartition(flash, appImage) {
  if (!isMerged(flash)) throw new Error("Launcher image must be a merged bin (partition table at 0x8000)");
  if (appImage[0] !== 0xe9) throw new Error("App image missing 0xe9 magic");

  const tableBuf = Buffer.from(flash.subarray(TABLE_OFF, TABLE_OFF + TABLE_SIZE));
  const { entries } = parseTable(tableBuf);
  if (!entries.length) throw new Error("Empty partition table");

  const otas = entries.filter((e) => e.type === TYPE_APP && e.subtype >= SUB_OTA_0 && e.subtype <= SUB_OTA_0 + 15);
  if (otas.length >= 16) throw new Error("max apps (16)");

  let end = 0;
  for (const e of entries) {
    end = Math.max(end, e.address + e.size);
  }
  end = alignUp(end, ALIGN);

  const size = measureAppSize(appImage);
  if (end + size > FLASH_SIZE) {
    throw new Error(`not enough space (need ${size}, free ${FLASH_SIZE - end})`);
  }

  const subtype = SUB_OTA_0 + otas.length;
  const label = `ota_${otas.length}`;
  const next = {
    type: TYPE_APP,
    subtype,
    address: end,
    size,
    label,
    flags: 0,
  };
  next.raw = encodeEntry(next);

  const newEntries = [...entries.map((e) => ({ raw: e.raw })), next];
  const newTable = writeTable(newEntries);

  const out = Buffer.alloc(Math.max(flash.length, end + size), 0xff);
  flash.copy(out, 0, 0, Math.min(flash.length, out.length));
  newTable.copy(out, TABLE_OFF);
  appImage.copy(out, end);

  return { out, entry: next, freeAfter: FLASH_SIZE - (end + size) };
}

function main() {
  const [, , launcherPath, appPath, outPathArg] = process.argv;
  if (!launcherPath || !appPath) {
    console.error("Usage: node scripts/append-partition.mjs <merged-launcher.bin> <app.bin> [out.bin]");
    process.exit(1);
  }
  const launcher = readFileSync(resolve(launcherPath));
  const appRaw = readFileSync(resolve(appPath));
  const appImage = extractAppImage(appRaw);
  const { out, entry, freeAfter } = appendOtaPartition(launcher, appImage);
  const outPath = resolve(outPathArg || `${launcherPath.replace(/\.bin$/i, "")}-with-${basename(appPath)}`);
  writeFileSync(outPath, out);
  console.log(
    `Wrote ${outPath}\n  ${entry.label} subtype=0x${entry.subtype.toString(16)} @ 0x${entry.address.toString(16)} size=0x${entry.size.toString(16)}\n  app payload ${appImage.length} bytes; free after ${freeAfter} bytes`
  );
}

const isMain = process.argv[1] && resolve(process.argv[1]) === resolve(new URL(import.meta.url).pathname);
if (isMain) main();
