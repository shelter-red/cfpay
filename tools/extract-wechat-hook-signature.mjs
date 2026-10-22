import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";

const inputPath = process.argv[2];
const profileName = process.argv[3] ?? "receipt";
const profiles = {
  transfer: { parserRva: 0x53c9350, callsiteRva: 0x53c97bc },
  receipt: { parserRva: 0x2293980, callsiteRva: 0x0ab207e },
};
const profile = profiles[profileName];

if (!inputPath || !profile) {
  console.error("用法: node tools/extract-wechat-hook-signature.mjs <Weixin.dll路径> [transfer|receipt]");
  process.exit(1);
}

const filePath = resolve(inputPath);
const image = await readFile(filePath);
const peOffset = image.readUInt32LE(0x3c);
if (image.toString("ascii", peOffset, peOffset + 4) !== "PE\0\0") throw new Error("INVALID_PE_SIGNATURE");

const coffOffset = peOffset + 4;
const sectionCount = image.readUInt16LE(coffOffset + 2);
const optionalHeaderSize = image.readUInt16LE(coffOffset + 16);
const optionalOffset = coffOffset + 20;
if (image.readUInt16LE(optionalOffset) !== 0x20b) throw new Error("EXPECTED_PE32_PLUS");

const imageBase = image.readBigUInt64LE(optionalOffset + 24);
const sectionTableOffset = optionalOffset + optionalHeaderSize;
const sections = [];
for (let index = 0; index < sectionCount; index += 1) {
  const offset = sectionTableOffset + index * 40;
  const name = image.toString("ascii", offset, offset + 8).replace(/\0+$/g, "");
  sections.push({
    name,
    virtualSize: image.readUInt32LE(offset + 8),
    virtualAddress: image.readUInt32LE(offset + 12),
    rawSize: image.readUInt32LE(offset + 16),
    rawOffset: image.readUInt32LE(offset + 20),
  });
}

function rvaToFileOffset(rva) {
  const section = sections.find((item) => {
    const size = Math.max(item.virtualSize, item.rawSize);
    return rva >= item.virtualAddress && rva < item.virtualAddress + size;
  });
  if (!section) throw new Error(`RVA_NOT_MAPPED:0x${rva.toString(16)}`);
  return { section, offset: section.rawOffset + rva - section.virtualAddress };
}

const parser = rvaToFileOffset(profile.parserRva);
const callsite = rvaToFileOffset(profile.callsiteRva);
const parserBytes = image.subarray(parser.offset, parser.offset + 32);
const callsiteBytes = image.subarray(callsite.offset, callsite.offset + 5);
if (callsiteBytes[0] !== 0xe8) throw new Error("EXPECTED_REL32_CALL");

const relativeTarget = callsiteBytes.readInt32LE(1);
const resolvedTargetRva = profile.callsiteRva + callsiteBytes.length + relativeTarget;
if (resolvedTargetRva !== profile.parserRva) throw new Error("CALL_TARGET_MISMATCH");

console.log(
  JSON.stringify(
    {
      file: filePath,
      profile: profileName,
      fileSize: image.length,
      sha256: createHash("sha256").update(image).digest("hex"),
      imageBase: `0x${imageBase.toString(16)}`,
      parser: {
        rva: `0x${profile.parserRva.toString(16)}`,
        section: parser.section.name,
        fileOffset: `0x${parser.offset.toString(16)}`,
        first32Bytes: parserBytes.toString("hex"),
      },
      callsite: {
        rva: `0x${profile.callsiteRva.toString(16)}`,
        section: callsite.section.name,
        fileOffset: `0x${callsite.offset.toString(16)}`,
        bytes: callsiteBytes.toString("hex"),
        resolvedTargetRva: `0x${resolvedTargetRva.toString(16)}`,
      },
    },
    null,
    2,
  ),
);
