// Glyph protocol utilities for Radiant WebUI
// Spec: https://github.com/Radiant-Core/radiant-mcp-server/blob/main/docs/RADIANT_AI_KNOWLEDGE_BASE.md

// ── Helpers ───────────────────────────────────────────────────────────────

export function hex2buf(hex: string): Uint8Array {
  if (hex.length % 2 !== 0) hex = '0' + hex
  const out = new Uint8Array(hex.length / 2)
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16)
  return out
}

export function buf2hex(buf: Uint8Array): string {
  return Array.from(buf).map(b => b.toString(16).padStart(2, '0')).join('')
}

function concat(...arrays: Uint8Array[]): Uint8Array {
  const len = arrays.reduce((s, a) => s + a.length, 0)
  const out = new Uint8Array(len)
  let off = 0
  for (const a of arrays) { out.set(a, off); off += a.length }
  return out
}

// ── CBOR decoder ─────────────────────────────────────────────────────────

export function decodeCBOR(bytes: Uint8Array): unknown {
  let offset = 0

  function readByte(): number {
    if (offset >= bytes.length) throw new Error('CBOR: unexpected end')
    return bytes[offset++]
  }
  function readBytes(n: number): Uint8Array {
    const b = bytes.slice(offset, offset + n); offset += n; return b
  }
  function readUintArg(info: number): number {
    if (info <= 23) return info
    if (info === 24) return readByte()
    if (info === 25) { const b = readBytes(2); return (b[0] << 8) | b[1] }
    if (info === 26) { const b = readBytes(4); return ((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]) >>> 0 }
    if (info === 27) { readBytes(8); return 0 } // 64-bit: approximate as 0
    throw new Error(`CBOR: unsupported int info ${info}`)
  }

  function decode(): unknown {
    const byte = readByte()
    const major = byte >> 5
    const info  = byte & 0x1f
    switch (major) {
      case 0: return readUintArg(info)
      case 1: return -1 - readUintArg(info)
      case 2: return readBytes(readUintArg(info))
      case 3: { const b = readBytes(readUintArg(info)); return new TextDecoder().decode(b) }
      case 4: { const n = readUintArg(info); return Array.from({ length: n }, () => decode()) }
      case 5: {
        const n = readUintArg(info)
        const obj: Record<string, unknown> = {}
        for (let i = 0; i < n; i++) { const k = decode(); obj[String(k)] = decode() }
        return obj
      }
      case 6: { readUintArg(info); return decode() } // tagged value — unwrap
      case 7: {
        if (info === 20) return false
        if (info === 21) return true
        if (info === 22 || info === 23) return null
        if (info === 25) { readBytes(2); return 0 }
        if (info === 26) { readBytes(4); return 0 }
        if (info === 27) { readBytes(8); return 0 }
        return null
      }
      default: throw new Error(`CBOR: unknown major ${major}`)
    }
  }

  return decode()
}

// ── CBOR encoder ─────────────────────────────────────────────────────────

function cborHead(major: number, n: number): Uint8Array {
  const m = major << 5
  if (n <= 23)     return new Uint8Array([m | n])
  if (n <= 0xff)   return new Uint8Array([m | 24, n])
  if (n <= 0xffff) return new Uint8Array([m | 25, n >> 8, n & 0xff])
  return new Uint8Array([m | 26, (n >> 24) & 0xff, (n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff])
}

export function encodeCBOR(value: unknown): Uint8Array {
  if (value === null || value === undefined) return new Uint8Array([0xf6])
  if (typeof value === 'boolean') return new Uint8Array([value ? 0xf5 : 0xf4])
  if (typeof value === 'number' && Number.isInteger(value)) {
    if (value >= 0) return cborHead(0, value)
    return cborHead(1, -1 - value)
  }
  if (typeof value === 'string') {
    const utf8 = new TextEncoder().encode(value)
    return concat(cborHead(3, utf8.length), utf8)
  }
  if (value instanceof Uint8Array) {
    // Tag 64 (uint8 typed array, RFC 8746) matches Photonic's wire format for byte fields.
    return concat(new Uint8Array([0xd8, 0x40]), cborHead(2, value.length), value)
  }
  if (Array.isArray(value)) {
    return concat(cborHead(4, value.length), ...value.map(encodeCBOR))
  }
  if (typeof value === 'object') {
    const entries = Object.entries(value as Record<string, unknown>)
    return concat(cborHead(5, entries.length), ...entries.flatMap(([k, v]) => [encodeCBOR(k), encodeCBOR(v)]))
  }
  throw new Error(`Cannot CBOR encode ${typeof value}`)
}

// ── Base58Check (for address → PKH) ──────────────────────────────────────

const B58_CHARS = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
const B58_MAP = new Map(B58_CHARS.split('').map((c, i) => [c, BigInt(i)]))

function base58Decode(s: string): Uint8Array {
  let n = 0n
  for (const c of s) {
    const v = B58_MAP.get(c)
    if (v === undefined) throw new Error(`Invalid base58 char: ${c}`)
    n = n * 58n + v
  }
  const leading = s.match(/^1*/)?.[0].length ?? 0
  const bytes: number[] = []
  while (n > 0n) { bytes.unshift(Number(n & 0xffn)); n >>= 8n }
  return new Uint8Array([...new Array(leading).fill(0), ...bytes])
}

// Returns the 20-byte P2PKH public key hash (hex) from a base58check address.
export function addressToPKH(address: string): string {
  const decoded = base58Decode(address)
  if (decoded.length < 21) throw new Error('Invalid address (too short)')
  return buf2hex(decoded.slice(1, 21))
}

// ── Script utilities ──────────────────────────────────────────────────────

// Detect a glyph UTXO from its scriptPubKey hex.
// Glyph FT:  d0 <36 bytes ref> 75 76 a9 14 <20 bytes PKH> 88 ac  (63 bytes)
// Glyph NFT: d8 <36 bytes ref> 75 76 a9 14 <20 bytes PKH> 88 ac  (63 bytes)
export function parseGlyphScript(scriptHex: string): { ref: string; isSingleton: boolean } | null {
  const s = scriptHex.toLowerCase()
  if (s.length < 126) return null  // 63 bytes minimum
  const op = s.slice(0, 2)
  if (op !== 'd0' && op !== 'd8') return null  // OP_PUSHINPUTREF / OP_PUSHINPUTREFSINGLETON
  return { ref: s.slice(2, 74), isSingleton: op === 'd8' }
}

// Get the display txid and vout from a 36-byte ref (72 hex chars).
// ref = txid_bytes_LE (32 bytes, reversed) + vout_LE_uint32 (4 bytes)
export function refToTxidVout(refHex: string): { txid: string; vout: number } {
  const txidLE = refHex.slice(0, 64)
  const txid = Array.from({ length: 32 }, (_, i) => txidLE.slice(i * 2, i * 2 + 2)).reverse().join('')
  const vb = refHex.slice(64, 72)
  const vout = (parseInt(vb.slice(0, 2), 16)
              | (parseInt(vb.slice(2, 4), 16) << 8)
              | (parseInt(vb.slice(4, 6), 16) << 16)
              | (parseInt(vb.slice(6, 8), 16) << 24)) >>> 0
  return { txid, vout }
}

// Convert a display txid + vout back to the 36-byte ref hex.
export function txidVoutToRef(txid: string, vout: number): string {
  // txid is big-endian display hex — reverse to LE bytes
  const txidLE = Array.from({ length: 32 }, (_, i) => txid.slice(i * 2, i * 2 + 2)).reverse().join('')
  const voutLE = [vout & 0xff, (vout >> 8) & 0xff, (vout >> 16) & 0xff, (vout >> 24) & 0xff]
    .map(b => b.toString(16).padStart(2, '0')).join('')
  return txidLE + voutLE
}

// Build a glyph token output script.
export function buildGlyphOutputScript(refHex: string, recipientPKH: string, isSingleton: boolean): string {
  const op = isSingleton ? 'd8' : 'd0'
  return `${op}${refHex}7576a914${recipientPKH}88ac`
}

// Standard P2PKH script.
export function buildP2PKHScript(pkhHex: string): string {
  return `76a914${pkhHex}88ac`
}

// Parse the glyph CBOR bytes out of an OP_RETURN scriptPubKey hex.
// Returns raw CBOR bytes, or null if not a valid glyph envelope.
// Handles both multi-push (03 "gly" 02 vf cf PUSH(cbor)) and
// single-push (PUSHDATA1 N ["gly" vf cf cbor]) Photonic formats.
export function parseGlyphOpReturn(scriptHex: string): Uint8Array | null {
  const s = scriptHex.toLowerCase()
  const idx = s.indexOf('676c79') // "gly" magic
  if (idx < 0) return null

  let pos = idx + 6 // immediately after "gly" magic

  if (pos + 2 > s.length) return null
  const vfPush = parseInt(s.slice(pos, pos + 2), 16)
  pos += 2

  if (vfPush <= 0x4b) {
    // Standard short push: vfPush bytes of version+flags follow
    pos += vfPush * 2
  } else if (vfPush === 0x4c) {
    // PUSHDATA1: next 1 byte is length, then CBOR
    if (pos + 2 > s.length) return null
    const cborLen = parseInt(s.slice(pos, pos + 2), 16); pos += 2
    const cborHex = s.slice(pos, pos + cborLen * 2)
    if (cborHex.length !== cborLen * 2) return null
    return hex2buf(cborHex)
  } else if (vfPush === 0x4d) {
    // PUSHDATA2: next 2 bytes LE are length, then CBOR
    // Used by Photonic when the CBOR + image fits in <=65535 bytes
    if (pos + 4 > s.length) return null
    const lo = parseInt(s.slice(pos, pos + 2), 16)
    const hi = parseInt(s.slice(pos + 2, pos + 4), 16)
    const cborLen = lo | (hi << 8); pos += 4
    const cborHex = s.slice(pos, pos + cborLen * 2)
    if (cborHex.length !== cborLen * 2) return null
    return hex2buf(cborHex)
  } else if (vfPush === 0x4e) {
    // PUSHDATA4: next 4 bytes LE are length, then CBOR
    // Photonic embeds NFT image+metadata directly in the scriptSig using PUSHDATA4
    // after the "gly" push: 03 676c79  4e <len4LE> <CBOR bytes>
    if (pos + 8 > s.length) return null
    const b0 = parseInt(s.slice(pos, pos + 2), 16)
    const b1 = parseInt(s.slice(pos + 2, pos + 4), 16)
    const b2 = parseInt(s.slice(pos + 4, pos + 6), 16)
    const b3 = parseInt(s.slice(pos + 6, pos + 8), 16)
    const cborLen = b0 | (b1 << 8) | (b2 << 16) | (b3 * 0x1000000); pos += 8
    const cborHex = s.slice(pos, pos + cborLen * 2)
    if (cborHex.length !== cborLen * 2) return null
    return hex2buf(cborHex)
  } else {
    // vfPush > 0x4e: raw CBOR byte in single-push format — CBOR starts immediately
    // after "gly" with no version+flags section. Roll back to include this byte.
    return hex2buf(s.slice(pos - 2))
  }

  // After version+flags: read the CBOR push
  if (pos + 2 > s.length) return null
  const pb = parseInt(s.slice(pos, pos + 2), 16)
  pos += 2

  let cborLen: number
  if (pb <= 0x4b) {
    cborLen = pb
  } else if (pb === 0x4c) {
    if (pos + 2 > s.length) return null
    cborLen = parseInt(s.slice(pos, pos + 2), 16); pos += 2
  } else if (pb === 0x4d) {
    if (pos + 4 > s.length) return null
    const lo = parseInt(s.slice(pos, pos + 2), 16)
    const hi = parseInt(s.slice(pos + 2, pos + 4), 16)
    cborLen = lo | (hi << 8); pos += 4
  } else if (pb === 0x4e) {
    if (pos + 8 > s.length) return null
    const b0 = parseInt(s.slice(pos, pos + 2), 16)
    const b1 = parseInt(s.slice(pos + 2, pos + 4), 16)
    const b2 = parseInt(s.slice(pos + 4, pos + 6), 16)
    const b3 = parseInt(s.slice(pos + 6, pos + 8), 16)
    cborLen = b0 | (b1 << 8) | (b2 << 16) | (b3 * 0x1000000); pos += 8
  } else {
    // pb is not a recognized push opcode — likely a raw CBOR byte in
    // single-push format (no separate CBOR push opcode after version+flags).
    return hex2buf(s.slice(pos - 2))
  }

  const cborHex = s.slice(pos, pos + cborLen * 2)
  if (cborHex.length !== cborLen * 2) return null
  return hex2buf(cborHex)
}

// Build an OP_RETURN scriptPubKey hex containing a glyph metadata envelope.
// Format: OP_RETURN PUSH3 "gly" PUSH2 [version flags] PUSHDATA(cbor)
export function buildGlyphOpReturn(meta: GlyphMeta, version = 2, flags = 0): string {
  const cbor = encodeCBOR(meta)
  // Header push: "02 02 00" → version=2, flags=0
  const vfByte = `02${version.toString(16).padStart(2, '0')}${flags.toString(16).padStart(2, '0')}`
  // CBOR push data opcode
  let cborPush: string
  const cl = cbor.length
  if (cl <= 0x4b) {
    cborPush = cl.toString(16).padStart(2, '0') + buf2hex(cbor)
  } else if (cl <= 0xff) {
    cborPush = '4c' + cl.toString(16).padStart(2, '0') + buf2hex(cbor)
  } else if (cl <= 0xffff) {
    cborPush = '4d' + (cl & 0xff).toString(16).padStart(2, '0') + ((cl >> 8) & 0xff).toString(16).padStart(2, '0') + buf2hex(cbor)
  } else {
    cborPush = '4e' +
      (cl & 0xff).toString(16).padStart(2, '0') +
      ((cl >> 8) & 0xff).toString(16).padStart(2, '0') +
      ((cl >> 16) & 0xff).toString(16).padStart(2, '0') +
      ((cl >> 24) & 0xff).toString(16).padStart(2, '0') +
      buf2hex(cbor)
  }
  return `6a03676c79${vfByte}${cborPush}`
}

// ── Token grouping ────────────────────────────────────────────────────────

export interface GlyphMeta {
  v?: number
  p?: number[]
  name?: string
  ticker?: string
  decimals?: number
  desc?: string
  license?: string
  maxSupply?: number
  reward?: number
  content?: unknown
  attrs?: Record<string, string> | unknown[]
  main?: { t?: string; b?: Uint8Array; u?: string }
  [key: string]: unknown
}

export interface GlyphToken {
  ref: string        // 72-char hex
  refTxid: string    // display (big-endian) txid of reveal tx
  refVout: number
  isSingleton: boolean
  utxos: GlyphUTXO[]
  totalSats: number
}

export interface GlyphUTXO {
  txid: string
  vout: number
  address: string
  scriptPubKey: string
  photons: number
  confirmations: number
  spendable: boolean
  revealoutpoint?: string  // txid of the reveal/metadata tx containing CBOR (from listglyph)
}

// Extract the 20-byte PKH (hex) from a glyph output script, or null if not a glyph script.
// Glyph: d0/d8 {72 ref} 75 76 a9 14 {40 pkh} 88 ac
// PKH starts at hex offset 2+72+2+2+2+2 = 82
export function extractGlyphPKH(scriptHex: string): string | null {
  const glyph = parseGlyphScript(scriptHex)
  if (!glyph) return null
  const pkhStart = 82
  if (scriptHex.length < pkhStart + 40) return null
  return scriptHex.slice(pkhStart, pkhStart + 40).toLowerCase()
}

export function parseGlyphUTXOs(utxos: Array<{
  txid: string; vout: number; address: string; scriptPubKey?: string
  amount: number; confirmations: number; spendable: boolean
}>): GlyphToken[] {
  const map = new Map<string, GlyphToken>()
  for (const u of utxos) {
    if (!u.scriptPubKey) continue
    const parsed = parseGlyphScript(u.scriptPubKey)
    if (!parsed) continue
    const { ref, isSingleton } = parsed
    const { txid: refTxid, vout: refVout } = refToTxidVout(ref)
    const photons = Math.round(u.amount * 1e8)
    const gu: GlyphUTXO = { txid: u.txid, vout: u.vout, address: u.address, scriptPubKey: u.scriptPubKey, photons, confirmations: u.confirmations, spendable: u.spendable }
    const existing = map.get(ref)
    if (existing) {
      existing.utxos.push(gu)
      existing.totalSats += photons
    } else {
      map.set(ref, { ref, refTxid, refVout, isSingleton, utxos: [gu], totalSats: photons })
    }
  }
  return Array.from(map.values())
}

export function displayBalance(totalSats: number, decimals: number): string {
  const factor = Math.pow(10, decimals)
  return (totalSats / factor).toFixed(decimals)
}

// Convert embedded image bytes in a GlyphMeta `main.b` field to a data URL.
// `b` is a Uint8Array when decoded on the frontend via decodeCBOR, or a base64
// string when it comes from the getglyphmetadata RPC `decoded` field.
export function metaToDataUrl(meta: GlyphMeta): string | null {
  try {
    const main = meta.main as { t?: string; b?: unknown } | undefined
    if (!main?.b) return null
    const type = (main.t as string | undefined) ?? 'image/jpeg'
    // Only create data URLs for image types — text/html, text/plain etc. are handled separately
    if (!type.startsWith('image/')) return null
    if (main.b instanceof Uint8Array) {
      const bytes = main.b
      const CHUNK = 8192
      let binary = ''
      for (let i = 0; i < bytes.byteLength; i += CHUNK)
        binary += String.fromCharCode(...bytes.subarray(i, i + CHUNK))
      return `data:${type};base64,${btoa(binary)}`
    }
    if (typeof main.b === 'string' && main.b.length > 0)
      return `data:${type};base64,${main.b}`
    return null
  } catch { return null }
}

// ── Raw transaction building ──────────────────────────────────────────────

function encodeVarint(n: number): string {
  if (n < 0xfd) return n.toString(16).padStart(2, '0')
  if (n <= 0xffff) return 'fd' + uint16LE(n)
  if (n <= 0xffffffff) return 'fe' + uint32LE(n)
  throw new Error('varint too large')
}

function uint16LE(n: number): string {
  return [(n & 0xff), (n >> 8) & 0xff].map(b => b.toString(16).padStart(2, '0')).join('')
}

function uint32LE(n: number): string {
  return [n & 0xff, (n >> 8) & 0xff, (n >> 16) & 0xff, (n >> 24) & 0xff].map(b => b.toString(16).padStart(2, '0')).join('')
}

function uint64LE(n: bigint): string {
  const bytes: string[] = []
  for (let i = 0; i < 8; i++) { bytes.push(Number(n & 0xffn).toString(16).padStart(2, '0')); n >>= 8n }
  return bytes.join('')
}

// Build a raw transaction hex. Pass scriptSig per input for pre-signed inputs;
// omit (or leave undefined) for inputs to be signed by signrawtransactionwithwallet.
export function buildRawTx(
  inputs: Array<{ txid: string; vout: number; scriptSig?: string }>,
  outputs: Array<{ scriptHex: string; satoshis: number }>
): string {
  const parts: string[] = []
  parts.push('01000000') // version 1

  parts.push(encodeVarint(inputs.length))
  for (const inp of inputs) {
    const txidLE = Array.from({ length: 32 }, (_, i) => inp.txid.slice(i * 2, i * 2 + 2)).reverse().join('')
    parts.push(txidLE)
    parts.push(uint32LE(inp.vout))
    const ss = inp.scriptSig ?? ''
    parts.push(encodeVarint(ss.length / 2))
    parts.push(ss)
    parts.push('ffffffff')   // sequence
  }

  parts.push(encodeVarint(outputs.length))
  for (const out of outputs) {
    parts.push(uint64LE(BigInt(out.satoshis)))
    parts.push(encodeVarint(out.scriptHex.length / 2))
    parts.push(out.scriptHex)
  }

  parts.push('00000000') // locktime
  return parts.join('')
}

