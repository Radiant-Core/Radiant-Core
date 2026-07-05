import { useState, useEffect, useCallback, useRef, useMemo } from 'react'
import QRCode from 'qrcode'
import { api, WalletSummary, AddressEntry, UTXOEntry, CoinEntry, GlyphRPCEntry, explorerTxUrl, explorerAddressUrl } from '../lib/api'
import SeedImport from '../components/SeedImport'
import {
  parseGlyphOpReturn, decodeCBOR, buildGlyphOpReturn,
  buildP2PKHScript, buildRawTx,
  addressToPKH, refToTxidVout, displayBalance, metaToDataUrl, hex2buf,
  replaceGlyphScriptPKH, isTokenBearing,
  GlyphToken, GlyphMeta, GlyphUTXO,
} from '../lib/glyph'

// ── Toast system ─────────────────────────────────────────────────────────────

interface ToastItem { id: number; msg: string; type: 'success'|'error'|'warning'|'info' }

const TOAST_COLORS = { success: '#00a8de', error: '#ef4444', warning: '#7fe0ff', info: 'var(--accent)' }
const TOAST_ICONS  = { success: '✓', error: '✗', warning: '⚠', info: 'ℹ' }

function ToastContainer({ toasts, onDismiss }: { toasts: ToastItem[]; onDismiss: (id: number) => void }) {
  return (
    <div style={{ position: 'fixed', bottom: '1.5rem', right: '1.5rem', zIndex: 9999, display: 'flex', flexDirection: 'column', gap: '0.5rem', maxWidth: '400px', width: 'calc(100vw - 3rem)', pointerEvents: 'none' }}>
      {toasts.map(t => (
        <div key={t.id} style={{
          display: 'flex', alignItems: 'flex-start', gap: '0.6rem',
          padding: '0.7rem 0.8rem',
          background: 'var(--bg2)', borderRadius: 'var(--radius)',
          borderLeft: `3px solid ${TOAST_COLORS[t.type]}`,
          border: `1px solid ${TOAST_COLORS[t.type]}40`,
          boxShadow: '0 4px 16px rgba(0,0,0,0.5)',
          pointerEvents: 'all',
        }}>
          <span style={{ color: TOAST_COLORS[t.type], fontSize: '0.85rem', marginTop: '0.1rem', flexShrink: 0 }}>{TOAST_ICONS[t.type]}</span>
          <span style={{ flex: 1, minWidth: 0, fontSize: '0.82rem', color: 'var(--text)', lineHeight: 1.5, wordBreak: 'break-word', overflowWrap: 'anywhere' }}>{t.msg}</span>
          <button onClick={() => onDismiss(t.id)} style={{ background: 'none', border: 'none', color: 'var(--text2)', cursor: 'pointer', fontSize: '1.1rem', lineHeight: 1, padding: '0', flexShrink: 0, marginTop: '-0.05rem' }}>×</button>
        </div>
      ))}
    </div>
  )
}

function CopyIcon({ size = 14 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M16 12.9V17.1C16 20.6 14.6 22 11.1 22H6.9C3.4 22 2 20.6 2 17.1V12.9C2 9.4 3.4 8 6.9 8H11.1C14.6 8 16 9.4 16 12.9Z" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"/>
      <path d="M22 6.9V11.1C22 14.6 20.6 16 17.1 16H16V12.9C16 9.4 14.6 8 11.1 8H8V6.9C8 3.4 9.4 2 12.9 2H17.1C20.6 2 22 3.4 22 6.9Z" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"/>
    </svg>
  )
}

function CopiedIcon({ size = 14 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M22 11.1V6.9C22 3.4 20.6 2 17.1 2H12.9C9.4 2 8 3.4 8 6.9V8H11.1C14.6 8 16 9.4 16 12.9V16H17.1C20.6 16 22 14.6 22 11.1Z" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"/>
      <path d="M16 17.1V12.9C16 9.4 14.6 8 11.1 8H6.9C3.4 8 2 9.4 2 12.9V17.1C2 20.6 3.4 22 6.9 22H11.1C14.6 22 16 20.6 16 17.1Z" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"/>
      <path d="M6.08008 15L8.03008 16.95L11.9201 13.05" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"/>
    </svg>
  )
}

// ── Wallet page ───────────────────────────────────────────────────────────────

type WalletTab = 'overview' | 'transactions' | 'utxos' | 'send' | 'psbt' | 'signverify' | 'consolidate' | 'tokens' | 'security'

const TABS: Array<{ id: WalletTab; label: string }> = [
  { id: 'overview',     label: 'Overview' },
  { id: 'transactions', label: 'Transactions' },
  { id: 'utxos',        label: 'UTXOs' },
  { id: 'send',         label: 'Send' },
  { id: 'psbt',         label: 'PSBT' },
  { id: 'signverify',   label: 'Sign / Verify' },
  { id: 'consolidate',  label: 'Consolidate' },
  { id: 'tokens',       label: 'Glyphs' },
  { id: 'security',     label: 'Security' },
]

const UNLOCK_DURATIONS = [
  { label: '5 min',   seconds: 300 },
  { label: '15 min',  seconds: 900 },
  { label: '30 min',  seconds: 1800 },
  { label: '1 hour',  seconds: 3600 },
  { label: '8 hours', seconds: 28800 },
]

function fmtCountdown(s: number) {
  const m = Math.floor(s / 60)
  return m > 0 ? `${m}m ${s % 60}s` : `${s}s`
}

// Format an RXD amount: thousands separators + trim trailing zeros after the decimal.
// Always keeps at least 2 decimal places (e.g. 822,110.29 or 0.00000123).
function fmtRXD(amount: number): string {
  const fixed = amount.toFixed(8)
  const [int, dec] = fixed.split('.')
  const trimmed = dec.replace(/0+$/, '') || '00'
  const significant = trimmed.length < 2 ? trimmed.padEnd(2, '0') : trimmed
  return `${Number(int).toLocaleString()}.${significant}`
}

// Derive the display type label, badge CSS class, and optional card icon from CBOR metadata.
// Glyph v2 protocol IDs (matches Photonic wallet protocols.ts):
//   1=FT  2=NFT  3=DAT  4=DMINT  5=MUT  6=BURN  7=CONTAINER  8=ENCRYPTED  9=TIMELOCK  10=AUTHORITY  11=WAVE
// meta.type semantic values: "user" | "container" | "fungible" | "mutable" | ...
function tokenTypeInfo(isSingleton: boolean, meta?: GlyphMeta): { label: string; cls: string; icon?: JSX.Element } {
  if (meta) {
    const p = Array.isArray(meta.p) ? (meta.p as number[]) : []
    const metaType = typeof meta.type === 'string' ? (meta.type as string) : ''
    if (p.includes(1)) {
      if (p.includes(6))  return { label: 'Burned FT', cls: 'red' }
      if (p.includes(4) || meta.dmint) return { label: 'dMint FT',  cls: 'blue' }
      return { label: 'FT', cls: 'green' }
    }
    if (p.includes(2)) {
      if (p.includes(6))  return { label: 'Burned NFT', cls: 'red' }
      if (p.includes(11)) return { label: 'WAVE',       cls: 'blue',  icon: <IconWave size={13} /> }
      if (p.includes(10)) return { label: 'Authority',  cls: 'blue' }
      if (p.includes(8))  return { label: 'Encrypted',  cls: 'blue' }
      // meta.type takes priority over generic p-flags for semantic types
      if (metaType === 'container') return { label: 'Container', cls: 'blue',  icon: <IconBox size={13} /> }
      if (metaType === 'user')      return { label: 'User',      cls: 'amber', icon: <IconUser size={13} /> }
      if (p.includes(7))  return { label: 'Container', cls: 'blue',  icon: <IconBox size={13} /> }
      if (p.includes(5))  return { label: 'Mutable',   cls: 'amber' }
      return { label: 'NFT', cls: 'amber' }
    }
    if (p.includes(3)) return { label: 'DAT', cls: 'blue' }
  }
  return isSingleton ? { label: 'NFT', cls: 'amber' } : { label: 'FT', cls: 'green' }
}

// Extract the URL from meta.main.u if present.
// Returns { url, isIpfs } or null.
function metaMainUrl(meta?: GlyphMeta): { url: string; isIpfs: boolean } | null {
  const main = (meta?.main) as { u?: unknown } | undefined
  if (!main?.u || typeof main.u !== 'string') return null
  const url = main.u
  return { url, isIpfs: url.startsWith('ipfs://') }
}

function IconLink({ size = 32 }: { size?: number }) {
  return (
    <svg stroke="currentColor" fill="none" strokeWidth={2} viewBox="0 0 24 24" strokeLinecap="round" strokeLinejoin="round" width={size} height={size}>
      <path d="M9 15l6 -6" /><path d="M11 6l.463 -.536a5 5 0 0 1 7.071 7.072l-.534 .464" /><path d="M13 18l-.397 .534a5.068 5.068 0 0 1 -7.127 0a4.972 4.972 0 0 1 0 -7.071l.524 -.463" />
    </svg>
  )
}
function IconBox({ size = 32 }: { size?: number }) {
  return (
    <svg stroke="currentColor" fill="none" strokeWidth={2} viewBox="0 0 24 24" strokeLinecap="round" strokeLinejoin="round" width={size} height={size}>
      <path d="M12 3l8 4.5l0 9l-8 4.5l-8 -4.5l0 -9l8 -4.5" /><path d="M12 12l8 -4.5" /><path d="M12 12l0 9" /><path d="M12 12l-8 -4.5" />
    </svg>
  )
}
function IconUser({ size = 32 }: { size?: number }) {
  return (
    <svg stroke="currentColor" fill="none" strokeWidth={2} viewBox="0 0 24 24" strokeLinecap="round" strokeLinejoin="round" width={size} height={size}>
      <path d="M12 12m-9 0a9 9 0 1 0 18 0a9 9 0 1 0 -18 0" /><path d="M12 10m-3 0a3 3 0 1 0 6 0a3 3 0 1 0 -6 0" /><path d="M6.168 18.849a4 4 0 0 1 3.832 -2.849h4a4 4 0 0 1 3.834 2.855" />
    </svg>
  )
}
function IconWave({ size = 32 }: { size?: number }) {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={2} strokeLinecap="round" strokeLinejoin="round" width={size} height={size}>
      <path d="M2 6c.6.5 1.2 1 2.5 1C7 7 7 5 9.5 5c2.6 0 2.4 2 5 2 2.5 0 2.5-2 5-2 1.3 0 1.9.5 2.5 1" />
      <path d="M2 12c.6.5 1.2 1 2.5 1 2.5 0 2.5-2 5-2 2.6 0 2.4 2 5 2 2.5 0 2.5-2 5-2 1.3 0 1.9.5 2.5 1" />
      <path d="M2 18c.6.5 1.2 1 2.5 1 2.5 0 2.5-2 5-2 2.6 0 2.4 2 5 2 2.5 0 2.5-2 5-2 1.3 0 1.9.5 2.5 1" />
    </svg>
  )
}
function IconDiscord({ size = 16 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="currentColor" xmlns="http://www.w3.org/2000/svg">
      <path d="M18.59 5.88997C17.36 5.31997 16.05 4.89997 14.67 4.65997C14.5 4.95997 14.3 5.36997 14.17 5.69997C12.71 5.47997 11.26 5.47997 9.83001 5.69997C9.69001 5.36997 9.49001 4.95997 9.32001 4.65997C7.94001 4.89997 6.63001 5.31997 5.40001 5.88997C2.92001 9.62997 2.25001 13.28 2.58001 16.87C4.23001 18.1 5.82001 18.84 7.39001 19.33C7.78001 18.8 8.12001 18.23 8.42001 17.64C7.85001 17.43 7.31001 17.16 6.80001 16.85C6.94001 16.75 7.07001 16.64 7.20001 16.54C10.33 18 13.72 18 16.81 16.54C16.94 16.65 17.07 16.75 17.21 16.85C16.7 17.16 16.15 17.42 15.59 17.64C15.89 18.23 16.23 18.8 16.62 19.33C18.19 18.84 19.79 18.1 21.43 16.87C21.82 12.7 20.76 9.08997 18.61 5.88997H18.59ZM8.84001 14.67C7.90001 14.67 7.13001 13.8 7.13001 12.73C7.13001 11.66 7.88001 10.79 8.84001 10.79C9.80001 10.79 10.56 11.66 10.55 12.73C10.55 13.79 9.80001 14.67 8.84001 14.67ZM15.15 14.67C14.21 14.67 13.44 13.8 13.44 12.73C13.44 11.66 14.19 10.79 15.15 10.79C16.11 10.79 16.87 11.66 16.86 12.73C16.86 13.79 16.11 14.67 15.15 14.67Z"/>
    </svg>
  )
}
function IconTwitterX({ size = 16 }: { size?: number }) {
  return (
    <svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" fill="currentColor" width={size} height={size}>
      <path d="M18.901 1.153h3.68l-8.04 9.19L24 22.846h-7.406l-5.8-7.584-6.638 7.584H.474l8.6-9.83L0 1.154h7.594l5.243 6.932ZM17.61 20.644h2.039L6.486 3.24H4.298Z" />
    </svg>
  )
}
function IconWebsite({ size = 16 }: { size?: number }) {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor" width={size} height={size}>
      <path d="M12.186 24h-.007c-3.581-.024-6.334-1.205-8.184-3.509C2.35 18.44 1.5 15.586 1.472 12.01v-.017c.03-3.579.879-6.43 2.525-8.482C5.845 1.205 8.6.024 12.18 0h.014c2.746.02 5.043.725 6.826 2.098 1.677 1.29 2.858 3.13 3.509 5.467l-2.04.569c-1.104-3.96-3.898-5.984-8.304-6.015-2.91.022-5.11.936-6.54 2.717C4.307 6.504 3.616 8.914 3.589 12c.027 3.086.718 5.496 2.057 7.164 1.43 1.783 3.631 2.698 6.54 2.717 2.623-.02 4.358-.631 5.8-2.045 1.647-1.613 1.618-3.593 1.09-4.798-.31-.71-.873-1.3-1.634-1.75-.192 1.352-.622 2.446-1.284 3.272-.886 1.102-2.14 1.704-3.73 1.79-1.202.065-2.361-.218-3.259-.801-1.063-.689-1.685-1.74-1.752-2.964-.065-1.19.408-2.285 1.33-3.082.88-.76 2.119-1.207 3.583-1.291a13.853 13.853 0 0 1 3.02.142c-.126-.742-.375-1.332-.75-1.757-.513-.586-1.308-.883-2.359-.89h-.029c-.844 0-1.992.232-2.721 1.32L7.734 7.847c.98-1.454 2.568-2.256 4.478-2.256h.044c3.194.02 5.097 1.975 5.287 5.388.108.046.216.094.321.142 1.49.7 2.58 1.761 3.154 3.07.797 1.82.871 4.79-1.548 7.158-1.85 1.81-4.094 2.628-7.277 2.65Zm1.003-11.69c-.242 0-.487.007-.739.021-1.836.103-2.98.946-2.916 2.143.067 1.256 1.452 1.839 2.784 1.767 1.224-.065 2.818-.543 3.086-3.71a10.5 10.5 0 0 0-2.215-.221z" />
    </svg>
  )
}
function IconGitHub({ size = 16 }: { size?: number }) {
  return (
    <svg fill="currentColor" width={size} height={size} viewBox="0 -0.5 25 25" xmlns="http://www.w3.org/2000/svg">
      <path d="m12.301 0h.093c2.242 0 4.34.613 6.137 1.68l-.055-.031c1.871 1.094 3.386 2.609 4.449 4.422l.031.058c1.04 1.769 1.654 3.896 1.654 6.166 0 5.406-3.483 10-8.327 11.658l-.087.026c-.063.02-.135.031-.209.031-.162 0-.312-.054-.433-.144l.002.001c-.128-.115-.208-.281-.208-.466 0-.005 0-.01 0-.014v.001q0-.048.008-1.226t.008-2.154c.007-.075.011-.161.011-.249 0-.792-.323-1.508-.844-2.025.618-.061 1.176-.163 1.718-.305l-.076.017c.573-.16 1.073-.373 1.537-.642l-.031.017c.508-.28.938-.636 1.292-1.058l.006-.007c.372-.476.663-1.036.84-1.645l.009-.035c.209-.683.329-1.468.329-2.281 0-.045 0-.091-.001-.136v.007c0-.022.001-.047.001-.072 0-1.248-.482-2.383-1.269-3.23l.003.003c.168-.44.265-.948.265-1.479 0-.649-.145-1.263-.404-1.814l.011.026c-.115-.022-.246-.035-.381-.035-.334 0-.649.078-.929.216l.012-.005c-.568.21-1.054.448-1.512.726l.038-.022-.609.384c-.922-.264-1.981-.416-3.075-.416s-2.153.152-3.157.436l.081-.02q-.256-.176-.681-.433c-.373-.214-.814-.421-1.272-.595l-.066-.022c-.293-.154-.64-.244-1.009-.244-.124 0-.246.01-.364.03l.013-.002c-.248.524-.393 1.139-.393 1.788 0 .531.097 1.04.275 1.509l-.01-.029c-.785.844-1.266 1.979-1.266 3.227 0 .025 0 .051.001.076v-.004c-.001.039-.001.084-.001.13 0 .809.12 1.591.344 2.327l-.015-.057c.189.643.476 1.202.85 1.693l-.009-.013c.354.435.782.793 1.267 1.062l.022.011c.432.252.933.465 1.46.614l.046.011c.466.125 1.024.227 1.595.284l.046.004c-.431.428-.718 1-.784 1.638l-.001.012c-.207.101-.448.183-.699.236l-.021.004c-.256.051-.549.08-.85.08-.022 0-.044 0-.066 0h.003c-.394-.008-.756-.136-1.055-.348l.006.004c-.371-.259-.671-.595-.881-.986l-.007-.015c-.198-.336-.459-.614-.768-.827l-.009-.006c-.225-.169-.49-.301-.776-.38l-.016-.004-.32-.048c-.023-.002-.05-.003-.077-.003-.14 0-.273.028-.394.077l.007-.003q-.128.072-.08.184c.039.086.087.16.145.225l-.001-.001c.061.072.13.135.205.19l.003.002.112.08c.283.148.516.354.693.603l.004.006c.191.237.359.505.494.792l.01.024.16.368c.135.402.38.738.7.981l.005.004c.3.234.662.402 1.057.478l.016.002c.33.064.714.104 1.106.112h.007c.045.002.097.002.15.002.261 0 .517-.021.767-.062l-.027.004.368-.064q0 .609.008 1.418t.008.873v.014c0 .185-.08.351-.208.466h-.001c-.119.089-.268.143-.431.143-.075 0-.147-.011-.214-.032l.005.001c-4.929-1.689-8.409-6.283-8.409-11.69 0-2.268.612-4.393 1.681-6.219l-.032.058c1.094-1.871 2.609-3.386 4.422-4.449l.058-.031c1.739-1.034 3.835-1.645 6.073-1.645h.098-.005zm-7.64 17.666q.048-.112-.112-.192-.16-.048-.208.032-.048.112.112.192.144.096.208-.032zm.497.545q.112-.08-.032-.256-.16-.144-.256-.048-.112.08.032.256.159.157.256.047zm.48.72q.144-.112 0-.304-.128-.208-.272-.096-.144.08 0 .288t.272.112zm.672.673q.128-.128-.064-.304-.192-.192-.32-.048-.144.128.064.304.192.192.32.044zm.913.4q.048-.176-.208-.256-.24-.064-.304.112t.208.24q.24.097.304-.096zm1.009.08q0-.208-.272-.176-.256 0-.256.176 0 .208.272.176.256.001.256-.175zm.929-.16q-.032-.176-.288-.144-.256.048-.224.24t.288.128.225-.224z"/>
    </svg>
  )
}
function IconLinkedIn({ size = 16 }: { size?: number }) {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor" width={size} height={size}>
      <path d="M20.447 20.452h-3.554v-5.569c0-1.328-.027-3.037-1.852-3.037-1.853 0-2.136 1.445-2.136 2.939v5.667H9.351V9h3.414v1.561h.046c.477-.9 1.637-1.85 3.37-1.85 3.601 0 4.267 2.37 4.267 5.455v6.286zM5.337 7.433a2.062 2.062 0 01-2.063-2.065 2.064 2.064 0 112.063 2.065zm1.782 13.019H3.555V9h3.564v11.452zM22.225 0H1.771C.792 0 0 .774 0 1.729v20.542C0 23.227.792 24 1.771 24h20.451C23.2 24 24 23.227 24 22.271V1.729C24 .774 23.2 0 22.222 0h.003z" />
    </svg>
  )
}

function socialLink(platform: string, value: string): { icon: JSX.Element; href: string | null } | null {
  const k = platform.toLowerCase()
  const v = value.trim()
  const handle = v.startsWith('@') ? v.slice(1) : v
  if (k === 'twitter' || k === 'x') return { icon: <IconTwitterX size={14} />, href: `https://x.com/${handle}` }
  if (k === 'discord') return { icon: <IconDiscord size={14} />, href: `https://discord.com/users/${handle}` }
  if (k === 'website' || k === 'url' || k === 'site') return { icon: <IconWebsite size={14} />, href: v.startsWith('http') ? v : `https://${v}` }
  if (k === 'github') return { icon: <IconGitHub size={14} />, href: `https://github.com/${handle}` }
  if (k === 'linkedin') return { icon: <IconLinkedIn size={14} />, href: `https://linkedin.com/in/${handle}` }
  return null
}

// Placeholder shown in the card thumbnail when there is no embedded image.
function thumbnailPlaceholder(meta: GlyphMeta | undefined, loading: boolean): string | JSX.Element {
  if (loading) return '…'
  if (!meta) return '?'
  const p = Array.isArray(meta.p) ? (meta.p as number[]) : []
  if (p.includes(6))  return 'BURN'
  if (p.includes(11)) return <IconWave size={48} />
  const metaType = typeof meta.type === 'string' ? meta.type : ''
  if (metaType === 'container' || p.includes(7)) return <IconBox size={48} />
  if (metaType === 'user') return <IconUser size={48} />
  const mu = metaMainUrl(meta)
  if (mu) return <IconLink size={48} />
  // Non-image inline content (text/html, text/plain, etc.)
  const mainI = meta.main as { b?: unknown; t?: unknown } | undefined
  if (mainI?.b instanceof Uint8Array && typeof mainI?.t === 'string') return <IconLink size={48} />
  if (meta.dmint) return 'DMINT'
  return '?'
}

export default function WalletsPage({ masked = false, refreshKey = 0 }: { masked?: boolean; refreshKey?: number }) {
  const [walletName, setWalletName] = useState<string | null>(null)
  const [tab, setTab]               = useState<WalletTab>('overview')
  const [summary, setSummary]       = useState<WalletSummary | null>(null)
  const [addresses, setAddresses]   = useState<AddressEntry[]>([])
  const [addrFilter, setAddrFilter] = useState('')
  const [txs, setTxs]               = useState<Record<string, unknown>[]>([])
  const [txPage, setTxPage]         = useState(0)
  const [txHideDust, setTxHideDust] = useState(true)
  const [utxos, setUtxos]           = useState<UTXOEntry[] | null>(null)
  const [showSpent, setShowSpent]   = useState(false)
  const [coins, setCoins]           = useState<CoinEntry[] | null>(null)
  const [coinsLoading, setCoinsLoading] = useState(false)
  const [toasts, setToasts]         = useState<ToastItem[]>([])
  const toastSeq                    = useRef(0)
  const toast = useCallback((msg: string, type: ToastItem['type'] = 'info') => {
    const id = ++toastSeq.current
    setToasts(prev => [...prev, { id, msg, type }])
    const ms = type === 'error' ? 9000 : type === 'warning' ? 7000 : 4000
    setTimeout(() => setToasts(prev => prev.filter(t => t.id !== id)), ms)
  }, [])
  const dismissToast = useCallback((id: number) => setToasts(prev => prev.filter(t => t.id !== id)), [])
  const [noWallet, setNoWallet]     = useState(false)
  const [copied, setCopied]         = useState<string | null>(null)
  const [qrAddr, setQrAddr]         = useState<string | null>(null)
  const [qrDataUrl, setQrDataUrl]   = useState<string | null>(null)
  const [txModal, setTxModal]       = useState<import('../lib/api').AddressEntry | null>(null)

  // Lock / unlock banner
  const [bannerPw, setBannerPw]           = useState('')
  const [unlockDuration, setUnlockDuration] = useState(300)
  const [unlockUntil, setUnlockUntil]     = useState<number | null>(null)
  const [secondsLeft, setSecondsLeft]     = useState(0)

  // Inline label editing
  const [editingAddr, setEditingAddr] = useState<string | null>(null)
  const [editLabelVal, setEditLabelVal] = useState('')
  const labelInputRef = useRef<HTMLInputElement>(null)

  // New address
  const [newLabel, setNewLabel] = useState('')

  // Send
  const [sendAddr, setSendAddr]           = useState('')
  const [sendAmt, setSendAmt]             = useState('')
  const [sendComment, setSendComment]     = useState('')
  const [sendChangeAddr, setSendChangeAddr] = useState('')
  const [sendManualInputs, setSendManualInputs] = useState(false)
  const [sendUtxos, setSendUtxos]         = useState<UTXOEntry[]>([])
  const [sendUtxosLoading, setSendUtxosLoading] = useState(false)
  const [sendSelectedKeys, setSendSelectedKeys] = useState<Set<string>>(new Set())

  // PSBT
  const [psbtAddr, setPsbtAddr]             = useState('')
  const [psbtAmount, setPsbtAmount]         = useState('')
  const [psbtOutputList, setPsbtOutputList] = useState<Array<{ address: string; amount: number }>>([])
  const [psbtChangeAddr, setPsbtChangeAddr] = useState('')
  const [psbtResult, setPsbtResult]         = useState('')
  const [psbtInput, setPsbtInput]           = useState('')
  const [psbtSignResult, setPsbtSignResult] = useState('')

  // Sign / Verify
  const [signAddr, setSignAddr]         = useState('')
  const [signMsg, setSignMsg]           = useState('')
  const [signResult, setSignResult]     = useState('')
  const [verifyAddr, setVerifyAddr]     = useState('')
  const [verifySig, setVerifySig]       = useState('')
  const [verifyMsg, setVerifyMsg]       = useState('')
  const [verifyResult, setVerifyResult] = useState<boolean | null>(null)

  // Consolidate
  const [consMin, setConsMin]           = useState('0.001')
  const [consMax, setConsMax]           = useState('')
  const [consDest, setConsDest]         = useState('')
  const [consMaxBatch, setConsMaxBatch] = useState(200)
  const [consMaxRuns, setConsMaxRuns]   = useState(0)
  const [consScanned, setConsScanned]   = useState<UTXOEntry[] | null>(null)
  const [consRunning, setConsRunning]   = useState(false)
  const [consLog, setConsLog]           = useState<string[]>([])

  // Tokens (glyph)
  const [glyphMeta, setGlyphMeta]               = useState<Map<string, GlyphMeta>>(new Map())
  const [glyphMetaLoading, setGlyphMetaLoading] = useState<Set<string>>(new Set())
  const [glyphMetaFailed, setGlyphMetaFailed]   = useState<Set<string>>(new Set())
  const [rpcGlyphEntries, setRpcGlyphEntries]   = useState<GlyphRPCEntry[] | null>(null)
  const [glyphLoading, setGlyphLoading]         = useState(false)
  const [glyphSort, setGlyphSort]               = useState<'newest' | 'oldest' | 'name'>('newest')
  const [glyphFilter, setGlyphFilter]           = useState('All')
  const [tokenSendRef, setTokenSendRef]         = useState<GlyphToken | null>(null)
  const [tokenSendAmt, setTokenSendAmt]         = useState('')
  const [tokenSendAddr, setTokenSendAddr]       = useState('')
  const [tokenSendPassphrase, setTokenSendPassphrase] = useState('')
  const [pendingTokenTx, setPendingTokenTx]     = useState<{
    signedHex: string
    txid: string
    recipient: string
    fee: number
    ticker: string
    decimals: number
    tokenAmtSats: number
  } | null>(null)
  const [showRawHex, setShowRawHex]             = useState(false)
  const [showMintHex, setShowMintHex]           = useState(false)
  const [selectedTokenRef, setSelectedTokenRef] = useState<string | null>(null)
  const glyphLoadingRef                         = useRef(false)
  const rpcGlyphEntriesRef                      = useRef<GlyphRPCEntry[] | null>(null)
  const [mintType, setMintType]                 = useState<'ft' | 'nft' | 'user' | 'container'>('nft')
  const [mintName, setMintName]                 = useState('')
  const [mintTicker, setMintTicker]             = useState('')

  const [mintSupply, setMintSupply]             = useState('')
  const [mintDesc, setMintDesc]                 = useState('')
  const [mintLicense, setMintLicense]           = useState('')
  const [mintDataMode, setMintDataMode]         = useState<'none' | 'file' | 'url' | 'text'>('none')
  const [mintFileBytes, setMintFileBytes]       = useState<Uint8Array | null>(null)
  const [mintFileMime, setMintFileMime]         = useState('')
  const [mintFileName, setMintFileName]         = useState('')
  const [mintFilePreview, setMintFilePreview]   = useState<string | null>(null)
  const [mintContentUrl, setMintContentUrl]     = useState('')
  const [mintAttrs, setMintAttrs]               = useState<{ name: string; value: string }[]>([])
  const [mintImmutable, setMintImmutable]       = useState(true)
  const [mintRunning, setMintRunning]           = useState(false)
  const [mintSubTab, setMintSubTab]             = useState<'list' | 'mint'>('list')
  const [mintText, setMintText]                 = useState('')
  const [mintAuthor, setMintAuthor]             = useState('')
  const [mintContainer, setMintContainer]       = useState('')
  const [mintFeeEstimate, setMintFeeEstimate]   = useState<{ feeSats: number; txBytes: number; feeRatePerKb: number } | null>(null)
  const [mintFundingKey, setMintFundingKey]     = useState('')  // "txid:vout" of chosen funding UTXO
  const [mintPreviewTxs, setMintPreviewTxs]    = useState<{ commitHex: string; revealHex: string; commitTxid: string; dmint?: { ftRef: string; contractRefs: string[]; nftRef: string } } | null>(null)
  const [mintSuccess, setMintSuccess]          = useState<{ revealTxid: string; commitTxid: string; dmint?: { ftRef: string; contractRefs: string[]; nftRef: string } } | null>(null)
  const [mintDeployMethod, setMintDeployMethod] = useState<'direct' | 'dmint'>('direct')
  const [dmintNumMints, setDmintNumMints]       = useState('1000')
  const [dmintReward, setDmintReward]           = useState('1000')
  const [dmintDifficulty, setDmintDifficulty]   = useState('10')
  const [dmintAlgorithm, setDmintAlgorithm]     = useState<'sha256d' | 'blake3' | 'k12'>('blake3')
  const [dmintDaaMode, setDmintDaaMode]         = useState<'fixed' | 'asert' | 'lwma'>('asert')
  const [dmintTargetBlockTime, setDmintTargetBlockTime] = useState('60')
  const [dmintHalfLife, setDmintHalfLife]       = useState('240')
  const [dmintNumContracts, setDmintNumContracts] = useState('1')
  const [dmintPremine, setDmintPremine]         = useState('0')

  // Security
  const [unlockPw, setUnlockPw]     = useState('')
  const [encryptPw, setEncryptPw]   = useState('')
  const [encryptPw2, setEncryptPw2] = useState('')
  const [backupBusy, setBackupBusy] = useState(false)
  const [oldPw, setOldPw]           = useState('')
  const [newPw, setNewPw]           = useState('')
  const [newPw2, setNewPw2]         = useState('')

  const maskAmt = (s: string) => masked ? '••••' : s

  const clearMsg = () => {}  // toasts auto-dismiss; kept for call-site compatibility
  const utxosRef = useRef<UTXOEntry[] | null>(null)
  useEffect(() => { utxosRef.current = utxos }, [utxos])

  // Consolidation reminder — once on first UTXO load, then hourly
  const consolidationAlerted = useRef(false)
  useEffect(() => {
    if (utxos === null) { consolidationAlerted.current = false; return }
    if (consolidationAlerted.current) return
    consolidationAlerted.current = true
    const count = utxos.filter(u => u.spendable && u.amount > 0.001).length
    if (count > 5) toast(`This wallet has ${count} spendable UTXOs — visit the Consolidate tab to merge them and reduce future fees.`, 'warning')
  }, [utxos, toast])

  useEffect(() => {
    if (walletName === null) return
    const id = setInterval(() => {
      const list = utxosRef.current
      if (!list) return
      const count = list.filter(u => u.spendable && u.amount > 0.001).length
      if (count > 5) toast(`Reminder: this wallet has ${count} UTXOs — consider consolidating to reduce fees.`, 'warning')
    }, 60 * 60 * 1000)
    return () => clearInterval(id)
  }, [walletName, toast])

  const loadSummary = useCallback(async (name: string) => {
    try { setSummary(await api.walletSummary(name)) }
    catch (e: unknown) { toast(e instanceof Error ? e.message : 'Failed to load wallet', 'error') }
  }, [])

  const loadAddresses = useCallback(async (name: string) => {
    try {
      const list = await api.walletAddresses(name)
      const addrs = Array.isArray(list) ? list : []
      setAddresses(addrs)
      if (addrs.length > 0) setSendChangeAddr(addrs[0].address)
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Failed to load addresses', 'error') }
  }, [])

  const loadTxs = useCallback(async (name: string) => {
    try {
      const raw = await api.walletTxs(name)
      // listsinceblock returns {transactions: [...], lastblock: "..."}
      let arr: Record<string, unknown>[]
      if (raw && typeof raw === 'object' && !Array.isArray(raw)) {
        const obj = raw as Record<string, unknown>
        if (Array.isArray(obj.transactions)) {
          arr = obj.transactions as Record<string, unknown>[]
        } else if (obj.error) {
          // RPC returned an error object — surface it
          const errMsg = typeof obj.error === 'object'
            ? String((obj.error as Record<string,unknown>).message ?? JSON.stringify(obj.error))
            : String(obj.error)
          toast(`Transactions: ${errMsg}`, 'error')
          return
        } else {
          arr = []
        }
      } else {
        arr = Array.isArray(raw) ? (raw as Record<string, unknown>[]) : []
      }
      // Newest first — use blocktime (actual block timestamp) for confirmed txs;
      // fall back to time (wallet-seen time, set at rescan for old txs)
      const txTs = (tx: Record<string, unknown>) => Number(tx.blocktime ?? tx.time ?? 0)
      arr = [...arr].sort((a, b) => txTs(b) - txTs(a))
      setTxs(arr)
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Failed to load transactions', 'error') }
  }, [])

  const loadUTXOs = useCallback(async (name: string) => {
    try {
      const list = await api.walletUTXOs(name)
      setUtxos(Array.isArray(list) ? list : [])
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Failed to load UTXOs', 'error') }
  }, [])

  const loadCoins = useCallback(async (name: string, all: boolean) => {
    setCoinsLoading(true)
    try {
      const res = await api.walletCoins(name, all)
      setCoins(Array.isArray(res.utxos) ? res.utxos : [])
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Failed to load coins', 'error') }
    finally { setCoinsLoading(false) }
  }, [])

  // Total RXD of manually-selected send UTXOs
  const sendSelectedTotal = useMemo(
    () => sendUtxos
      .filter(u => sendSelectedKeys.has(`${u.txid}:${u.vout}`))
      .reduce((s, u) => s + u.amount, 0),
    [sendUtxos, sendSelectedKeys]
  )

  // Per-address balance from UTXOs
  const addrBalance = useMemo(() => {
    const m = new Map<string, number>()
    if (utxos) for (const u of utxos) m.set(u.address, (m.get(u.address) ?? 0) + u.amount)
    return m
  }, [utxos])

  // Glyph tokens: group RPC listglyph entries by token ref
  const glyphTokens = useMemo<GlyphToken[]>(() => {
    const entries = rpcGlyphEntries ?? []
    const map = new Map<string, GlyphToken>()
    for (const e of entries) {
      const { txid: refTxid, vout: refVout } = refToTxidVout(e.ref)
      const isSingleton = e.type === 'nft'
      const gu: GlyphUTXO = {
        txid: e.txid, vout: e.vout, address: e.address,
        scriptPubKey: e.scriptPubKey, photons: e.photons,
        confirmations: e.confirmations, spendable: true,
        revealoutpoint: e.revealoutpoint,
      }
      const existing = map.get(e.ref)
      if (existing) {
        existing.utxos.push(gu)
        existing.totalSats += e.photons
      } else {
        map.set(e.ref, { ref: e.ref, refTxid, refVout, isSingleton, utxos: [gu], totalSats: e.photons })
      }
    }
    return Array.from(map.values())
  }, [rpcGlyphEntries])

  // Sorted and filtered view of glyphTokens for display.
  // Tokens whose metadata fetch completed but found only OP_RETURN CBOR (invalid Glyph v2)
  // are recorded in glyphMetaFailed and excluded from display.
  const displayedTokens = useMemo(() => {
    let tokens = glyphTokens.filter(t => !glyphMetaFailed.has(t.ref))
    if (glyphFilter !== 'All') {
      tokens = tokens.filter(t => {
        const meta = glyphMeta.get(t.ref)
        if (!meta) return false  // hide unloaded tokens when a type filter is active
        return tokenTypeInfo(t.isSingleton, meta).label === glyphFilter
      })
    }
    return [...tokens].sort((a, b) => {
      if (glyphSort === 'name') {
        const an = glyphMeta.get(a.ref)?.name ?? ''
        const bn = glyphMeta.get(b.ref)?.name ?? ''
        if (!an && !bn) return 0
        if (!an) return 1   // unloaded names sort last
        if (!bn) return -1
        return an.localeCompare(bn)
      }
      const ac = a.utxos[0]?.confirmations ?? 0
      const bc = b.utxos[0]?.confirmations ?? 0
      return glyphSort === 'newest' ? ac - bc : bc - ac
    })
  }, [glyphTokens, glyphMeta, glyphMetaFailed, glyphSort, glyphFilter])

  // Lazy-load metadata for each unique glyph token ref.
  // Glyph v2 (Photonic protocol): CBOR is in the scriptSig of the reveal tx.
  // Only scriptSig-sourced metadata is accepted; OP_RETURN glyphs are excluded as invalid.
  useEffect(() => {
    type VinEntry = { txid: string; vout: number; scriptSig?: { hex?: string } }
    type RawTx = { vout?: Array<{ scriptPubKey?: { hex?: string } }>; vin?: VinEntry[] }

    // Fetch a wallet-tracked tx via gettransaction + decoderawtransaction.
    async function fetchWalletTx(txid: string): Promise<{ tx: RawTx; blockhash?: string } | null> {
      try {
        const wres = await api.rpc('gettransaction', [txid], walletName ?? undefined)
        const wtx = wres.result as { hex?: string; blockhash?: string } | null
        if (wtx?.hex) {
          const dres = await api.rpc('decoderawtransaction', [wtx.hex])
          if (dres.result) return { tx: dres.result as RawTx, blockhash: wtx.blockhash }
        }
      } catch { /* not wallet-tracked */ }
      // Fallback: getrawtransaction (requires txindex or mempool)
      try {
        const res = await api.rpc('getrawtransaction', [txid, true])
        if (res.result) return { tx: res.result as RawTx }
      } catch { /* needs txindex */ }
      return null
    }

    // Search a decoded tx for glyph CBOR metadata.
    // Glyph v2 (Photonic protocol) embeds CBOR in the scriptSig of the reveal tx:
    //   [sig][pubkey]["gly"][cbor]
    // OP_RETURN output format is not valid Glyph v2 — ignored here intentionally.
    // refTxid/refVout identify the genesis outpoint — in a multi-glyph tx each token's
    // CBOR is in the input that spends its own genesis outpoint, so we try that first.
    function extractMeta(tx: RawTx, refTxid?: string, refVout?: number): GlyphMeta | null {
      const tryVin = (inp: VinEntry): GlyphMeta | null => {
        const hex = inp.scriptSig?.hex
        if (!hex) return null
        const cborBytes = parseGlyphOpReturn(hex)
        if (!cborBytes) return null
        try { return decodeCBOR(cborBytes) as GlyphMeta } catch { return null }
      }
      // Prioritised: input spending this token's genesis outpoint
      if (refTxid !== undefined && refVout !== undefined) {
        for (const inp of tx.vin ?? []) {
          if (inp.txid === refTxid && inp.vout === refVout) {
            const meta = tryVin(inp)
            if (meta !== null) return meta
          }
        }
      }
      // Fallback: first input with glyph CBOR (single-glyph txes)
      for (const inp of tx.vin ?? []) {
        const meta = tryVin(inp)
        if (meta !== null) return meta
      }
      return null
    }

    for (const token of glyphTokens) {
      if (glyphMeta.has(token.ref) || glyphMetaLoading.has(token.ref)) continue
      setGlyphMetaLoading(prev => new Set([...prev, token.ref]))
      ;(async () => {
        try {
          const utxoTxid = token.utxos[0]?.txid
          if (!utxoTxid) return

          // If listglyph found the mint tx in the wallet history, use it directly.
          const mintTxid = token.utxos[0]?.revealoutpoint
          if (mintTxid) {
            const result = await fetchWalletTx(mintTxid)
            if (result) {
              const meta = extractMeta(result.tx, token.refTxid, token.refVout)
              if (meta) { setGlyphMeta(prev => new Map([...prev, [token.ref, meta]])); return }
            }
          }

          // For dMint contract UTXOs the CBOR lives in the genesis contract tx
          // (identified by token.refTxid), which may be many hops back.
          // Try it directly before the walk to avoid hitting MAX_HOPS.
          const genesisTxid = token.refTxid
          {
            const gr = await fetchWalletTx(genesisTxid)
            if (gr) {
              const gm = extractMeta(gr.tx, token.refTxid, token.refVout)
              if (gm) { setGlyphMeta(prev => new Map([...prev, [token.ref, gm]])); return }
            }
          }

          // Walk backwards following the glyph-token vin chain.
          // In Photonic-style txs the fee input is often vin[0] and the token input is vin[1]
          // (or later). Blindly following vin[0] walks into fee history and never reaches the
          // mint tx. Instead, for multi-input txs we peek at each vin's previous output script
          // to find the one that carries our glyph ref (63-byte d0/d8 glyph or 75-byte dMint).
          const MAX_HOPS = 30
          const visited = new Set<string>()
          let currentTxid = utxoTxid

          for (let hop = 0; hop < MAX_HOPS; hop++) {
            if (visited.has(currentTxid)) break
            visited.add(currentTxid)

            const result = await fetchWalletTx(currentTxid)
            if (!result) break

            const meta = extractMeta(result.tx, token.refTxid, token.refVout)
            if (meta) { setGlyphMeta(prev => new Map([...prev, [token.ref, meta]])); return }

            if (currentTxid === genesisTxid) break

            const vins = (result.tx.vin ?? []).filter(v => v.txid)
            if (vins.length === 0) break

            let nextTxid: string | null = null
            if (vins.length === 1) {
              nextTxid = vins[0].txid
            } else {
              // Identify the glyph vin by checking each previous output's script.
              const refLow = token.ref.toLowerCase()
              for (const vin of vins) {
                if (visited.has(vin.txid)) continue
                const prevR = await fetchWalletTx(vin.txid)
                if (!prevR) continue
                const sh = (prevR.tx.vout?.[vin.vout ?? 0]?.scriptPubKey?.hex ?? '').toLowerCase()
                // Standard 63-byte glyph: (d0|d8) + [ref:72hex] + ...
                if (sh.length === 126 &&
                    (sh.startsWith('d0') || sh.startsWith('d8')) &&
                    sh.slice(2, 74) === refLow) {
                  nextTxid = vin.txid; break
                }
                // dMint 75-byte contract: 76a914[pkh20]88acbdd0[ref:72hex]+...
                if (sh.length >= 126 &&
                    sh.startsWith('76a914') &&
                    sh.slice(46, 50) === '88ac' &&
                    sh.slice(50, 54) === 'bdd0' &&
                    sh.slice(54, 126) === refLow) {
                  nextTxid = vin.txid; break
                }
              }
              if (!nextTxid) nextTxid = vins[0].txid  // fallback
            }

            if (!nextTxid) break
            currentTxid = nextTxid
          }

          // Final fallback: ask the C++ backend via the wallet spend index.
          // The backend's getglyphmetadata does a forward walk from genesis using mapWallet,
          // which finds the reveal tx even when txindex=0 (e.g. Photonic commit/reveal in same block).
          if (walletName) {
            try {
              const gmr = await api.walletGlyphMetadata(walletName, token.ref)
              if (gmr?.metadata && gmr.source !== 'opreturn') {
                const bytes = Uint8Array.from(
                  gmr.metadata.match(/.{2}/g)!.map((b: string) => parseInt(b, 16))
                )
                const decoded = decodeCBOR(bytes) as GlyphMeta
                setGlyphMeta(prev => new Map([...prev, [token.ref, decoded]])); return
              }
            } catch { /* not found or RPC unavailable */ }
          }
        } catch { /* non-fatal */ }
        finally {
          setGlyphMetaLoading(prev => { const s = new Set(prev); s.delete(token.ref); return s })
          // If nothing was found after the full walk, mark as failed so the token is hidden.
          setGlyphMeta(prev => {
            if (!prev.has(token.ref))
              setGlyphMetaFailed(f => new Set([...f, token.ref]))
            return prev
          })
        }
      })()
    }
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [glyphTokens])

  useEffect(() => {
    api.walletsLoaded()
      .then(w => {
        if (!w.walletSupportEnabled || w.wallets.length === 0) { setNoWallet(true); return }
        const name = w.wallets[0].name
        setWalletName(name)
        loadSummary(name)
        loadAddresses(name)
        loadTxs(name)
        loadUTXOs(name)
      })
      .catch(e => toast(e instanceof Error ? e.message : 'Failed', 'error'))
  }, [loadSummary, loadAddresses, loadTxs, loadUTXOs])

  // Unlock countdown ticker
  useEffect(() => {
    if (!unlockUntil) return
    const tick = () => {
      const left = Math.max(0, Math.round((unlockUntil - Date.now()) / 1000))
      setSecondsLeft(left)
      if (left === 0) {
        setUnlockUntil(null)
        if (walletName) loadSummary(walletName)
      }
    }
    tick()
    const id = setInterval(tick, 1000)
    return () => clearInterval(id)
  }, [unlockUntil, walletName, loadSummary])

  useEffect(() => {
    if (editingAddr) labelInputRef.current?.focus()
  }, [editingAddr])

  useEffect(() => {
    if (!qrAddr) { setQrDataUrl(null); return }
    QRCode.toDataURL(qrAddr, { margin: 2, width: 220, color: { dark: '#000000', light: '#ffffff' } })
      .then(setQrDataUrl).catch(() => {})
  }, [qrAddr])

  useEffect(() => { setTxPage(0) }, [txs])

  // Auto-populate consScanned whenever utxos or filter inputs change so the
  // Consolidate button is enabled as soon as UTXOs are loaded.
  useEffect(() => {
    if (!utxos) { setConsScanned(null); return }
    const min = parseFloat(consMin) || 0
    const max = parseFloat(consMax) || Infinity
    setConsScanned(utxos.filter(u => u.spendable && u.amount >= min && u.amount <= max))
  }, [utxos, consMin, consMax])

  const switchTab = (t: WalletTab) => {
    clearMsg()
    setTab(t)
    // Trigger glyph load immediately on tab switch — don't wait for the useEffect
    // to fire after paint. Uses refs so the values are always current.
    if (t === 'tokens' && walletName !== null && rpcGlyphEntriesRef.current === null && !glyphLoadingRef.current) {
      loadGlyphTokens(walletName)
    }
  }

  // ── Handlers ──

  const handleBannerUnlock = async () => {
    if (walletName === null || !bannerPw) return
    clearMsg()
    try {
      await api.walletUnlock(walletName, bannerPw, unlockDuration)
      setBannerPw('')
      setUnlockUntil(Date.now() + unlockDuration * 1000)
      loadSummary(walletName)
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Unlock failed', 'error') }
  }

  const handleBannerLock = async () => {
    if (walletName === null) return
    clearMsg()
    try {
      await api.walletLock(walletName)
      setUnlockUntil(null)
      loadSummary(walletName)
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Lock failed', 'error') }
  }

  const handleNewAddress = async () => {
    if (walletName === null) return
    clearMsg()
    try {
      const addr = await api.walletNewAddress(walletName, newLabel || undefined)
      toast(`New address: ${addr}`, 'success')
      setNewLabel('')
      loadAddresses(walletName)
      loadUTXOs(walletName)
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Failed', 'error') }
  }

  const startEditLabel = (a: AddressEntry) => { setEditingAddr(a.address); setEditLabelVal(a.label) }

  const commitLabel = async () => {
    if (!editingAddr || walletName === null) { setEditingAddr(null); return }
    const addr = editingAddr; const label = editLabelVal
    setEditingAddr(null)
    try {
      await api.walletSetLabel(walletName, addr, label)
      setAddresses(prev => prev.map(a => a.address === addr ? { ...a, label } : a))
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Failed to set label', 'error') }
  }

  const handleCopy = (text: string) => {
    navigator.clipboard.writeText(text).then(() => {
      setCopied(text); setTimeout(() => setCopied(c => c === text ? null : c), 2000)
    })
  }

  const handleSend = async () => {
    if (walletName === null || !sendAddr || !sendAmt) return
    clearMsg()
    const changeAddr = sendChangeAddr.trim() || undefined
    try {
      let txid: string
      if (sendManualInputs && sendSelectedKeys.size > 0) {
        const inputs = sendUtxos
          .filter(u => sendSelectedKeys.has(`${u.txid}:${u.vout}`))
          .map(u => ({ txid: u.txid, vout: u.vout }))
        txid = await api.walletSendWithInputs(walletName, sendAddr, parseFloat(sendAmt), inputs, changeAddr)
      } else {
        txid = await api.walletSend(walletName, sendAddr, parseFloat(sendAmt), sendComment || undefined, changeAddr)
      }
      toast(`Sent! txid: ${txid}`, 'success')
      setSendAddr(''); setSendAmt(''); setSendComment('')
      setSendManualInputs(false); setSendSelectedKeys(new Set()); setSendUtxos([])
      loadSummary(walletName)
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Send failed', 'error') }
  }

  const loadSendUtxos = useCallback(async () => {
    if (walletName === null) return
    setSendUtxosLoading(true)
    try {
      const list = await api.walletUTXOs(walletName)
      setSendUtxos(
        (Array.isArray(list) ? list : [])
          .sort((a: UTXOEntry, b: UTXOEntry) => b.amount - a.amount)
      )
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Failed to load UTXOs', 'error') }
    finally { setSendUtxosLoading(false) }
  }, [walletName])

  const handleSign = async () => {
    if (walletName === null || !signAddr || !signMsg) return
    clearMsg()
    try {
      const sig = await api.walletSignMessage(walletName, signAddr, signMsg)
      setSignResult(typeof sig === 'string' ? sig : JSON.stringify(sig))
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Sign failed', 'error') }
  }

  const handleVerify = async () => {
    if (!verifyAddr || !verifySig || !verifyMsg) return
    clearMsg(); setVerifyResult(null)
    try {
      const res = await api.verifyMessage(verifyAddr, verifySig, verifyMsg)
      setVerifyResult(res.valid)
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Verify failed', 'error') }
  }

  const handleUnlock = async () => {
    if (walletName === null || !unlockPw) return
    clearMsg()
    try {
      await api.walletUnlock(walletName, unlockPw, unlockDuration)
      toast(`Wallet unlocked for ${fmtCountdown(unlockDuration)}`, 'success')
      setUnlockPw('')
      setUnlockUntil(Date.now() + unlockDuration * 1000)
      loadSummary(walletName)
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Unlock failed', 'error') }
  }

  const handleLock = async () => {
    if (walletName === null) return
    clearMsg()
    try { await api.walletLock(walletName); setUnlockUntil(null); loadSummary(walletName) }
    catch (e: unknown) { toast(e instanceof Error ? e.message : 'Lock failed', 'error') }
  }

  const handleEncrypt = async () => {
    if (walletName === null || !encryptPw) return
    if (encryptPw !== encryptPw2) { toast('Passphrases do not match', 'error'); return }
    clearMsg()
    try {
      await api.walletEncrypt(walletName, encryptPw)
      toast('Wallet encrypted. radiantd will restart — please log in again.', 'warning')
      setEncryptPw(''); setEncryptPw2('')
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Encrypt failed', 'error') }
  }

  const handleBackup = async () => {
    if (walletName === null || backupBusy) return
    setBackupBusy(true)
    try {
      const blob = await api.walletBackupExport(walletName)
      const date = new Date().toISOString().slice(0, 10)
      const safeName = (walletName || 'wallet').replace(/[^a-zA-Z0-9_-]/g, '_')
      const filename = `${safeName}-backup-${date}.dat`

      if ('showSaveFilePicker' in window) {
        try {
          const handle = await (window as unknown as {
            showSaveFilePicker(opts: unknown): Promise<{ createWritable(): Promise<{ write(b: Blob): Promise<void>; close(): Promise<void> }> }>
          }).showSaveFilePicker({
            suggestedName: filename,
            types: [{ description: 'Wallet backup', accept: { 'application/octet-stream': ['.dat'] } }],
          })
          const writable = await handle.createWritable()
          await writable.write(blob)
          await writable.close()
          toast('Wallet backup saved', 'success')
          return
        } catch (e) {
          // User cancelled the picker — don't treat as error
          if (e instanceof Error && e.name === 'AbortError') return
          // Fall through to anchor fallback
        }
      }

      // Fallback: anchor download (no Save-As picker)
      const url = URL.createObjectURL(blob)
      const a = document.createElement('a')
      a.href = url; a.download = filename
      document.body.appendChild(a); a.click()
      document.body.removeChild(a)
      setTimeout(() => URL.revokeObjectURL(url), 10000)
      toast('Wallet backup downloaded', 'success')
    } catch (e: unknown) {
      toast(e instanceof Error ? e.message : 'Backup failed', 'error')
    } finally {
      setBackupBusy(false)
    }
  }

  const handleChangePassphrase = async () => {
    if (walletName === null || !oldPw || !newPw) return
    if (newPw !== newPw2) { toast('New passphrases do not match', 'error'); return }
    clearMsg()
    try {
      await api.walletChangePassphrase(walletName, oldPw, newPw)
      toast('Passphrase changed', 'success')
      setOldPw(''); setNewPw(''); setNewPw2('')
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Change passphrase failed', 'error') }
  }

  const handlePSBTAddOutput = () => {
    const amt = parseFloat(psbtAmount)
    if (!psbtAddr.trim() || isNaN(amt) || amt <= 0) return
    setPsbtOutputList(prev => [...prev, { address: psbtAddr.trim(), amount: amt }])
    setPsbtAddr('')
    setPsbtAmount('')
  }

  const handlePSBTCreate = async () => {
    if (walletName === null || psbtOutputList.length === 0) return
    clearMsg()
    try {
      const outputs = psbtOutputList.map(o => ({ [o.address]: o.amount }))
      const res = await api.walletPSBTCreate(walletName, [], outputs, psbtChangeAddr.trim() || undefined)
      setPsbtResult(typeof res === 'object' ? JSON.stringify(res, null, 2) : String(res))
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'PSBT create failed', 'error') }
  }

  const handlePSBTSign = async () => {
    if (walletName === null || !psbtInput.trim()) return
    clearMsg()
    try {
      const res = await api.walletPSBTSign(walletName, psbtInput.trim())
      const resultStr = typeof res === 'object' ? JSON.stringify(res, null, 2) : String(res)
      setPsbtSignResult(resultStr)
      if (typeof res === 'object' && (res as Record<string,unknown>).complete === false) {
        toast('PSBT signed but not complete — wallet may be locked or missing keys for some inputs', 'warning')
      }
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'PSBT sign failed', 'error') }
  }

  const handleConsScan = async () => {
    if (walletName === null) return
    setConsLog([])
    await loadUTXOs(walletName)
    // consScanned auto-updates via the utxos useEffect
  }

  const handleConsolidate = async () => {
    if (!consScanned || consScanned.length < 2 || !consDest || consRunning) return
    clearMsg()
    setConsRunning(true)
    const log: string[] = []
    const addLog = (s: string) => { log.push(s); setConsLog([...log]) }

    const filtered = consScanned
    const batches: UTXOEntry[][] = []
    for (let i = 0; i < filtered.length; i += consMaxBatch) {
      batches.push(filtered.slice(i, i + consMaxBatch))
      if (consMaxRuns > 0 && batches.length >= consMaxRuns) break
    }

    addLog(`Starting consolidation: ${filtered.length} UTXOs → ${batches.length} batch(es)`)

    for (let b = 0; b < batches.length; b++) {
      const batch = batches[b]
      const total = batch.reduce((s, u) => s + u.amount, 0)
      addLog(`Batch ${b + 1}/${batches.length}: ${batch.length} UTXOs, ${total.toFixed(8)} RXD`)
      try {
        const inputs = batch.map(u => ({ txid: u.txid, vout: u.vout }))
        // walletcreatefundedpsbt with subtractFeeFromOutputs so fee comes from the output
        const psbt = await api.rpc('walletcreatefundedpsbt', [
          inputs,
          [{ [consDest]: total }],
          0,
          { subtractFeeFromOutputs: [0] },
        ], walletName ?? undefined)
        if (psbt.error) throw new Error(String((psbt.error as Record<string,unknown>).message ?? psbt.error))
        const psbtStr = (psbt.result as Record<string,unknown>).psbt as string

        const signed = await api.rpc('walletprocesspsbt', [psbtStr], walletName ?? undefined)
        if (signed.error) throw new Error(String((signed.error as Record<string,unknown>).message ?? signed.error))
        const signedPsbt = (signed.result as Record<string,unknown>).psbt as string

        const finalized = await api.rpc('finalizepsbt', [signedPsbt])
        if (finalized.error) throw new Error(String((finalized.error as Record<string,unknown>).message ?? finalized.error))
        const finalRes = finalized.result as Record<string,unknown>
        if (!finalRes.complete) { addLog(`  ✗ Could not finalize (wallet locked?)`); break }

        const broadcast = await api.rpc('sendrawtransaction', [finalRes.hex])
        if (broadcast.error) throw new Error(String((broadcast.error as Record<string,unknown>).message ?? broadcast.error))
        addLog(`  ✓ txid: ${broadcast.result}`)
      } catch (e: unknown) {
        addLog(`  ✗ Error: ${e instanceof Error ? e.message : String(e)}`)
        break
      }
    }

    addLog('Done.')
    setConsRunning(false)
    if (walletName) { loadSummary(walletName); loadUTXOs(walletName) }
  }

  // ── Token send ──
  const handleSendToken = async () => {
    if (!tokenSendRef || !tokenSendAddr || walletName === null) return
    clearMsg()
    const meta = glyphMeta.get(tokenSendRef.ref)
    const decimals = meta?.decimals ?? 0
    const amtSats = tokenSendRef.isSingleton
      ? tokenSendRef.totalSats
      : Math.round(parseFloat(tokenSendAmt) * Math.pow(10, decimals))
    if (isNaN(amtSats) || amtSats <= 0) { toast('Invalid amount', 'error'); return }

    try {
      let recipientPKH: string
      try { recipientPKH = addressToPKH(tokenSendAddr) }
      catch { toast('Invalid recipient address', 'error'); return }

      // Unlock the wallet if a passphrase was supplied in the dialog.
      let didUnlock = false
      if (tokenSendPassphrase && walletName !== null) {
        await api.walletUnlock(walletName, tokenSendPassphrase, 60)
        didUnlock = true
      }

      // Query the node for the effective min relay fee rate (same as estimateMintFee).
      const mpInfo = await api.rpc('getmempoolinfo', [])
      if (mpInfo.error) throw new Error('Could not fetch fee rate from node')
      const mp = mpInfo.result as Record<string, unknown>
      const rawRate = mp.effective_minrelaytxfee ?? mp.minrelaytxfee
      const feeRatePerKb = typeof rawRate === 'number' ? Math.round(rawRate * 1e8) : 10_000_000

      // Collect token UTXOs to cover the amount
      const spendable = tokenSendRef.utxos.filter(u => u.spendable)
      const selected: typeof spendable = []
      let collected = 0
      for (const u of spendable) {
        selected.push(u); collected += u.photons
        if (collected >= amtSats) break
      }
      if (collected < amtSats) { toast('Insufficient token balance', 'error'); return }
      const change = collected - amtSats

      // Estimate transaction size to compute the required fee:
      //   base overhead: 10 bytes (version + locktime + input/output count varints)
      //   per input: 41 bytes (outpoint + sequence) + 108 bytes scriptSig (P2PKH {sig}{pubkey})
      //   per token output: 8 (value) + 1 varint + script bytes
      //   per P2PKH output: 34 bytes
      const tokenScriptLen = (tokenSendRef.utxos[0]?.scriptPubKey ?? '').length / 2
      const tokenOutputSize = 8 + (tokenScriptLen > 252 ? 3 : 1) + tokenScriptLen
      const numTokenOutputs = 1 + (change > 0 ? 1 : 0)
      const estBytes = 10 + (selected.length + 1) * 149 + numTokenOutputs * tokenOutputSize + 34
      const FEE_SATS = Math.max(1000, Math.ceil(estBytes * feeRatePerKb / 1000))

      // Find a non-glyph RXD UTXO for fees
      const rxdUTXO = utxos?.find(u => u.spendable && !isTokenBearing(u.scriptPubKey ?? '') && Math.round(u.amount * 1e8) >= FEE_SATS)
      if (!rxdUTXO) {
        const feeRxd = (FEE_SATS / 1e8).toFixed(8)
        toast(`No spendable RXD available for fees (need at least ${feeRxd} RXD)`, 'error')
        return
      }

      const senderPKH = addressToPKH(rxdUTXO.address)
      const rxdSats   = Math.round(rxdUTXO.amount * 1e8)

      // Preserve the exact on-chain script format by replacing only the PKH.
      // Photonic FT scripts start with 76a914 (P2PKH + covenant); simple d0/d8
      // scripts start with the ref opcode. Using the wrong format would produce
      // outputs with a different script hash that Photonic wallet can't find.
      const templateScript = selected[0].scriptPubKey
      const recipientScript = replaceGlyphScriptPKH(templateScript, recipientPKH)
      const senderScript    = replaceGlyphScriptPKH(templateScript, senderPKH)
      if (!recipientScript) { toast('Unknown token script format — cannot build output', 'error'); return }

      const inputs = [
        ...selected.map(u => ({ txid: u.txid, vout: u.vout })),
        { txid: rxdUTXO.txid, vout: rxdUTXO.vout },
      ]
      const outputs: Array<{ scriptHex: string; satoshis: number }> = [
        { scriptHex: recipientScript, satoshis: amtSats },
      ]
      if (change > 0 && senderScript) {
        outputs.push({ scriptHex: senderScript, satoshis: change })
      }
      const rxdChange = rxdSats - FEE_SATS
      if (rxdChange > 0) {
        outputs.push({ scriptHex: buildP2PKHScript(senderPKH), satoshis: rxdChange })
      }

      const rawTx = buildRawTx(inputs, outputs)

      const prevtxs = [
        ...selected.map(u => ({ txid: u.txid, vout: u.vout, scriptPubKey: u.scriptPubKey, amount: u.photons / 1e8 })),
        { txid: rxdUTXO.txid, vout: rxdUTXO.vout, scriptPubKey: rxdUTXO.scriptPubKey, amount: rxdUTXO.amount },
      ]

      const signed = await api.rpc('signrawtransactionwithwallet', [rawTx, prevtxs], walletName)
      if (didUnlock && walletName !== null) { await api.walletLock(walletName).catch(() => {}) }
      if (signed.error) throw new Error(String((signed.error as Record<string,unknown>).message ?? signed.error))
      const signedRes = signed.result as Record<string, unknown>
      if (!signedRes.complete) {
        const errs = signedRes.errors as Array<Record<string,unknown>> | undefined
        const detail = errs?.map(e => `[${e.txid}:${e.vout}] ${e.error}`).join('; ') ?? 'no detail'
        throw new Error(`Could not fully sign: ${detail}`)
      }

      const decoded = await api.rpc('decoderawtransaction', [signedRes.hex])
      if (decoded.error) throw new Error(String((decoded.error as Record<string,unknown>).message ?? decoded.error))
      const decodedTx = decoded.result as {
        txid: string
        vout: Array<{ value: number; n: number; scriptPubKey: { hex: string; type?: string; addresses?: string[]; address?: string } }>
      }

      setTokenSendPassphrase('')
      setTokenSendRef(null)
      setPendingTokenTx({
        signedHex: signedRes.hex as string,
        txid: decodedTx.txid,
        recipient: tokenSendAddr,
        fee: FEE_SATS,
        ticker: meta?.ticker ?? 'tokens',
        decimals,
        tokenAmtSats: amtSats,
      })
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Token send failed', 'error') }
  }

  const confirmBroadcastToken = async () => {
    if (!pendingTokenTx || walletName === null) return
    try {
      const broadcast = await api.rpc('sendrawtransaction', [pendingTokenTx.signedHex])
      if (broadcast.error) throw new Error(String((broadcast.error as Record<string,unknown>).message ?? broadcast.error))
      toast(`Token sent! txid: ${broadcast.result}`, 'success')
      setPendingTokenTx(null)
      setShowRawHex(false)
      setTokenSendAmt(''); setTokenSendAddr('')
      loadUTXOs(walletName)
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Broadcast failed', 'error') }
  }

  // ── Token mint ──

  // Spendable non-glyph UTXOs available to fund a mint, sorted largest first.
  const fundingUtxos = (utxos ?? [])
    .filter(u => u.spendable && !isTokenBearing(u.scriptPubKey ?? ''))
    .sort((a, b) => b.amount - a.amount)

  // Resolve the UTXO the user selected (or fall back to largest available).
  const selectedFundingUtxo = mintFundingKey
    ? fundingUtxos.find(u => `${u.txid}:${u.vout}` === mintFundingKey) ?? fundingUtxos[0]
    : fundingUtxos[0]

  // Returns fee info without broadcasting. Queries the node for the live min relay fee rate.
  const estimateMintFee = async (): Promise<{ feeSats: number; txBytes: number; feeRatePerKb: number } | null> => {
    if (!mintName) return null
    try {
      // Query node for the *effective* minimum relay fee (accounts for Radiant Core 2 upgrade).
      // effective_minrelaytxfee is set by GetEffectiveMinRelayFee() and reflects the actual
      // enforced floor (10,000,000 sat/kB after RC2 upgrade + grace period).
      const mpInfo = await api.rpc('getmempoolinfo', [])
      const mp = mpInfo.result as Record<string, unknown>
      const rawRate = mp.effective_minrelaytxfee ?? mp.minrelaytxfee
      // Use the effective rate exactly — Photonic's "Calculate fee" uses no additional multiplier.
      const feeRatePerKb = typeof rawRate === 'number' ? Math.round(rawRate * 1e8) : 10_000_000

      const decimals = 0
      const validAttrs = mintAttrs.filter(a => a.name.trim())
      const estPFlags = [mintType === 'ft' ? 1 : 2]
      if (!mintImmutable) estPFlags.push(5)
      if (mintType === 'container') estPFlags.push(7)
      const meta: GlyphMeta = {
        v: 2,
        p: estPFlags,
        name: mintName,
        ...(mintType === 'ft' && { ticker: mintTicker, decimals }),
        ...(mintType === 'user' && { type: 'user' }),
        ...(mintType === 'container' && { type: 'container' }),
        ...(mintDesc && { desc: mintDesc }),
        ...(mintLicense && { license: mintLicense }),
        ...(mintDataMode === 'file' && mintFileBytes && { main: { t: mintFileMime || 'application/octet-stream', b: mintFileBytes } }),
        ...(mintDataMode === 'url' && mintContentUrl && { main: { u: mintContentUrl } }),
        ...(mintDataMode === 'text' && mintText && { main: { t: 'text/plain', b: new TextEncoder().encode(mintText) } }),
        ...(validAttrs.length > 0 && { attrs: Object.fromEntries(validAttrs.map(a => [a.name.trim(), a.value.trim()])) }),
        ...(mintAuthor && (mintType === 'nft' || mintType === 'container') && { by: [hex2buf(mintAuthor)] }),
        ...(mintContainer && mintType === 'nft' && { in: [hex2buf(mintContainer)] }),
      }
      const opReturnScript = buildGlyphOpReturn(meta)
      const opReturnScriptBytes = opReturnScript.length / 2
      // Commit tx: 1-in 2-out P2PKH ≈ 226 bytes
      // Reveal tx: 1 P2PKH input + glyph output (72B) + OP_RETURN output (no change)
      const commitBytes = 226
      const opReturnLenVarint = opReturnScriptBytes > 252 ? 3 : 1
      const revealBytes = 10 + 149 + 72 + (8 + opReturnLenVarint + opReturnScriptBytes)
      const txBytes = commitBytes + revealBytes
      const feeSats = Math.ceil(commitBytes * feeRatePerKb / 1000) + Math.ceil(revealBytes * feeRatePerKb / 1000)
      return { feeSats, txBytes, feeRatePerKb }
    } catch { return null }
  }

  const handleEstimateFee = async () => {
    clearMsg()
    const est = await estimateMintFee()
    if (est) setMintFeeEstimate(est)
    else toast('Could not estimate fee — check node connection', 'error')
  }

  const handleMintToken = async () => {
    if (!mintName || walletName === null) return
    const isDmintFt = mintType === 'ft' && mintDeployMethod === 'dmint'
    if (mintType === 'ft' && !mintTicker) { toast('Ticker is required for FT', 'error'); return }
    if (mintType === 'ft' && !isDmintFt && !mintSupply) { toast('Supply is required for direct FT mint', 'error'); return }
    clearMsg()
    setMintRunning(true)
    try {
      const originUTXO = selectedFundingUtxo
      if (!originUTXO) { toast('No spendable RXD UTXO available to fund the mint', 'error'); setMintRunning(false); return }

      if (isDmintFt) {
        let dmintDataHex: string | null = null
        let dmintMimeType: string | null = null
        let dmintUrl: string | null = null
        if (mintDataMode === 'file' && mintFileBytes) {
          dmintDataHex = Array.from(mintFileBytes).map(b => b.toString(16).padStart(2, '0')).join('')
          dmintMimeType = mintFileMime || 'application/octet-stream'
        } else if (mintDataMode === 'text' && mintText) {
          const bytes = new TextEncoder().encode(mintText)
          dmintDataHex = Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('')
          dmintMimeType = 'text/plain'
        } else if (mintDataMode === 'url' && mintContentUrl) {
          dmintUrl = mintContentUrl
        }

        const mintRes = await api.rpc('mintdmint', [
          originUTXO.txid,
          originUTXO.vout,
          originUTXO.address,
          mintName,
          mintTicker,
          parseInt(dmintNumMints) || 1000,
          parseInt(dmintReward) || 1000,
          parseFloat(dmintDifficulty) || 10,
          dmintAlgorithm,
          dmintDaaMode,
          parseInt(dmintTargetBlockTime) || 60,
          parseInt(dmintHalfLife) || 240,
          parseInt(dmintNumContracts) || 1,
          parseInt(dmintPremine) || 0,
          mintDesc || null,
          dmintMimeType,
          dmintDataHex,
          dmintUrl,
        ], walletName)
        if (mintRes.error) throw new Error(String((mintRes.error as Record<string,unknown>).message ?? mintRes.error))

        const r = mintRes.result as Record<string, unknown>
        setMintPreviewTxs({
          commitHex:  String(r.commit_hex),
          revealHex:  String(r.reveal_hex),
          commitTxid: String(r.commit_txid),
          dmint: {
            ftRef:        String(r.ft_ref),
            contractRefs: (r.contract_refs as string[]) ?? [],
            nftRef:       String(r.nft_ref),
          },
        })
        toast('Transactions signed — review and confirm to broadcast.', 'info')
        return
      }

      const decimals = 0
      const supplyDisplayUnits = mintType === 'ft' ? parseFloat(mintSupply) : 1
      const supplySats = Math.round(supplyDisplayUnits * Math.pow(10, decimals))
      if (supplySats <= 0) { toast('Supply must be positive', 'error'); setMintRunning(false); return }

      const validAttrs = mintAttrs.filter(a => a.name.trim())
      const attrsObj = validAttrs.length > 0
        ? Object.fromEntries(validAttrs.map(a => [a.name.trim(), a.value.trim()]))
        : null

      let mainDataHex: string | null = null
      let mainMimeType: string | null = null
      let mainUrl: string | null = null
      if (mintDataMode === 'file' && mintFileBytes) {
        mainDataHex = Array.from(mintFileBytes).map(b => b.toString(16).padStart(2, '0')).join('')
        mainMimeType = mintFileMime || 'application/octet-stream'
      } else if (mintDataMode === 'text' && mintText) {
        const bytes = new TextEncoder().encode(mintText)
        mainDataHex = Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('')
        mainMimeType = 'text/plain'
      } else if (mintDataMode === 'url' && mintContentUrl) {
        mainUrl = mintContentUrl
      }

      // mintglyph RPC: CBOR encode + build + sign commit and reveal txs server-side.
      // Private keys never leave the node.
      const mintRes = await api.rpc('mintglyph', [
        originUTXO.txid,
        originUTXO.vout,
        originUTXO.address,
        mintType,
        mintName,
        supplySats,
        mintType === 'ft' ? mintTicker : null,
        mintType === 'ft' ? decimals : null,
        mintImmutable,
        mintDesc || null,
        mintLicense || null,
        (mintAuthor && (mintType === 'nft' || mintType === 'container')) ? mintAuthor : null,
        (mintContainer && mintType === 'nft') ? mintContainer : null,
        attrsObj,
        mainMimeType,
        mainDataHex,
        mainUrl,
      ], walletName)
      if (mintRes.error) throw new Error(String((mintRes.error as Record<string,unknown>).message ?? mintRes.error))

      const mintResult = mintRes.result as Record<string, unknown>
      setMintPreviewTxs({
        commitHex:  String(mintResult.commit_hex),
        revealHex:  String(mintResult.reveal_hex),
        commitTxid: String(mintResult.commit_txid),
      })
      toast('Transactions signed — review and confirm to broadcast.', 'info')
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Mint failed', 'error') }
    finally { setMintRunning(false) }
  }

  const handleBroadcastMint = async () => {
    if (!mintPreviewTxs || walletName === null) return
    clearMsg()
    setMintRunning(true)
    try {
      // Step 1: validate commit tx (its input is a confirmed UTXO — can be tested immediately).
      toast('Validating commit tx…', 'info')
      const testCommitRes = await api.rpc('testmempoolaccept', [[mintPreviewTxs.commitHex]])
      const testCommitResult = ((testCommitRes.result ?? []) as Array<{ allowed?: boolean; 'reject-reason'?: string }>)[0]
      if (!testCommitResult?.allowed) {
        throw new Error(`Commit tx rejected: ${testCommitResult?.['reject-reason'] ?? 'unknown reason'}`)
      }

      // Step 2: broadcast commit — creates the genesis outpoint in the mempool.
      toast('Sending commit tx…', 'info')
      const commitBroadcast = await api.rpc('sendrawtransaction', [mintPreviewTxs.commitHex])
      if (commitBroadcast.error) throw new Error(`Commit tx: ${String((commitBroadcast.error as Record<string,unknown>).message ?? commitBroadcast.error)}`)

      // Step 3: validate reveal tx now that the commit output exists in the mempool.
      toast('Validating reveal tx…', 'info')
      const testRevealRes = await api.rpc('testmempoolaccept', [[mintPreviewTxs.revealHex]])
      const testRevealResult = ((testRevealRes.result ?? []) as Array<{ allowed?: boolean; 'reject-reason'?: string }>)[0]
      if (!testRevealResult?.allowed) {
        // Commit is already in the mempool — funds are still recoverable (P2PKH output) but warn clearly.
        throw new Error(
          `Reveal tx rejected: ${testRevealResult?.['reject-reason'] ?? 'unknown reason'}. ` +
          `Commit tx ${mintPreviewTxs.commitTxid.slice(0, 16)}… is in the mempool — ` +
          `the commit output will be returned to your wallet once it confirms.`
        )
      }

      // Step 4: broadcast reveal — creates the actual glyph token output.
      toast('Sending reveal tx…', 'info')
      const revealBroadcast = await api.rpc('sendrawtransaction', [mintPreviewTxs.revealHex])
      if (revealBroadcast.error) throw new Error(`Reveal tx: ${String((revealBroadcast.error as Record<string,unknown>).message ?? revealBroadcast.error)}`)

      const revealTxid = String(revealBroadcast.result)
      setMintSuccess({ revealTxid, commitTxid: mintPreviewTxs.commitTxid, dmint: mintPreviewTxs.dmint })
      setMintPreviewTxs(null); setShowMintHex(false)
      setMintName(''); setMintTicker(''); setMintSupply(''); setMintDesc('')
      setMintLicense(''); setMintFileBytes(null); setMintFileMime(''); setMintFileName(''); setMintFilePreview(null)
      setMintContentUrl(''); setMintText(''); setMintAttrs([]); setMintImmutable(true); setMintDataMode('none')
      setMintAuthor(''); setMintContainer(''); setMintFundingKey('')
      setMintDeployMethod('direct')
      setDmintNumMints('1000'); setDmintReward('1000'); setDmintDifficulty('10')
      setDmintAlgorithm('blake3'); setDmintDaaMode('asert')
      setDmintTargetBlockTime('60'); setDmintHalfLife('240')
      setDmintNumContracts('1'); setDmintPremine('0')
      setMintSubTab('list')
      loadUTXOs(walletName)
    } catch (e: unknown) { toast(e instanceof Error ? e.message : 'Broadcast failed', 'error') }
    finally { setMintRunning(false) }
  }

  // ── Glyph token loading via listglyph RPC ──
  const loadGlyphTokens = useCallback(async (name: string) => {
    if (glyphLoadingRef.current) return
    glyphLoadingRef.current = true
    clearMsg()
    setGlyphLoading(true)
    try {
      const entries = await api.walletGlyph(name)
      setRpcGlyphEntries(Array.isArray(entries) ? entries : [])
    } catch (e: unknown) {
      toast(e instanceof Error ? e.message : 'Failed to load glyph tokens', 'error')
    } finally {
      setGlyphLoading(false)
      glyphLoadingRef.current = false
    }
  }, []) // stable ref — no state dep needed

  // Keep ref in sync for use inside callbacks that shouldn't re-run on every entry change
  useEffect(() => { rpcGlyphEntriesRef.current = rpcGlyphEntries }, [rpcGlyphEntries])

  // Fallback auto-load: covers the case where walletName resolves after the tab is already open.
  // switchTab handles the normal case; this catches the edge case of slow wallet detection.
  useEffect(() => {
    if (tab === 'tokens' && walletName !== null && rpcGlyphEntries === null && !glyphLoading) {
      loadGlyphTokens(walletName)
    }
  }, [tab, walletName, rpcGlyphEntries, glyphLoading, loadGlyphTokens])

  // Refresh from the topbar button — reloads data for the current tab.
  useEffect(() => {
    if (refreshKey <= 0 || walletName === null) return
    if (tab === 'overview')          { loadSummary(walletName); loadUTXOs(walletName) }
    else if (tab === 'transactions') loadTxs(walletName)
    else if (tab === 'utxos')        loadUTXOs(walletName)
    else if (tab === 'tokens')       loadGlyphTokens(walletName)
  }, [refreshKey]) // eslint-disable-line

  const filteredAddrs = addresses.filter(a =>
    !addrFilter ||
    a.address.toLowerCase().includes(addrFilter.toLowerCase()) ||
    a.label.toLowerCase().includes(addrFilter.toLowerCase())
  )

  const consFilteredCount = utxos
    ? utxos.filter(u => u.spendable && u.amount >= (parseFloat(consMin) || 0) && u.amount <= (parseFloat(consMax) || Infinity)).length
    : 0
  const consBatchCount = consMaxBatch > 0
    ? (consMaxRuns > 0 ? Math.min(consMaxRuns, Math.ceil(consFilteredCount / consMaxBatch)) : Math.ceil(consFilteredCount / consMaxBatch))
    : 0

  if (noWallet) {
    return (
      <div>
        <div className="wallet-tabs">
          {TABS.map(t => <button key={t.id} className={tab === t.id ? 'active' : ''} onClick={() => switchTab(t.id)}>{t.label}</button>)}
        </div>
        <div className="card">
          <p style={{ color: 'var(--text2)' }}>No wallet loaded. Start radiantd with <code>-wallet=&lt;name&gt;</code> to enable wallet features.</p>
        </div>
      </div>
    )
  }

  const isLocked   = summary?.encrypted && summary?.locked
  const isUnlocked = summary?.encrypted && !summary?.locked

  return (
    <div>
      {/* Mint confirm modal */}
      {mintPreviewTxs && (() => {
        const typeLabel = mintType === 'ft' && mintDeployMethod === 'dmint' ? 'dMint FT'
          : mintType === 'ft' ? 'FT' : mintType === 'nft' ? 'NFT'
          : mintType === 'user' ? 'User' : 'Container'
        const feeRXD = mintFeeEstimate ? mintFeeEstimate.feeSats / 1e8 : null
        const row = (label: string, value: React.ReactNode, mono = false) => (
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', padding: '0.7rem 0', borderBottom: '1px solid var(--border)' }}>
            <span style={{ color: 'var(--text2)', fontSize: '0.85rem', flexShrink: 0, marginRight: '1rem' }}>{label}</span>
            <span style={{ fontFamily: mono ? 'monospace' : undefined, fontSize: '0.85rem', textAlign: 'right', wordBreak: 'break-all' }}>{value}</span>
          </div>
        )
        return (
          <div style={{
            position: 'fixed', inset: 0, zIndex: 1001,
            background: 'rgba(0,0,0,0.80)', backdropFilter: 'blur(4px)',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
          }}>
            <div className="card" style={{ width: '100%', maxWidth: 440, padding: '1.75rem', border: '1px solid var(--border)', position: 'relative', maxHeight: '90vh', overflowY: 'auto' }}>
              <button onClick={() => { setMintPreviewTxs(null); setShowMintHex(false) }} style={{
                position: 'absolute', top: '1rem', right: '1rem',
                background: 'none', border: 'none', fontSize: '1.2rem',
                color: 'var(--text2)', cursor: 'pointer', padding: '0 0.25rem',
              }}>✕</button>

              <h2 style={{ margin: '0 0 1.25rem', fontSize: '1.1rem' }}>Confirm Mint</h2>

              {row('Token', <><strong>{mintName}</strong> <span className="badge" style={{ marginLeft: '0.4rem', fontSize: '0.68rem' }}>{typeLabel}</span></>)}
              {mintTicker && row('Ticker', mintTicker, true)}
              {mintType === 'ft' && mintDeployMethod === 'direct' && mintSupply && row('Supply', `${mintSupply} ${mintTicker}`)}
              {feeRXD !== null && row('Est. Fee', `${feeRXD.toFixed(8)} RXD`, true)}
              {row('Commit TxID',
                <span style={{ display: 'flex', alignItems: 'center', gap: '0.3rem' }}>
                  <code style={{ fontSize: '0.72rem' }}>{mintPreviewTxs.commitTxid.slice(0, 16)}…{mintPreviewTxs.commitTxid.slice(-8)}</code>
                  <button title="Copy" onClick={() => navigator.clipboard?.writeText(mintPreviewTxs.commitTxid)}
                    style={{ background: 'none', border: 'none', cursor: 'pointer', color: 'var(--text2)', padding: 0, lineHeight: 0 }}>
                    <CopyIcon size={13} />
                  </button>
                </span>
              )}
              {row('Commit Tx', `${(mintPreviewTxs.commitHex.length / 2).toLocaleString()} bytes`, true)}
              {row('Reveal Tx', `${(mintPreviewTxs.revealHex.length / 2).toLocaleString()} bytes`, true)}

              <div style={{ marginTop: '1.25rem', marginBottom: '1rem', padding: '0.75rem 1rem', background: 'rgba(245,158,11,0.12)', border: '1px solid rgba(245,158,11,0.4)', borderRadius: 'var(--radius)', fontSize: '0.82rem', color: '#f59e0b', display: 'flex', gap: '0.5rem', alignItems: 'flex-start' }}>
                <span>⚠</span>
                <span>Minting is irreversible. Verify all details before confirming.</span>
              </div>

              <div style={{ marginBottom: '1rem' }}>
                <button onClick={() => setShowMintHex(v => !v)}
                  style={{ background: 'none', border: 'none', color: 'var(--text2)', fontSize: '0.75rem', cursor: 'pointer', padding: 0, display: 'flex', alignItems: 'center', gap: '0.3rem' }}>
                  <span style={{ fontFamily: 'monospace', fontSize: '0.68rem' }}>{showMintHex ? '▼' : '▶'}</span>
                  Raw hex
                </button>
                {showMintHex && (
                  <div style={{ marginTop: '0.5rem', display: 'flex', flexDirection: 'column', gap: '0.5rem' }}>
                    {[['1 — Commit', mintPreviewTxs.commitHex], ['2 — Reveal', mintPreviewTxs.revealHex]].map(([label, hex]) => (
                      <div key={label}>
                        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.25rem' }}>
                          <span style={{ fontSize: '0.72rem', color: 'var(--text2)', fontWeight: 600 }}>{label}</span>
                          <button onClick={() => navigator.clipboard?.writeText(hex)} title="Copy hex"
                            style={{ background: 'none', border: 'none', cursor: 'pointer', color: 'var(--text2)', padding: 0, lineHeight: 0 }}>
                            <CopyIcon size={13} />
                          </button>
                        </div>
                        <code style={{ display: 'block', background: 'var(--bg3)', borderRadius: 'var(--radius)', padding: '0.5rem 0.65rem', fontSize: '0.65rem', wordBreak: 'break-all', color: 'var(--text2)', lineHeight: 1.5 }}>{hex}</code>
                      </div>
                    ))}
                  </div>
                )}
              </div>

              <div style={{ display: 'flex', gap: '0.5rem' }}>
                <button className="primary" style={{ flex: 1 }} onClick={handleBroadcastMint} disabled={mintRunning}>
                  {mintRunning ? 'Working…' : 'Confirm & Mint'}
                </button>
                <button onClick={() => { setMintPreviewTxs(null); setShowMintHex(false) }} disabled={mintRunning}>Cancel</button>
              </div>
            </div>
          </div>
        )
      })()}

      {/* Mint success modal */}
      {mintSuccess && (() => {
        const txRow = (label: string, txid: string) => (
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '0.7rem 0', borderBottom: '1px solid var(--border)' }}>
            <span style={{ color: 'var(--text2)', fontSize: '0.85rem', flexShrink: 0, marginRight: '1rem' }}>{label}</span>
            <span style={{ display: 'flex', alignItems: 'center', gap: '0.3rem' }}>
              <code style={{ fontSize: '0.72rem' }}>{txid.slice(0, 16)}…{txid.slice(-8)}</code>
              <button title="Copy" onClick={() => navigator.clipboard?.writeText(txid)}
                style={{ background: 'none', border: 'none', cursor: 'pointer', color: 'var(--text2)', padding: 0, lineHeight: 0 }}>
                <CopyIcon size={13} />
              </button>
            </span>
          </div>
        )
        return (
          <div style={{
            position: 'fixed', inset: 0, zIndex: 1000,
            background: 'rgba(0,0,0,0.80)', backdropFilter: 'blur(4px)',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
          }}>
            <div className="card" style={{ width: '100%', maxWidth: 440, padding: '1.75rem', border: '1px solid var(--border)', position: 'relative' }}>
              <button onClick={() => setMintSuccess(null)} style={{
                position: 'absolute', top: '1rem', right: '1rem',
                background: 'none', border: 'none', fontSize: '1.2rem',
                color: 'var(--text2)', cursor: 'pointer', padding: '0 0.25rem',
              }}>✕</button>

              <h2 style={{ margin: '0 0 1.25rem', fontSize: '1.1rem' }}>Token Minted</h2>

              {txRow('Token TxID', mintSuccess.revealTxid)}
              {txRow('Commit TxID', mintSuccess.commitTxid)}

              {mintSuccess.dmint && (
                <>
                  {txRow('FT Token Ref', mintSuccess.dmint.ftRef)}
                  {mintSuccess.dmint.contractRefs.map((ref, i) =>
                    txRow(mintSuccess.dmint!.contractRefs.length > 1 ? `Contract Ref ${i + 1}` : 'Contract Ref', ref)
                  )}
                </>
              )}

              <div style={{ marginTop: '1.25rem', marginBottom: '1rem', padding: '0.75rem 1rem', background: 'rgba(16,185,129,0.12)', border: '1px solid rgba(16,185,129,0.4)', borderRadius: 'var(--radius)', fontSize: '0.82rem', color: '#10b981', display: 'flex', gap: '0.5rem', alignItems: 'flex-start' }}>
                <span>✓</span>
                <span>Token successfully minted and broadcast to the network.</span>
              </div>

              <div style={{ display: 'flex', gap: '0.5rem' }}>
                <button className="primary" style={{ flex: 1 }}
                  onClick={() => window.open(explorerTxUrl(mintSuccess.revealTxid), '_blank', 'noopener,noreferrer')}>
                  View on Explorer ↗
                </button>
                <button onClick={() => setMintSuccess(null)}>Close</button>
              </div>
            </div>
          </div>
        )
      })()}

      {/* Send token modal */}
      {tokenSendRef && (() => {
        const meta = glyphMeta.get(tokenSendRef.ref)
        const decimals = meta?.decimals ?? 0
        const ticker = meta?.ticker ?? 'tokens'
        const typeLabel = tokenTypeInfo(tokenSendRef.isSingleton, meta).label
        return (
          <div style={{
            position: 'fixed', inset: 0, zIndex: 1000,
            background: 'rgba(0,0,0,0.80)', backdropFilter: 'blur(4px)',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
          }}>
            <div className="card" style={{ width: '100%', maxWidth: 440, padding: '1.75rem', border: '1px solid var(--border)', position: 'relative' }}>
              <button onClick={() => { setTokenSendRef(null); setTokenSendPassphrase('') }} style={{
                position: 'absolute', top: '1rem', right: '1rem',
                background: 'none', border: 'none', fontSize: '1.2rem',
                color: 'var(--text2)', cursor: 'pointer', padding: '0 0.25rem',
              }}>✕</button>

              <h2 style={{ margin: '0 0 0.25rem', fontSize: '1.1rem' }}>
                Send {meta?.name ?? tokenSendRef.ref.slice(0, 12) + '…'}
              </h2>
              <p style={{ margin: '0 0 1.25rem', fontSize: '0.78rem', color: 'var(--text2)' }}>
                {typeLabel} · Available: <span style={{ color: 'var(--text)', fontFamily: 'monospace' }}>{maskAmt(displayBalance(tokenSendRef.totalSats, decimals))} {ticker}</span>
                {' '}· {tokenSendRef.utxos.filter(u => u.spendable).length} spendable UTXO(s)
              </p>

              {!tokenSendRef.isSingleton && (
                <div className="form-group" style={{ marginBottom: '0.75rem' }}>
                  <label>Amount ({ticker})</label>
                  <input type="number" value={tokenSendAmt} onChange={e => setTokenSendAmt(e.target.value)}
                    placeholder={`0.${'0'.repeat(decimals)}`} step={Math.pow(10, -decimals)} min="0" />
                </div>
              )}

              <div className="form-group" style={{ marginBottom: '1rem' }}>
                <label>Recipient address</label>
                <input value={tokenSendAddr} onChange={e => setTokenSendAddr(e.target.value)}
                  placeholder="1A1zP1eP5QGefi2…" list="wallet-addr-list-tok" autoFocus />
                <datalist id="wallet-addr-list-tok">
                  {addresses.map(a => <option key={a.address} value={a.address}>{a.label}</option>)}
                </datalist>
              </div>

              <p style={{ color: 'var(--text2)', fontSize: '0.75rem', marginBottom: '1.25rem' }}>
                A small RXD fee will be deducted from a non-token UTXO.
              </p>

              {isLocked && (
                <div className="form-group" style={{ marginBottom: '1rem' }}>
                  <label>Wallet passphrase (required to sign)</label>
                  <input type="password" value={tokenSendPassphrase}
                    onChange={e => setTokenSendPassphrase(e.target.value)}
                    placeholder="Enter passphrase…"
                    onKeyDown={e => { if (e.key === 'Enter' && tokenSendAddr) handleSendToken() }} />
                </div>
              )}

              <div style={{ display: 'flex', gap: '0.5rem' }}>
                <button className="primary" style={{ flex: 1 }}
                  onClick={handleSendToken}
                  disabled={!tokenSendAddr || (!tokenSendRef.isSingleton && !tokenSendAmt) || (!!isLocked && !tokenSendPassphrase)}>
                  Send
                </button>
                <button onClick={() => { setTokenSendRef(null); setTokenSendPassphrase('') }}>Cancel</button>
              </div>
            </div>
          </div>
        )
      })()}

      {/* Token send confirm modal */}
      {pendingTokenTx && (() => {
        const feeRXD = pendingTokenTx.fee / 1e8
        const tokenDisplay = displayBalance(pendingTokenTx.tokenAmtSats, pendingTokenTx.decimals)
        const row = (label: string, value: React.ReactNode, mono = false) => (
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', padding: '0.7rem 0', borderBottom: '1px solid var(--border)' }}>
            <span style={{ color: 'var(--text2)', fontSize: '0.85rem', flexShrink: 0, marginRight: '1rem' }}>{label}</span>
            <span style={{ fontFamily: mono ? 'monospace' : undefined, fontSize: '0.85rem', textAlign: 'right', wordBreak: 'break-all' }}>{value}</span>
          </div>
        )
        return (
          <div style={{
            position: 'fixed', inset: 0, zIndex: 1001,
            background: 'rgba(0,0,0,0.80)', backdropFilter: 'blur(4px)',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
          }}>
            <div className="card" style={{ width: '100%', maxWidth: 440, padding: '1.75rem', border: '1px solid var(--border)', position: 'relative' }}>
              <button onClick={() => { setPendingTokenTx(null); setShowRawHex(false) }} style={{
                position: 'absolute', top: '1rem', right: '1rem',
                background: 'none', border: 'none', fontSize: '1.2rem',
                color: 'var(--text2)', cursor: 'pointer', padding: '0 0.25rem',
              }}>✕</button>

              <h2 style={{ margin: '0 0 1.25rem', fontSize: '1.1rem' }}>Confirm Transaction</h2>

              {row('Recipient', pendingTokenTx.recipient, true)}
              {row('Token Amount', <strong>{maskAmt(tokenDisplay)} {pendingTokenTx.ticker}</strong>)}
              {row('Fee', `${feeRXD.toFixed(8)} RXD`, true)}
              {row('Total Cost', `${feeRXD.toFixed(8)} RXD`, true)}
              {row('TxID',
                <span style={{ display: 'flex', alignItems: 'center', gap: '0.3rem' }}>
                  <code style={{ fontSize: '0.72rem' }}>{pendingTokenTx.txid.slice(0, 16)}…{pendingTokenTx.txid.slice(-8)}</code>
                  <button title="Copy" onClick={() => navigator.clipboard?.writeText(pendingTokenTx.txid)}
                    style={{ background: 'none', border: 'none', cursor: 'pointer', color: 'var(--text2)', padding: 0, lineHeight: 0 }}>
                    <CopyIcon size={13} />
                  </button>
                </span>
              )}

              <div style={{ marginTop: '1.25rem', marginBottom: '1rem', padding: '0.75rem 1rem', background: 'rgba(245,158,11,0.12)', border: '1px solid rgba(245,158,11,0.4)', borderRadius: 'var(--radius)', fontSize: '0.82rem', color: '#f59e0b', display: 'flex', gap: '0.5rem', alignItems: 'flex-start' }}>
                <span>⚠</span>
                <span>Please verify the recipient address and amount before confirming.</span>
              </div>

              <div style={{ marginBottom: '1rem' }}>
                <button onClick={() => setShowRawHex(v => !v)}
                  style={{ background: 'none', border: 'none', color: 'var(--text2)', fontSize: '0.75rem', cursor: 'pointer', padding: 0, display: 'flex', alignItems: 'center', gap: '0.3rem' }}>
                  <span style={{ fontFamily: 'monospace', fontSize: '0.68rem' }}>{showRawHex ? '▼' : '▶'}</span>
                  Raw hex
                </button>
                {showRawHex && (
                  <div style={{ marginTop: '0.4rem', background: 'var(--bg3)', borderRadius: 'var(--radius)', padding: '0.5rem 0.65rem' }}>
                    <code style={{ fontSize: '0.65rem', wordBreak: 'break-all', color: 'var(--text2)', lineHeight: 1.5 }}>{pendingTokenTx.signedHex}</code>
                  </div>
                )}
              </div>

              <div style={{ display: 'flex', gap: '0.5rem' }}>
                <button className="primary" style={{ flex: 1 }} onClick={confirmBroadcastToken}>Confirm &amp; Send</button>
                <button onClick={() => { setPendingTokenTx(null); setShowRawHex(false) }}>Cancel</button>
              </div>
            </div>
          </div>
        )
      })()}

      {/* Tab bar */}
      <div className="wallet-tabs">
        {TABS.map(t => (
          <button key={t.id} className={tab === t.id ? 'active' : ''} onClick={() => switchTab(t.id)}>
            {t.label}
            {t.id === 'security' && summary && !summary.encrypted && (
              <span title="Wallet is not encrypted" style={{
                display: 'inline-block', marginLeft: '0.35rem',
                width: '7px', height: '7px', borderRadius: '50%',
                background: '#f59e0b', verticalAlign: 'middle', flexShrink: 0,
              }} />
            )}
          </button>
        ))}
      </div>

      {/* ── Lock / Unlock banner (hidden on Security tab — that tab has its own full UI) ── */}
      {summary?.encrypted && tab !== 'security' && (
        <div className="lock-banner">
          {isLocked ? (
            <>
              <span className="badge red" style={{ flexShrink: 0 }}>Locked</span>
              <input
                type="password"
                value={bannerPw}
                onChange={e => setBannerPw(e.target.value)}
                onKeyDown={e => e.key === 'Enter' && handleBannerUnlock()}
                placeholder="Passphrase…"
                className="lock-banner-input"
              />
              <select
                value={unlockDuration}
                onChange={e => setUnlockDuration(Number(e.target.value))}
                className="lock-banner-select"
              >
                {UNLOCK_DURATIONS.map(d => (
                  <option key={d.seconds} value={d.seconds}>{d.label}</option>
                ))}
              </select>
              <button className="primary" onClick={handleBannerUnlock} disabled={!bannerPw}
                style={{ flexShrink: 0, padding: '0.3rem 0.9rem' }}>
                Unlock
              </button>
            </>
          ) : (
            <>
              <span className="badge green" style={{ flexShrink: 0 }}>Unlocked</span>
              {unlockUntil && (
                <span style={{ color: 'var(--text2)', fontSize: '0.82rem' }}>
                  locks in {fmtCountdown(secondsLeft)}
                </span>
              )}
              <button onClick={handleBannerLock} style={{ marginLeft: 'auto', flexShrink: 0, padding: '0.25rem 0.75rem', fontSize: '0.8rem' }}>
                Lock now
              </button>
            </>
          )}
        </div>
      )}


      {/* ── OVERVIEW ── */}
      {tab === 'overview' && (
        <>
          {summary && (
            <div className="card" style={{ marginBottom: '1rem' }}>
              <div style={{ display: 'grid', gridTemplateColumns: '2fr 1fr 1fr', gap: '0.75rem' }}>
                <div className="stat">
                  <span className="stat-label">Confirmed balance</span>
                  <span className="stat-value" style={{ whiteSpace: 'nowrap' }}>{maskAmt(fmtRXD(summary.balance))} <span style={{ color: 'var(--text2)', fontWeight: 400, fontSize: '0.85rem' }}>RXD</span></span>
                </div>
                <div className="stat">
                  <span className="stat-label">Unconfirmed</span>
                  <span className="stat-value" style={{ whiteSpace: 'nowrap' }}>{maskAmt(fmtRXD(summary.unconfirmed))} <span style={{ color: 'var(--text2)', fontWeight: 400, fontSize: '0.85rem' }}>RXD</span></span>
                </div>
                <div className="stat">
                  <span className="stat-label">Immature</span>
                  <span className="stat-value" style={{ whiteSpace: 'nowrap' }}>{maskAmt(fmtRXD(summary.immature))} <span style={{ color: 'var(--text2)', fontWeight: 400, fontSize: '0.85rem' }}>RXD</span></span>
                </div>
              </div>
            </div>
          )}

          <div className="card">
            <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', marginBottom: '0.75rem', flexWrap: 'wrap' }}>
              <span style={{ fontWeight: 600, fontSize: '0.85rem', color: 'var(--text2)', textTransform: 'uppercase', letterSpacing: '0.06em', whiteSpace: 'nowrap' }}>
                Receiving addresses ({addresses.length})
              </span>
              <input
                value={addrFilter}
                onChange={e => setAddrFilter(e.target.value)}
                placeholder="Filter by address or label…"
                style={{ flex: '1 1 160px', minWidth: '0', fontSize: '0.82rem' }}
              />
              <button title="Refresh"
                onClick={() => walletName !== null && (loadSummary(walletName), loadAddresses(walletName), loadUTXOs(walletName))}
                style={{ padding: '0.35rem 0.6rem', fontSize: '1rem', flexShrink: 0 }}>↺</button>
              <input
                value={newLabel}
                onChange={e => setNewLabel(e.target.value)}
                onKeyDown={e => e.key === 'Enter' && handleNewAddress()}
                placeholder="Label (optional)"
                style={{ width: '130px', fontSize: '0.82rem', flexShrink: 0 }}
              />
              <button className="primary" onClick={handleNewAddress} style={{ flexShrink: 0 }}>+ New address</button>
            </div>
            <table className="table">
              <thead>
                <tr>
                  <th>Address</th>
                  <th>Label <span style={{ color: 'var(--text2)', fontWeight: 400, fontSize: '0.72rem' }}>(click to edit)</span></th>
                  <th style={{ textAlign: 'right' }}>Balance</th>
                  <th style={{ textAlign: 'right' }}>Txns</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                {filteredAddrs.map(a => (
                  <tr key={a.address}>
                    <td>
                      <a href={explorerAddressUrl(a.address)} target="_blank" rel="noopener noreferrer" style={{ textDecoration: 'none' }}>
                        <code style={{ fontSize: '0.77rem', color: 'var(--accent)' }}>{a.address}</code>
                      </a>
                    </td>
                    <td onClick={() => editingAddr !== a.address && startEditLabel(a)}
                      style={{ cursor: editingAddr === a.address ? 'default' : 'text', minWidth: '100px' }}>
                      {editingAddr === a.address ? (
                        <input ref={labelInputRef} value={editLabelVal}
                          onChange={e => setEditLabelVal(e.target.value)}
                          onBlur={commitLabel}
                          onKeyDown={e => {
                            if (e.key === 'Enter') { e.preventDefault(); commitLabel() }
                            if (e.key === 'Escape') { e.preventDefault(); setEditingAddr(null) }
                          }}
                          style={{ width: '100%', padding: '2px 4px', fontSize: '0.82rem' }}
                          onClick={e => e.stopPropagation()}
                        />
                      ) : (
                        <span style={{ color: a.label ? 'var(--text)' : 'var(--text2)', fontSize: '0.85rem' }}>
                          {a.label || <em>no label</em>}
                        </span>
                      )}
                    </td>
                    <td style={{ textAlign: 'right', fontFamily: 'monospace', fontSize: '0.82rem' }}>
                      {(() => { const b = addrBalance.get(a.address); return b ? maskAmt(b.toFixed(8)) : '—' })()}
                    </td>
                    <td style={{ textAlign: 'right' }}>
                      {(a.txids?.length ?? 0) > 0 ? (
                        <button
                          onClick={() => setTxModal(a)}
                          style={{ background: 'none', border: 'none', cursor: 'pointer', color: 'var(--accent)', fontFamily: 'monospace', fontSize: '0.82rem', padding: '2px 4px' }}
                          title="Show transactions"
                        >
                          {a.txids!.length}
                        </button>
                      ) : (
                        <span style={{ color: 'var(--text2)', fontSize: '0.82rem' }}>—</span>
                      )}
                    </td>
                    <td style={{ textAlign: 'right', whiteSpace: 'nowrap' }}>
                      <button onClick={() => setQrAddr(a.address)} style={{ padding: '2px 6px', marginRight: '0.35rem', lineHeight: 0 }} title="Show QR code">
                        <svg width="14" height="14" viewBox="0 0 512 512" fill="currentColor" xmlns="http://www.w3.org/2000/svg">
                          <rect x="336" y="336" width="80" height="80" rx="8" ry="8"/>
                          <rect x="272" y="272" width="64" height="64" rx="8" ry="8"/>
                          <rect x="416" y="416" width="64" height="64" rx="8" ry="8"/>
                          <rect x="432" y="272" width="48" height="48" rx="8" ry="8"/>
                          <rect x="272" y="432" width="48" height="48" rx="8" ry="8"/>
                          <rect x="336" y="96" width="80" height="80" rx="8" ry="8"/>
                          <rect x="288" y="48" width="176" height="176" rx="16" ry="16" fill="none" stroke="currentColor" strokeWidth="32" strokeLinecap="round" strokeLinejoin="round"/>
                          <rect x="96" y="96" width="80" height="80" rx="8" ry="8"/>
                          <rect x="48" y="48" width="176" height="176" rx="16" ry="16" fill="none" stroke="currentColor" strokeWidth="32" strokeLinecap="round" strokeLinejoin="round"/>
                          <rect x="96" y="336" width="80" height="80" rx="8" ry="8"/>
                          <rect x="48" y="288" width="176" height="176" rx="16" ry="16" fill="none" stroke="currentColor" strokeWidth="32" strokeLinecap="round" strokeLinejoin="round"/>
                        </svg>
                      </button>
                      <button onClick={() => handleCopy(a.address)} style={{ padding: '2px 6px', lineHeight: 0, color: copied === a.address ? '#22c55e' : undefined }} title={copied === a.address ? 'Copied!' : 'Copy address'}>
                        {copied === a.address ? <CopiedIcon /> : <CopyIcon />}
                      </button>
                    </td>
                  </tr>
                ))}
                {filteredAddrs.length === 0 && (
                  <tr><td colSpan={5} style={{ color: 'var(--text2)', textAlign: 'center', padding: '1.5rem' }}>
                    {addrFilter ? 'No addresses match filter' : 'No addresses yet — generate one above'}
                  </td></tr>
                )}
              </tbody>
            </table>
          </div>
        </>
      )}

      {/* ── TRANSACTIONS ── */}
      {tab === 'transactions' && (() => {
        const DUST_THRESHOLD = 0.001
        const filteredTxs = txHideDust
          ? txs.filter(tx => Math.abs(Number(tx.amount)) >= DUST_THRESHOLD)
          : txs
        const dustCount = txs.length - filteredTxs.length
        const TX_PER_PAGE = 25
        const totalPages = Math.max(1, Math.ceil(filteredTxs.length / TX_PER_PAGE))
        const page = Math.min(txPage, totalPages - 1)
        const pageStart = page * TX_PER_PAGE
        const pageTxs = filteredTxs.slice(pageStart, pageStart + TX_PER_PAGE)
        return (
          <div className="card">
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.75rem', flexWrap: 'wrap', gap: '0.5rem' }}>
              <h2 style={{ margin: 0 }}>Transactions ({filteredTxs.length}{dustCount > 0 && txHideDust ? ` of ${txs.length}` : ''})</h2>
              <div style={{ display: 'flex', alignItems: 'center', gap: '1rem', flexShrink: 0 }}>
                <label style={{ display: 'flex', alignItems: 'center', gap: '0.4rem', fontSize: '0.82rem', cursor: 'pointer', userSelect: 'none', color: 'var(--text2)', whiteSpace: 'nowrap' }}>
                  <input type="checkbox" checked={txHideDust}
                    onChange={e => { setTxHideDust(e.target.checked); setTxPage(0) }} />
                  {`Hide dust${dustCount > 0 ? ` (${dustCount} hidden)` : ''}`}
                </label>
                <button onClick={() => walletName !== null && loadTxs(walletName)} style={{ fontSize: '0.8rem' }}>↺ Refresh</button>
              </div>
            </div>
            {filteredTxs.length === 0 && txs.length === 0 && <p style={{ color: 'var(--text2)' }}>No transactions</p>}
            {filteredTxs.length === 0 && txs.length > 0 && <p style={{ color: 'var(--text2)' }}>All {txs.length} transactions are below the dust threshold ({"<"} {DUST_THRESHOLD} RXD).</p>}
            {filteredTxs.length > 0 && (
              <>
                <table className="table">
                  <thead><tr><th>Type</th><th>Amount</th><th>Label</th><th>Conf.</th><th>Time</th><th>txid</th></tr></thead>
                  <tbody>
                    {pageTxs.map((tx, i) => (
                      <tr key={pageStart + i}>
                        <td><span className={`badge ${String(tx.category) === 'send' ? 'red' : 'green'}`}>{String(tx.category)}</span></td>
                        <td style={{ fontFamily: 'monospace', fontSize: '0.82rem' }}>{maskAmt(Number(tx.amount).toFixed(8))}</td>
                        <td style={{ color: 'var(--text2)', fontSize: '0.82rem' }}>{String(tx.label ?? tx.account ?? '')}</td>
                        <td style={{ color: 'var(--text2)', fontSize: '0.82rem' }}>{String(tx.confirmations)}</td>
                        <td style={{ color: 'var(--text2)', fontSize: '0.8rem' }}>
                          {(() => { const ts = Number(tx.blocktime ?? tx.time ?? 0); return ts ? new Date(ts * 1000).toLocaleString() : '—' })()}
                        </td>
                        <td>
                          <a href={explorerTxUrl(String(tx.txid))} target="_blank" rel="noopener noreferrer" style={{ textDecoration: 'none' }}>
                            <code style={{ fontSize: '0.75rem', color: 'var(--accent)' }} title={String(tx.txid)}>
                              {String(tx.txid).slice(0, 16)}…
                            </code>
                          </a>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
                {totalPages > 1 && (
                  <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', marginTop: '0.75rem', justifyContent: 'flex-end', fontSize: '0.82rem' }}>
                    <span style={{ color: 'var(--text2)', marginRight: '0.25rem' }}>
                      {pageStart + 1}–{Math.min(pageStart + TX_PER_PAGE, filteredTxs.length)} of {filteredTxs.length}
                    </span>
                    <button onClick={() => setTxPage(0)}           disabled={page === 0}              style={{ padding: '0.2rem 0.5rem' }}>«</button>
                    <button onClick={() => setTxPage(p => p - 1)}  disabled={page === 0}              style={{ padding: '0.2rem 0.5rem' }}>‹</button>
                    <span style={{ color: 'var(--text2)' }}>Page {page + 1} / {totalPages}</span>
                    <button onClick={() => setTxPage(p => p + 1)}  disabled={page >= totalPages - 1}  style={{ padding: '0.2rem 0.5rem' }}>›</button>
                    <button onClick={() => setTxPage(totalPages - 1)} disabled={page >= totalPages - 1} style={{ padding: '0.2rem 0.5rem' }}>»</button>
                  </div>
                )}
              </>
            )}
          </div>
        )
      })()}

      {/* ── UTXOs ── */}
      {tab === 'utxos' && (() => {
        const displayCoins = showSpent
        const rows = displayCoins ? (coins ?? []) : (utxos ?? [])
        const isLoading = displayCoins ? coinsLoading : utxos === null
        const spentCount = displayCoins && coins ? coins.filter(c => c.is_spent).length : 0
        return (
          <div className="card">
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.75rem', flexWrap: 'wrap', gap: '0.5rem' }}>
              <h2 style={{ margin: 0 }}>
                {displayCoins ? `Outputs (${rows.length})` : `Unspent outputs${utxos !== null ? ` (${utxos.length})` : ''}`}
                {displayCoins && spentCount > 0 && (
                  <span style={{ fontSize: '0.75rem', fontWeight: 400, color: 'var(--text2)', marginLeft: '0.5rem' }}>{spentCount} spent</span>
                )}
              </h2>
              <div style={{ display: 'flex', alignItems: 'center', gap: '0.75rem' }}>
                <label style={{ display: 'flex', alignItems: 'center', gap: '0.35rem', fontSize: '0.82rem', cursor: 'pointer', whiteSpace: 'nowrap' }}>
                  <input type="checkbox" checked={showSpent}
                    onChange={e => {
                      const checked = e.target.checked
                      setShowSpent(checked)
                      if (checked && walletName !== null && coins === null)
                        loadCoins(walletName, true)
                    }} />
                  Show spent
                </label>
                <button style={{ fontSize: '0.8rem' }} onClick={() => {
                  if (walletName === null) return
                  if (showSpent) loadCoins(walletName, true)
                  else loadUTXOs(walletName)
                }}>↺ Refresh</button>
              </div>
            </div>
            {isLoading && <p style={{ color: 'var(--text2)' }}>Loading…</p>}
            {!isLoading && rows.length === 0 && <p style={{ color: 'var(--text2)' }}>No outputs found</p>}
            {!isLoading && rows.length > 0 && (
              <table className="table">
                <thead>
                  <tr>
                    {displayCoins && <th>Status</th>}
                    <th>Address</th>
                    {!displayCoins && <th>Label</th>}
                    <th style={{ textAlign: 'right' }}>Amount (RXD)</th>
                    <th style={{ textAlign: 'right' }}>Conf.</th>
                    <th>{displayCoins ? 'TXID / Spent by' : 'txid : vout'}</th>
                  </tr>
                </thead>
                <tbody>
                  {displayCoins
                    ? (rows as CoinEntry[]).map((c, i) => (
                      <tr key={i} style={{ opacity: c.is_spent ? 0.7 : 1 }}>
                        <td style={{ whiteSpace: 'nowrap' }}>
                          {c.is_spent
                            ? <span className="badge red" style={{ fontSize: '0.72rem' }}>Spent</span>
                            : <span className="badge green" style={{ fontSize: '0.72rem' }}>Unspent</span>}
                        </td>
                        <td>
                          <a href={explorerAddressUrl(c.address)} target="_blank" rel="noopener noreferrer" style={{ textDecoration: 'none' }}>
                            <code style={{ fontSize: '0.77rem', color: 'var(--accent)' }}>{c.address}</code>
                          </a>
                        </td>
                        <td style={{ textAlign: 'right', fontFamily: 'monospace', fontSize: '0.82rem' }}>{maskAmt(c.amount.toFixed(8))}</td>
                        <td style={{ textAlign: 'right', color: 'var(--text2)', fontSize: '0.82rem' }}>{c.is_spent ? '—' : c.confirmations}</td>
                        <td>
                          <a href={explorerTxUrl(c.txid)} target="_blank" rel="noopener noreferrer" style={{ textDecoration: 'none' }}>
                            <code style={{ fontSize: '0.75rem', color: 'var(--accent)' }} title={c.txid}>
                              {c.txid.slice(0, 12)}…:{c.vout}
                            </code>
                          </a>
                          {c.is_spent && c.spent_by && (
                            <div style={{ fontSize: '0.7rem', color: 'var(--text2)', marginTop: '2px' }}>
                              spent by{' '}
                              <a href={explorerTxUrl(c.spent_by)} target="_blank" rel="noopener noreferrer" style={{ textDecoration: 'none' }}>
                                <code style={{ color: 'var(--accent)' }}>{c.spent_by.slice(0, 12)}…</code>
                              </a>
                            </div>
                          )}
                        </td>
                      </tr>
                    ))
                    : (rows as UTXOEntry[]).map((u, i) => (
                      <tr key={i}>
                        <td>
                          <a href={explorerAddressUrl(u.address)} target="_blank" rel="noopener noreferrer" style={{ textDecoration: 'none' }}>
                            <code style={{ fontSize: '0.77rem', color: 'var(--accent)' }}>{u.address}</code>
                          </a>
                        </td>
                        <td style={{ color: 'var(--text2)', fontSize: '0.82rem' }}>{u.label || '—'}</td>
                        <td style={{ textAlign: 'right', fontFamily: 'monospace', fontSize: '0.82rem' }}>{maskAmt(u.amount.toFixed(8))}</td>
                        <td style={{ textAlign: 'right', color: 'var(--text2)', fontSize: '0.82rem' }}>{u.confirmations}</td>
                        <td>
                          <a href={explorerTxUrl(u.txid)} target="_blank" rel="noopener noreferrer" style={{ textDecoration: 'none' }}>
                            <code style={{ fontSize: '0.75rem', color: 'var(--accent)' }} title={u.txid}>
                              {u.txid.slice(0, 12)}…:{u.vout}
                            </code>
                          </a>
                        </td>
                      </tr>
                    ))
                  }
                </tbody>
              </table>
            )}
          </div>
        )
      })()}

      {/* ── SEND ── */}
      {tab === 'send' && (
        <div className="card">
          <h2>Send RXD</h2>

          <datalist id="send-wallet-addresses">
            {addresses.map(a => <option key={a.address} value={a.address}>{a.label ? `${a.label} — ${a.address}` : a.address}</option>)}
          </datalist>

          <div className="form-group">
            <label>Recipient address</label>
            <input value={sendAddr} onChange={e => setSendAddr(e.target.value)} placeholder="1A1zP1eP5QGefi2DMPTfTL5SLmv7Divf…" />
          </div>
          <div className="form-row" style={{ marginTop: '0.75rem' }}>
            <div className="form-group" style={{ flex: 1 }}>
              <label>Amount (RXD)</label>
              <input type="number" value={sendAmt} onChange={e => setSendAmt(e.target.value)} placeholder="0.001" step="0.00000001" min="0" />
            </div>
            <div className="form-group" style={{ flex: 2 }}>
              <label>Memo <span style={{ fontWeight: 400, color: 'var(--text2)', fontSize: '0.8rem' }}>(wallet only — not stored on-chain)</span></label>
              <input value={sendComment} onChange={e => setSendComment(e.target.value)} placeholder="Payment for…" />
            </div>
          </div>

          <div className="form-group" style={{ marginTop: '0.75rem' }}>
            <label>
              Change address{' '}
              <span style={{ fontWeight: 400, color: 'var(--text2)', fontSize: '0.8rem' }}>
                (leave blank for wallet default — set to your primary address to keep change visible in Photonic)
              </span>
            </label>
            <input
              value={sendChangeAddr}
              onChange={e => setSendChangeAddr(e.target.value)}
              placeholder="Leave blank to let wallet choose automatically"
              list="send-wallet-addresses"
              style={{ fontFamily: 'monospace', fontSize: '0.85rem' }}
            />
          </div>

          {/* UTXO picker */}
          <div style={{ marginTop: '1rem', borderTop: '1px solid var(--border)', paddingTop: '0.75rem' }}>
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
              <label style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', cursor: 'pointer', fontSize: '0.85rem', userSelect: 'none', whiteSpace: 'nowrap' }}>
                <input type="checkbox" checked={sendManualInputs}
                  onChange={e => {
                    setSendManualInputs(e.target.checked)
                    if (e.target.checked && sendUtxos.length === 0) loadSendUtxos()
                  }} />
                Choose inputs manually
              </label>
              {sendManualInputs && (
                <button style={{ fontSize: '0.8rem' }} onClick={loadSendUtxos} disabled={sendUtxosLoading}>
                  {sendUtxosLoading ? '…' : 'Reload'}
                </button>
              )}
            </div>

            {sendManualInputs && (
              <div style={{ marginTop: '0.75rem' }}>
                {sendUtxosLoading ? (
                  <p style={{ color: 'var(--text2)', fontSize: '0.85rem' }}>Loading UTXOs…</p>
                ) : sendUtxos.length === 0 ? (
                  <p style={{ color: 'var(--text2)', fontSize: '0.85rem' }}>No UTXOs found. Try clicking Reload.</p>
                ) : (
                  <>
                    <div style={{ display: 'flex', gap: '0.5rem', marginBottom: '0.5rem' }}>
                      <button style={{ fontSize: '0.8rem' }}
                        onClick={() => setSendSelectedKeys(new Set(sendUtxos.map(u => `${u.txid}:${u.vout}`)))}>
                        Select all
                      </button>
                      <button style={{ fontSize: '0.8rem' }}
                        onClick={() => setSendSelectedKeys(new Set())}>
                        Clear
                      </button>
                    </div>
                    <div style={{ maxHeight: '220px', overflowY: 'auto', border: '1px solid var(--border)', borderRadius: 'var(--radius)' }}>
                      <table style={{ width: '100%', fontSize: '0.8rem', borderCollapse: 'collapse' }}>
                        <thead>
                          <tr style={{ background: 'var(--bg2)', borderBottom: '1px solid var(--border)', position: 'sticky', top: 0 }}>
                            <th style={{ width: '28px', padding: '4px 8px' }}></th>
                            <th style={{ padding: '4px 8px', textAlign: 'left' }}>UTXO</th>
                            <th style={{ padding: '4px 8px', textAlign: 'left' }}>Address</th>
                            <th style={{ padding: '4px 8px', textAlign: 'right' }}>Amount (RXD)</th>
                            <th style={{ padding: '4px 8px', textAlign: 'right' }}>Confs</th>
                          </tr>
                        </thead>
                        <tbody>
                          {sendUtxos.map(u => {
                            const key = `${u.txid}:${u.vout}`
                            const checked = sendSelectedKeys.has(key)
                            const toggle = () => setSendSelectedKeys(prev => {
                              const next = new Set(prev)
                              if (next.has(key)) next.delete(key); else next.add(key)
                              return next
                            })
                            return (
                              <tr key={key} onClick={toggle}
                                style={{ cursor: 'pointer', background: checked ? 'var(--bg3)' : 'transparent', borderBottom: '1px solid var(--border)' }}>
                                <td style={{ padding: '4px 8px', textAlign: 'center' }}>
                                  <input type="checkbox" checked={checked} onChange={() => {}} onClick={e => { e.stopPropagation(); toggle() }} />
                                </td>
                                <td style={{ padding: '4px 8px', fontFamily: 'monospace' }}>
                                  {u.txid.slice(0, 8)}…{u.txid.slice(-4)}:{u.vout}
                                </td>
                                <td style={{ padding: '4px 8px', fontFamily: 'monospace', maxWidth: '120px', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                                  {u.address}
                                </td>
                                <td style={{ padding: '4px 8px', textAlign: 'right' }}>
                                  {maskAmt(u.amount.toFixed(8))}
                                </td>
                                <td style={{ padding: '4px 8px', textAlign: 'right', color: u.confirmations === 0 ? 'var(--yellow)' : 'var(--text2)' }}>
                                  {u.confirmations}
                                </td>
                              </tr>
                            )
                          })}
                        </tbody>
                      </table>
                    </div>
                    <div style={{ marginTop: '0.5rem', fontSize: '0.85rem', display: 'flex', gap: '1rem', alignItems: 'center', flexWrap: 'wrap' }}>
                      <span>
                        Selected: <strong>{maskAmt(sendSelectedTotal.toFixed(8))} RXD</strong>
                        {sendSelectedKeys.size > 0 && ` (${sendSelectedKeys.size} UTXO${sendSelectedKeys.size !== 1 ? 's' : ''})`}
                      </span>
                      {sendAmt && sendSelectedKeys.size > 0 && sendSelectedTotal < parseFloat(sendAmt) && (
                        <span style={{ color: 'var(--red)' }}>
                          Insufficient — need at least {parseFloat(sendAmt).toFixed(8)} RXD
                        </span>
                      )}
                    </div>
                  </>
                )}
              </div>
            )}
          </div>

          <button className="primary" style={{ marginTop: '1rem' }} onClick={handleSend}
            disabled={!sendAddr || !sendAmt || (sendManualInputs && sendSelectedKeys.size === 0)}>
            Send
          </button>
        </div>
      )}

      {/* ── PSBT ── */}
      {tab === 'psbt' && (
        <>
          <div className="card">
            <h2>Create Funded PSBT</h2>

            {/* Datalist of wallet addresses for both address inputs */}
            <datalist id="psbt-wallet-addresses">
              {addresses.map(a => <option key={a.address} value={a.address}>{a.label ? `${a.label} — ${a.address}` : a.address}</option>)}
            </datalist>

            {/* Output builder */}
            <div style={{ display: 'flex', gap: '0.5rem', alignItems: 'flex-end', flexWrap: 'wrap', marginBottom: '0.75rem' }}>
              <div className="form-group" style={{ flex: '1 1 260px', minWidth: 0 }}>
                <label>Recipient Address</label>
                <input
                  type="text"
                  list="psbt-wallet-addresses"
                  value={psbtAddr}
                  onChange={e => setPsbtAddr(e.target.value)}
                  onKeyDown={e => { if (e.key === 'Enter') handlePSBTAddOutput() }}
                  placeholder="RXD address…"
                  style={{ fontFamily: 'monospace', fontSize: '0.82rem' }}
                />
              </div>
              <div className="form-group" style={{ width: 130, flexShrink: 0 }}>
                <label>Amount (RXD)</label>
                <input
                  type="number"
                  min="0"
                  step="any"
                  value={psbtAmount}
                  onChange={e => setPsbtAmount(e.target.value)}
                  onKeyDown={e => { if (e.key === 'Enter') handlePSBTAddOutput() }}
                  placeholder="0.0"
                />
              </div>
              <button
                onClick={handlePSBTAddOutput}
                disabled={!psbtAddr.trim() || !psbtAmount || parseFloat(psbtAmount) <= 0}
                style={{ flexShrink: 0, alignSelf: 'flex-end' }}
              >+ Add Output</button>
            </div>

            {/* Output list */}
            {psbtOutputList.length > 0 && (
              <div style={{ marginBottom: '0.75rem', border: '1px solid var(--border)', borderRadius: 'var(--radius)', overflow: 'hidden' }}>
                <table className="table" style={{ margin: 0 }}>
                  <thead>
                    <tr>
                      <th>#</th>
                      <th>Address</th>
                      <th style={{ textAlign: 'right' }}>Amount (RXD)</th>
                      <th></th>
                    </tr>
                  </thead>
                  <tbody>
                    {psbtOutputList.map((o, i) => (
                      <tr key={i}>
                        <td style={{ color: 'var(--text2)', width: 28 }}>{i + 1}</td>
                        <td style={{ fontFamily: 'monospace', fontSize: '0.8rem', wordBreak: 'break-all' }}>{o.address}</td>
                        <td style={{ textAlign: 'right', fontVariantNumeric: 'tabular-nums' }}>{o.amount.toFixed(8)}</td>
                        <td style={{ width: 32, textAlign: 'center' }}>
                          <button
                            onClick={() => setPsbtOutputList(prev => prev.filter((_, j) => j !== i))}
                            title="Remove"
                            style={{ padding: '0.1rem 0.35rem', fontSize: '0.75rem', color: 'var(--red)', background: 'none', border: 'none', cursor: 'pointer' }}
                          >✕</button>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                  <tfoot>
                    <tr>
                      <td colSpan={2} style={{ fontSize: '0.75rem', color: 'var(--text2)' }}>Total</td>
                      <td style={{ textAlign: 'right', fontWeight: 600, fontVariantNumeric: 'tabular-nums' }}>
                        {psbtOutputList.reduce((s, o) => s + o.amount, 0).toFixed(8)} RXD
                      </td>
                      <td></td>
                    </tr>
                  </tfoot>
                </table>
              </div>
            )}

            <div className="form-group" style={{ marginBottom: '0.75rem' }}>
              <label>Change Address <span style={{ color: 'var(--text2)', fontWeight: 400 }}>(optional — wallet picks one if blank)</span></label>
              <input
                type="text"
                list="psbt-wallet-addresses"
                value={psbtChangeAddr}
                onChange={e => setPsbtChangeAddr(e.target.value)}
                placeholder="RXD address for change output…"
                style={{ fontFamily: 'monospace', fontSize: '0.82rem' }}
              />
            </div>

            <button className="primary" onClick={handlePSBTCreate} disabled={psbtOutputList.length === 0}>Create PSBT</button>
            {psbtResult && (
              <>
                <div style={{ display: 'flex', justifyContent: 'flex-end', marginTop: '0.75rem' }}>
                  <button onClick={() => { try { const j = JSON.parse(psbtResult); handleCopy(j.psbt ?? psbtResult) } catch { handleCopy(psbtResult) } }} style={{ fontSize: '0.8rem', display: 'inline-flex', alignItems: 'center', gap: '0.35rem' }} title="Copy PSBT"><CopyIcon /> Copy PSBT</button>
                </div>
                <pre style={{ marginTop: '0.25rem', whiteSpace: 'pre-wrap', wordBreak: 'break-all', fontSize: '0.78rem', background: 'var(--bg3)', padding: '0.75rem', borderRadius: 'var(--radius)' }}>{psbtResult}</pre>
              </>
            )}
          </div>
          <div className="card">
            <h2>Sign PSBT</h2>
            {isLocked && (
              <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', marginBottom: '0.75rem', padding: '0.5rem 0.75rem', background: 'rgba(245,158,11,0.1)', border: '1px solid rgba(245,158,11,0.35)', borderRadius: 'var(--radius)' }}>
                <span style={{ color: '#f59e0b', fontSize: '0.82rem' }}>⚠ Wallet is locked — unlock it above before signing or the PSBT will return <code>complete: false</code>.</span>
              </div>
            )}
            <div className="form-group">
              <label>PSBT (base64)</label>
              <textarea value={psbtInput} onChange={e => setPsbtInput(e.target.value)} rows={3} placeholder="cHNidP8B…"
                style={{ fontFamily: 'monospace', fontSize: '0.82rem', resize: 'vertical' }} />
            </div>
            <button className="primary" onClick={handlePSBTSign} disabled={!psbtInput.trim()} style={{ marginTop: '0.75rem' }}>Sign / Process</button>
            {psbtSignResult && (
              <>
                <div style={{ display: 'flex', justifyContent: 'flex-end', marginTop: '0.75rem' }}>
                  <button onClick={() => { try { const j = JSON.parse(psbtSignResult); handleCopy(j.psbt ?? j.hex ?? psbtSignResult) } catch { handleCopy(psbtSignResult) } }} style={{ fontSize: '0.8rem', display: 'inline-flex', alignItems: 'center', gap: '0.35rem' }} title="Copy result"><CopyIcon /> Copy result</button>
                </div>
                <pre style={{ marginTop: '0.25rem', whiteSpace: 'pre-wrap', wordBreak: 'break-all', fontSize: '0.78rem', background: 'var(--bg3)', padding: '0.75rem', borderRadius: 'var(--radius)' }}>{psbtSignResult}</pre>
              </>
            )}
          </div>
        </>
      )}

      {/* ── SIGN / VERIFY ── */}
      {tab === 'signverify' && (
        <>
          <div className="card">
            <h2>Sign Message</h2>
            <div className="form-group">
              <label>Address</label>
              <input value={signAddr} onChange={e => setSignAddr(e.target.value)} placeholder="Your RXD address" list="wallet-addr-list" />
              <datalist id="wallet-addr-list">
                {addresses.map(a => <option key={a.address} value={a.address}>{a.label}</option>)}
              </datalist>
            </div>
            <div className="form-group" style={{ marginTop: '0.75rem' }}>
              <label>Message</label>
              <textarea value={signMsg} onChange={e => setSignMsg(e.target.value)} rows={3} placeholder="Message to sign…" style={{ resize: 'vertical' }} />
            </div>
            <button className="primary" onClick={handleSign} disabled={!signAddr || !signMsg} style={{ marginTop: '0.75rem' }}>Sign</button>
            {signResult && (
              <div style={{ marginTop: '0.75rem' }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', marginBottom: '0.35rem' }}>
                  <span style={{ fontSize: '0.8rem', color: 'var(--text2)' }}>Signature</span>
                  <button onClick={() => handleCopy(signResult)} style={{ padding: '2px 6px', lineHeight: 0, color: copied === signResult ? '#22c55e' : undefined }} title={copied === signResult ? 'Copied!' : 'Copy signature'}>
                    {copied === signResult ? <CopiedIcon /> : <CopyIcon />}
                  </button>
                </div>
                <code style={{ display: 'block', wordBreak: 'break-all', fontSize: '0.8rem', background: 'var(--bg3)', padding: '0.6rem', borderRadius: 'var(--radius)' }}>{signResult}</code>
              </div>
            )}
          </div>
          <div className="card">
            <h2>Verify Message</h2>
            <div className="form-group">
              <label>Address</label>
              <input value={verifyAddr} onChange={e => setVerifyAddr(e.target.value)} placeholder="Signer's address" />
            </div>
            <div className="form-group" style={{ marginTop: '0.5rem' }}>
              <label>Signature (base64)</label>
              <input value={verifySig} onChange={e => setVerifySig(e.target.value)} placeholder="IHVz…" style={{ fontFamily: 'monospace', fontSize: '0.82rem' }} />
            </div>
            <div className="form-group" style={{ marginTop: '0.5rem' }}>
              <label>Message</label>
              <textarea value={verifyMsg} onChange={e => setVerifyMsg(e.target.value)} rows={3} placeholder="Message that was signed…" style={{ resize: 'vertical' }} />
            </div>
            <button className="primary" onClick={handleVerify} disabled={!verifyAddr || !verifySig || !verifyMsg} style={{ marginTop: '0.75rem' }}>Verify</button>
            {verifyResult !== null && (
              <div style={{ marginTop: '0.75rem' }}>
                <span className={`badge ${verifyResult ? 'green' : 'red'}`} style={{ fontSize: '0.9rem', padding: '0.25rem 0.75rem' }}>
                  {verifyResult ? '✓ Valid signature' : '✗ Invalid signature'}
                </span>
              </div>
            )}
          </div>
        </>
      )}

      {/* ── CONSOLIDATE ── */}
      {tab === 'consolidate' && (
        <>
          <p style={{ color: 'var(--text2)', fontSize: '0.85rem', marginBottom: '1rem' }}>
            Merge small UTXOs into fewer larger outputs to reduce wallet fragmentation and future fee costs.
          </p>

          <div className="card">
            <h2>UTXO Filter</h2>
            <div className="form-row">
              <div className="form-group" style={{ flex: 1 }}>
                <label>Min UTXO (RXD)</label>
                <input type="number" value={consMin} onChange={e => setConsMin(e.target.value)} placeholder="0.001" step="0.00000001" min="0" />
              </div>
              <div className="form-group" style={{ flex: 1 }}>
                <label>Max UTXO (RXD)</label>
                <input type="number" value={consMax} onChange={e => setConsMax(e.target.value)} placeholder="no cap" step="0.00000001" min="0" />
              </div>
              <button onClick={handleConsScan} disabled={!utxos} style={{ alignSelf: 'flex-end' }}>
                Scan UTXOs
              </button>
            </div>
            {utxos && (
              <p style={{ marginTop: '0.6rem', fontSize: '0.82rem', color: consFilteredCount < 2 ? 'var(--text2)' : 'var(--accent)' }}>
                {consFilteredCount} matching UTXOs out of {utxos.length} total
                {consFilteredCount >= 2 && consMaxBatch > 0 && ` → ${consBatchCount} batch(es)`}
                {consFilteredCount === 1 && ' — need at least 2 UTXOs to consolidate'}
                {consFilteredCount === 0 && utxos.length > 0 && ' — no UTXOs match the filter'}
              </p>
            )}
          </div>

          <div className="card">
            <h2>Settings</h2>
            <div className="form-group">
              <label>Destination address</label>
              <select value={consDest} onChange={e => setConsDest(e.target.value)}>
                <option value="">— select address —</option>
                {addresses.map(a => (
                  <option key={a.address} value={a.address}>
                    {a.label ? `${a.label} — ` : ''}{a.address}
                  </option>
                ))}
              </select>
            </div>
            <div className="form-row" style={{ marginTop: '0.75rem' }}>
              <div className="form-group" style={{ flex: 1 }}>
                <label>Max UTXOs per batch</label>
                <input type="number" value={consMaxBatch} onChange={e => setConsMaxBatch(Math.max(1, Number(e.target.value)))} min="1" />
              </div>
              <div className="form-group" style={{ flex: 1 }}>
                <label>Max batches (0 = unlimited)</label>
                <input type="number" value={consMaxRuns} onChange={e => setConsMaxRuns(Math.max(0, Number(e.target.value)))} min="0" />
              </div>
            </div>
          </div>

          <button className="primary" onClick={handleConsolidate}
            disabled={!consScanned || consScanned.length < 2 || !consDest || consRunning}
            style={{ marginBottom: '1rem' }}>
            {consRunning ? 'Consolidating…' : 'Consolidate UTXOs'}
          </button>

          {consLog.length > 0 && (
            <div className="card">
              <h2>Progress</h2>
              <div style={{ fontSize: '0.8rem', color: 'var(--text2)', lineHeight: 1.7, fontFamily: 'monospace', whiteSpace: 'pre-wrap' }}>
                {consLog.map((line, i) => {
                  const m = line.match(/^(.*txid: )([0-9a-f]{64})(.*)$/i)
                  if (m) return (
                    <div key={i}>{m[1]}<a href={explorerTxUrl(m[2])} target="_blank" rel="noopener noreferrer" style={{ color: 'var(--accent)' }}>{m[2]}</a>{m[3]}</div>
                  )
                  return <div key={i}>{line}</div>
                })}
              </div>
            </div>
          )}
        </>
      )}

      {/* ── TOKENS ── */}
      {tab === 'tokens' && (
        <>
          {/* Sub-tab bar */}
          <div style={{ display: 'flex', gap: '0.5rem', marginBottom: '1rem', borderBottom: '1px solid var(--border)', paddingBottom: '0.5rem' }}>
            <button onClick={() => setMintSubTab('list')}
              style={{ fontSize: '0.85rem', padding: '0.3rem 1rem', background: mintSubTab === 'list' ? 'var(--accent)' : 'var(--bg3)', color: mintSubTab === 'list' ? '#000' : 'var(--text)', border: '1px solid var(--border)', borderRadius: 'var(--radius)' }}>
              My Tokens
            </button>
            <button onClick={() => setMintSubTab('mint')}
              style={{ fontSize: '0.85rem', padding: '0.3rem 1rem', background: mintSubTab === 'mint' ? 'var(--accent)' : 'var(--bg3)', color: mintSubTab === 'mint' ? '#000' : 'var(--text)', border: '1px solid var(--border)', borderRadius: 'var(--radius)' }}>
              + Mint
            </button>
          </div>

          {mintSubTab === 'list' && (
          <>
          <div className="card">
            <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', marginBottom: '0.5rem', flexWrap: 'wrap' }}>
              <h2 style={{ margin: 0, flex: 1 }}>
                Glyphs{glyphTokens.length > 0 ? ` (${displayedTokens.length}${glyphFilter !== 'All' ? `/${glyphTokens.length}` : ''})` : ''}
              </h2>
              <button onClick={() => walletName !== null && loadGlyphTokens(walletName)} disabled={glyphLoading} style={{ fontSize: '0.8rem' }} title="Refresh">
                {glyphLoading ? 'Loading…' : '↺ Refresh'}
              </button>
            </div>

            {/* Sort + filter toolbar */}
            {glyphTokens.length > 0 && (
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '0.4rem', marginBottom: '0.75rem', alignItems: 'center' }}>
                {/* Sort */}
                <span style={{ fontSize: '0.72rem', color: 'var(--text2)', marginRight: '0.1rem' }}>Sort:</span>
                {(['newest', 'oldest', 'name'] as const).map(s => (
                  <button key={s} onClick={() => { setGlyphSort(s); setSelectedTokenRef(null) }}
                    style={{ fontSize: '0.72rem', padding: '2px 8px',
                      background: glyphSort === s ? 'var(--accent)' : 'var(--bg3)',
                      color: glyphSort === s ? '#000' : 'var(--text)',
                      border: '1px solid var(--border)', borderRadius: 'var(--radius)' }}>
                    {s === 'newest' ? '↓ Newest' : s === 'oldest' ? '↑ Oldest' : 'A–Z Name'}
                  </button>
                ))}
                <span style={{ fontSize: '0.72rem', color: 'var(--text2)', marginLeft: '0.4rem', marginRight: '0.1rem' }}>Type:</span>
                {(['All', 'NFT', 'FT', 'User', 'Container', 'dMint FT', 'WAVE'] as const).map(f => (
                  <button key={f} onClick={() => { setGlyphFilter(f); setSelectedTokenRef(null) }}
                    style={{ fontSize: '0.72rem', padding: '2px 8px',
                      background: glyphFilter === f ? 'var(--accent)' : 'var(--bg3)',
                      color: glyphFilter === f ? '#000' : 'var(--text)',
                      border: '1px solid var(--border)', borderRadius: 'var(--radius)' }}>
                    {f}
                  </button>
                ))}
              </div>
            )}

            {glyphLoading && <p style={{ color: 'var(--accent)', fontSize: '0.82rem', marginBottom: '0.5rem' }}>Loading glyph tokens…</p>}
            {rpcGlyphEntries === null && !glyphLoading && <p style={{ color: 'var(--text2)' }}>Loading…</p>}
            {rpcGlyphEntries !== null && glyphTokens.length === 0 && !glyphLoading && (
              <p style={{ color: 'var(--text2)', fontSize: '0.85rem' }}>No glyph tokens found.</p>
            )}
            {rpcGlyphEntries !== null && glyphTokens.length > 0 && displayedTokens.length === 0 && (
              <p style={{ color: 'var(--text2)', fontSize: '0.85rem' }}>No tokens match the current filter.</p>
            )}
            <div style={{ display: 'flex', gap: '1rem', alignItems: 'flex-start' }}>
              {/* Left: token grid */}
              <div style={{ flex: 1, minWidth: 0 }}>
                {displayedTokens.length > 0 && (
                  <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(140px, 1fr))', gap: '0.75rem', marginTop: '0.5rem' }}>
                    {displayedTokens.map(token => {
                      const meta = glyphMeta.get(token.ref)
                      const loading = glyphMetaLoading.has(token.ref)
                      const decimals = meta?.decimals ?? 0
                      const name = meta?.name ?? (loading ? '…' : token.ref.slice(0, 8) + '…')
                      const ticker = meta?.ticker ?? ''
                      const imgUrl = meta ? metaToDataUrl(meta) : null
                      const isSelected = selectedTokenRef === token.ref
                      return (
                        <div
                          key={token.ref}
                          onClick={() => setSelectedTokenRef(isSelected ? null : token.ref)}
                          style={{
                            background: isSelected ? 'var(--bg3)' : 'var(--bg2)',
                            border: `1px solid ${isSelected ? 'var(--accent)' : 'var(--border)'}`,
                            borderRadius: 'var(--radius)',
                            overflow: 'hidden',
                            cursor: 'pointer',
                            display: 'flex',
                            flexDirection: 'column',
                          }}>
                          {/* Image area */}
                          <div style={{ width: '100%', aspectRatio: '1', background: 'var(--bg3)', overflow: 'hidden', position: 'relative' }}>
                            {imgUrl
                              ? <img src={imgUrl} alt={name} style={{ width: '100%', height: '100%', objectFit: 'scale-down', display: 'block' }} />
                              : <div style={{ width: '100%', height: '100%', display: 'flex', alignItems: 'center', justifyContent: 'center', color: 'var(--text2)', fontSize: loading ? '1.5rem' : '0.7rem', fontWeight: 600, letterSpacing: '0.05em' }}>
                                  {thumbnailPlaceholder(meta, loading)}
                                </div>
                            }
                            {(() => { const tt = tokenTypeInfo(token.isSingleton, glyphMeta.get(token.ref)); return (
                              <span className={`badge ${tt.cls}`} style={{ position: 'absolute', top: 4, right: 4, fontSize: '0.65rem' }}>{tt.label}</span>
                            ) })()}
                          </div>
                          {/* Card footer */}
                          <div style={{ padding: '0.4rem 0.5rem', display: 'flex', flexDirection: 'column', gap: '0.15rem' }}>
                            <div style={{ fontWeight: 600, fontSize: '0.82rem', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} title={name}>
                              {(() => { const ic = tokenTypeInfo(token.isSingleton, glyphMeta.get(token.ref)).icon; return ic ? <span style={{ marginRight: '0.25rem', color: 'var(--text2)', fontWeight: 400, display: 'inline-flex', alignItems: 'center', verticalAlign: 'middle' }}>{ic}</span> : null })()}
                              {name}
                            </div>
                            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                              <span style={{ fontFamily: 'monospace', fontSize: '0.75rem', color: 'var(--accent)' }}>
                                {maskAmt(displayBalance(token.totalSats, decimals))}{ticker ? ` ${ticker}` : ' ph'}
                              </span>
                              <button
                                style={{ fontSize: '0.65rem', padding: '1px 7px' }}
                                onClick={e => { e.stopPropagation(); setTokenSendRef(token); setTokenSendAmt(''); setTokenSendAddr('') }}>
                                Send
                              </button>
                            </div>
                          </div>
                        </div>
                      )
                    })}
                  </div>
                )}
              </div>
              {/* Right: detail panel */}
              {selectedTokenRef && (() => {
                const token = glyphTokens.find(t => t.ref === selectedTokenRef)
                if (!token) return null
                const meta = glyphMeta.get(token.ref)
                const imgUrl = meta ? metaToDataUrl(meta) : null
                const decimals = meta?.decimals ?? 0
                const attrs = meta?.attrs as Record<string, unknown> | undefined
                return (
                  <div style={{ width: 300, flexShrink: 0, position: 'sticky', top: '1rem', background: 'var(--bg3)', borderRadius: 'var(--radius)', border: '1px solid var(--border)', overflow: 'hidden' }}>
                    {/* Image */}
                    <div style={{ width: '100%', aspectRatio: '1', background: 'var(--bg2)', position: 'relative', overflow: 'hidden' }}>
                      {imgUrl
                        ? <img src={imgUrl} alt={meta?.name ?? ''} style={{ width: '100%', height: '100%', objectFit: 'contain', display: 'block' }} />
                        : tokenTypeInfo(token.isSingleton, meta).label === 'WAVE'
                          ? <div style={{ width: '100%', height: '100%', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: '0.75rem', background: 'linear-gradient(145deg, #0d1b2a 0%, #1b2d44 55%, #0d2137 100%)', color: 'rgba(96,165,250,0.65)' }}>
                              <IconWave size={44} />
                              <div style={{ color: '#e2e8f0', fontWeight: 700, fontSize: '1.15rem', textAlign: 'center', padding: '0 1.25rem', wordBreak: 'break-word', lineHeight: 1.25 }}>{meta?.name ?? ''}</div>
                            </div>
                          : <div style={{ width: '100%', height: '100%', display: 'flex', alignItems: 'center', justifyContent: 'center', color: 'var(--text2)' }}>
                              {thumbnailPlaceholder(meta, glyphMetaLoading.has(token.ref))}
                            </div>
                      }
                      <button
                        onClick={() => setSelectedTokenRef(null)}
                        style={{ position: 'absolute', top: 6, right: 6, fontSize: '0.7rem', padding: '2px 7px', background: 'rgba(0,0,0,0.55)', border: '1px solid rgba(255,255,255,0.15)', color: '#fff', borderRadius: 4, cursor: 'pointer', lineHeight: 1.4 }}>
                        ✕
                      </button>
                      {(() => { const tt = tokenTypeInfo(token.isSingleton, meta); return (
                        <span className={`badge ${tt.cls}`} style={{ position: 'absolute', top: 6, left: 6, fontSize: '0.65rem' }}>{tt.label}</span>
                      ) })()}
                    </div>
                    {/* Info */}
                    <div style={{ padding: '0.75rem', overflowY: 'auto', maxHeight: 'calc(100vh - 340px)' }}>
                      <div style={{ fontWeight: 700, fontSize: '0.95rem', marginBottom: '0.25rem', wordBreak: 'break-word' }}>{meta?.name ?? token.ref.slice(0, 12) + '…'}</div>
                      {meta?.ticker && <div style={{ color: 'var(--text2)', fontSize: '0.82rem', marginBottom: '0.3rem' }}>{meta.ticker}</div>}
                      {meta?.desc && typeof meta.desc === 'string' && tokenTypeInfo(token.isSingleton, meta).label !== 'WAVE' && (
                        <div style={{ color: 'var(--text)', fontSize: '0.82rem', marginBottom: '0.5rem', lineHeight: 1.4 }}>{meta.desc}</div>
                      )}
                      <div style={{ fontSize: '0.78rem', color: 'var(--text2)', display: 'flex', flexDirection: 'column', gap: '0.2rem' }}>
                        <div>Balance: <span style={{ color: 'var(--text)', fontFamily: 'monospace' }}>{maskAmt(displayBalance(token.totalSats, decimals))} {meta?.ticker ?? 'ph'}</span></div>
                        <div>UTXOs: <span style={{ color: 'var(--text)' }}>{token.utxos.length}</span></div>
                        <div style={{ display: 'flex', alignItems: 'center', gap: '0.3rem', flexWrap: 'wrap' }}>
                          Ref:
                          <a href={explorerTxUrl(token.refTxid)} target="_blank" rel="noopener noreferrer"
                            title={`${token.refTxid}:${token.refVout} — view on explorer`}
                            style={{ fontSize: '0.72rem', color: 'var(--accent)', fontFamily: 'monospace', textDecoration: 'none' }}>
                            {token.refTxid.slice(0, 16)}…:{token.refVout}
                          </a>
                          <button title="Copy ref" onClick={() => handleCopy(`${token.refTxid}:${token.refVout}`)}
                            style={{ background: 'none', border: 'none', cursor: 'pointer', color: copied === `${token.refTxid}:${token.refVout}` ? '#22c55e' : 'var(--text2)', padding: 0, lineHeight: 0 }}>
                            {copied === `${token.refTxid}:${token.refVout}` ? <CopiedIcon size={13} /> : <CopyIcon size={13} />}
                          </button>
                        </div>
                      </div>
                      <button className="primary" style={{ width: '100%', marginTop: '0.75rem', marginBottom: '0.25rem' }}
                        onClick={() => { setTokenSendRef(token); setTokenSendAmt(''); setTokenSendAddr('') }}>
                        Send {meta?.name ?? 'Token'}
                      </button>
                      <div style={{ fontSize: '0.78rem', color: 'var(--text2)', display: 'flex', flexDirection: 'column', gap: '0.2rem' }}>
                        {/* WAVE-specific layout */}
                        {tokenTypeInfo(token.isSingleton, meta).label === 'WAVE' && (() => {
                          if (!attrs) return null
                          const target = attrs.target
                          const records = attrs.records as Record<string, unknown> | undefined
                          const socialEntries = records
                            ? Object.entries(records).filter((e): e is [string, string] => typeof e[1] === 'string' && !!socialLink(e[0], e[1]))
                            : []
                          const attrEntries = Object.entries(attrs).filter(([k]) => k !== 'records')
                          return (
                            <>
                              {target !== undefined && (
                                <div style={{ marginTop: '0.5rem', background: '#1a2d4a', borderRadius: 8, padding: '0.65rem 0.75rem' }}>
                                  <div style={{ display: 'flex', alignItems: 'center', gap: '0.4rem', marginBottom: '0.45rem', color: '#60a5fa', fontWeight: 600, fontSize: '0.82rem' }}>
                                    <IconWave size={14} /> WAVE Name Resolution
                                  </div>
                                  <div style={{ fontSize: '0.72rem', color: '#93c5fd', marginBottom: '0.2rem', display: 'flex', alignItems: 'center', gap: '0.3rem' }}>
                                    <span>⊙</span> Points to:
                                  </div>
                                  <code style={{ fontSize: '0.7rem', color: '#60a5fa', wordBreak: 'break-all', cursor: 'pointer', display: 'block', lineHeight: 1.5 }}
                                    title="Click to copy" onClick={() => handleCopy(String(target))}>
                                    {String(target)}
                                  </code>
                                </div>
                              )}
                              {socialEntries.length > 0 && (
                                <div style={{ marginTop: '0.5rem', background: 'var(--bg2)', borderRadius: 8, padding: '0.65rem 0.75rem' }}>
                                  <div style={{ display: 'flex', alignItems: 'center', gap: '0.4rem', marginBottom: '0.45rem', fontWeight: 600, fontSize: '0.82rem', color: 'var(--text)' }}>
                                    <IconLink size={14} /> Social Links
                                  </div>
                                  <div style={{ display: 'flex', flexWrap: 'wrap', gap: '0.4rem' }}>
                                    {socialEntries.map(([k, v]) => {
                                      const sl = socialLink(k, v)!
                                      return sl.href ? (
                                        <a key={k} href={sl.href} target="_blank" rel="noopener noreferrer"
                                          style={{ display: 'inline-flex', alignItems: 'center', gap: '0.3rem', padding: '0.3rem 0.7rem', background: '#356fdb', borderRadius: 20, color: '#fff', fontSize: '0.77rem', textDecoration: 'none', fontWeight: 500 }}>
                                          {sl.icon} {v}
                                        </a>
                                      ) : (
                                        <span key={k} style={{ display: 'inline-flex', alignItems: 'center', gap: '0.3rem', padding: '0.3rem 0.7rem', background: '#356fdb', borderRadius: 20, color: '#fff', fontSize: '0.77rem', fontWeight: 500 }}>
                                          {sl.icon} {v}
                                        </span>
                                      )
                                    })}
                                  </div>
                                </div>
                              )}
                              {attrEntries.length > 0 && (
                                <>
                                  <div style={{ fontSize: '0.72rem', fontWeight: 600, color: 'var(--text2)', marginTop: '0.6rem', marginBottom: '0.3rem', letterSpacing: '0.04em', textTransform: 'uppercase' }}>Attributes</div>
                                  <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0.3rem' }}>
                                    {attrEntries.map(([k, v]) => (
                                      <div key={k} style={{ background: 'var(--bg2)', borderRadius: 6, padding: '0.4rem 0.5rem', minWidth: 0 }}>
                                        <div style={{ fontSize: '0.62rem', color: 'var(--text2)', marginBottom: '0.15rem', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', display: 'flex', alignItems: 'center', gap: '0.25rem' }}>
                                          <svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ flexShrink: 0 }}><path d="M12.586 2.586A2 2 0 0 0 11.172 2H4a2 2 0 0 0-2 2v7.172a2 2 0 0 0 .586 1.414l8.704 8.704a2.426 2.426 0 0 0 3.42 0l6.58-6.58a2.426 2.426 0 0 0 0-3.42z"/><circle cx="7.5" cy="7.5" r=".5" fill="currentColor"/></svg>
                                          <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{k}</span>
                                        </div>
                                        <div style={{ fontSize: '0.78rem', wordBreak: 'break-all', lineHeight: 1.3 }}>{String(v)}</div>
                                      </div>
                                    ))}
                                  </div>
                                </>
                              )}
                            </>
                          )
                        })()}
                        {/* Generic attrs for non-WAVE tokens */}
                        {attrs && Object.keys(attrs).length > 0 && tokenTypeInfo(token.isSingleton, meta).label !== 'WAVE' && (() => {
                          const tagIcon = <svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ flexShrink: 0 }}><path d="M12.586 2.586A2 2 0 0 0 11.172 2H4a2 2 0 0 0-2 2v7.172a2 2 0 0 0 .586 1.414l8.704 8.704a2.426 2.426 0 0 0 3.42 0l6.58-6.58a2.426 2.426 0 0 0 0-3.42z"/><circle cx="7.5" cy="7.5" r=".5" fill="currentColor"/></svg>
                          const allEntries = Object.entries(attrs)
                          const socialEntries = allEntries.filter(([k, v]) => typeof v === 'string' && socialLink(k, v) !== null) as [string, string][]
                          const plainEntries = allEntries.filter(([k, v]) => !(typeof v === 'string' && socialLink(k, v) !== null))
                          return (
                            <div style={{ marginTop: '0.5rem' }}>
                              {socialEntries.length > 0 && (
                                <div style={{ marginBottom: '0.5rem', background: 'var(--bg2)', borderRadius: 8, padding: '0.65rem 0.75rem' }}>
                                  <div style={{ display: 'flex', alignItems: 'center', gap: '0.4rem', marginBottom: '0.45rem', fontWeight: 600, fontSize: '0.82rem', color: 'var(--text)' }}>
                                    <IconLink size={14} /> Social Links
                                  </div>
                                  <div style={{ display: 'flex', flexWrap: 'wrap', gap: '0.4rem' }}>
                                    {socialEntries.map(([k, v]) => {
                                      const sl = socialLink(k, v)!
                                      return sl.href ? (
                                        <a key={k} href={sl.href} target="_blank" rel="noopener noreferrer"
                                          style={{ display: 'inline-flex', alignItems: 'center', gap: '0.3rem', padding: '0.3rem 0.7rem', background: '#356fdb', borderRadius: 20, color: '#fff', fontSize: '0.77rem', textDecoration: 'none', fontWeight: 500 }}>
                                          {sl.icon} {v}
                                        </a>
                                      ) : (
                                        <span key={k} style={{ display: 'inline-flex', alignItems: 'center', gap: '0.3rem', padding: '0.3rem 0.7rem', background: '#356fdb', borderRadius: 20, color: '#fff', fontSize: '0.77rem', fontWeight: 500 }}>
                                          {sl.icon} {v}
                                        </span>
                                      )
                                    })}
                                  </div>
                                </div>
                              )}
                              {plainEntries.length > 0 && (
                                <>
                                  <div style={{ fontSize: '0.72rem', fontWeight: 600, color: 'var(--text2)', marginBottom: '0.3rem', letterSpacing: '0.04em', textTransform: 'uppercase' }}>Attributes</div>
                                  <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0.3rem' }}>
                                    {plainEntries.map(([k, v]) => {
                                      if (v !== null && typeof v === 'object' && !Array.isArray(v)) {
                                        return (
                                          <div key={k} style={{ background: 'var(--bg2)', borderRadius: 6, padding: '0.4rem 0.5rem', minWidth: 0, gridColumn: '1 / -1' }}>
                                            <div style={{ fontSize: '0.62rem', color: 'var(--text2)', marginBottom: '0.3rem', textTransform: 'uppercase', letterSpacing: '0.03em', display: 'flex', alignItems: 'center', gap: '0.25rem' }}>
                                              {tagIcon}<span>{k}</span>
                                            </div>
                                            {Object.entries(v as Record<string, unknown>).map(([sk, sv]) => {
                                              const sl = k === 'records' && typeof sv === 'string' ? socialLink(sk, sv) : null
                                              return sl ? (
                                                <div key={sk} style={{ display: 'flex', alignItems: 'center', gap: '0.35rem', marginBottom: '0.15rem' }}>
                                                  <span style={{ color: 'var(--text2)', display: 'flex', alignItems: 'center' }}>{sl.icon}</span>
                                                  {sl.href
                                                    ? <a href={sl.href} target="_blank" rel="noopener noreferrer" style={{ color: 'var(--accent)', fontSize: '0.78rem', wordBreak: 'break-all' }}>{String(sv)}</a>
                                                    : <span style={{ fontSize: '0.78rem' }}>{String(sv)}</span>}
                                                </div>
                                              ) : (
                                                <div key={sk} style={{ fontSize: '0.75rem', display: 'flex', gap: '0.3rem', marginBottom: '0.1rem' }}>
                                                  <span style={{ color: 'var(--text2)', flexShrink: 0 }}>{sk}:</span>
                                                  <span style={{ wordBreak: 'break-all' }}>{String(sv)}</span>
                                                </div>
                                              )
                                            })}
                                          </div>
                                        )
                                      }
                                      const displayVal = Array.isArray(v) ? v.map(String).join(', ') : String(v)
                                      return (
                                        <div key={k} style={{ background: 'var(--bg2)', borderRadius: 6, padding: '0.4rem 0.5rem', minWidth: 0 }}>
                                          <div style={{ fontSize: '0.62rem', color: 'var(--text2)', marginBottom: '0.15rem', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', display: 'flex', alignItems: 'center', gap: '0.25rem' }}>
                                            {tagIcon}<span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{k}</span>
                                          </div>
                                          <div style={{ fontSize: '0.78rem', wordBreak: 'break-all', lineHeight: 1.3 }}>{displayVal}</div>
                                        </div>
                                      )
                                    })}
                                  </div>
                                </>
                              )}
                            </div>
                          )
                        })()}
                        {metaMainUrl(meta) && (() => {
                          const mu = metaMainUrl(meta)!
                          return (
                            <div style={{ marginTop: '0.3rem' }}>
                              <span style={{ color: 'var(--text2)' }}>{mu.isIpfs ? 'IPFS' : 'Link'}:</span>{' '}
                              <a href={mu.isIpfs ? `https://ipfs.io/ipfs/${mu.url.slice(7)}` : mu.url}
                                 target="_blank" rel="noopener noreferrer"
                                 style={{ color: 'var(--accent)', fontSize: '0.72rem', wordBreak: 'break-all' }}>
                                {mu.url}
                              </a>
                            </div>
                          )
                        })()}
                        {/* Inline bytes content viewer (text/html, text/plain, etc.) */}
                        {(() => {
                          const mainC = meta?.main as { b?: unknown; t?: unknown } | undefined
                          if (!(mainC?.b instanceof Uint8Array) || typeof mainC?.t !== 'string') return null
                          const bytes = mainC.b
                          const contentType = mainC.t
                          const sizeKb = (bytes.byteLength / 1024).toFixed(1)

                          if (contentType === 'text/plain') {
                            const text = new TextDecoder().decode(bytes)
                            return (
                              <div style={{ marginTop: '0.5rem' }}>
                                <div style={{ fontSize: '0.72rem', fontWeight: 600, color: 'var(--text2)', marginBottom: '0.25rem', textTransform: 'uppercase', letterSpacing: '0.04em' }}>
                                  Text Content <span style={{ fontWeight: 400 }}>({sizeKb} KB)</span>
                                </div>
                                <pre style={{
                                  fontSize: '0.78rem', color: 'var(--text)', background: 'var(--bg2)',
                                  borderRadius: 6, padding: '0.5rem 0.65rem',
                                  whiteSpace: 'pre-wrap', wordBreak: 'break-word',
                                  maxHeight: '12rem', overflowY: 'auto',
                                  margin: 0, lineHeight: 1.55,
                                }}>
                                  {text}
                                </pre>
                              </div>
                            )
                          }

                          // text/html and other openable types
                          const label = contentType === 'text/html' ? '↗ Open HTML' : `↗ Open (${contentType})`
                          return (
                            <div style={{ marginTop: '0.5rem' }}>
                              <button
                                onClick={() => {
                                  const blob = new Blob([bytes.slice()], { type: contentType })
                                  const url = URL.createObjectURL(blob)
                                  window.open(url, '_blank', 'noopener,noreferrer')
                                  setTimeout(() => URL.revokeObjectURL(url), 60_000)
                                }}
                                style={{
                                  display: 'inline-flex', alignItems: 'center', gap: '0.3rem',
                                  padding: '0.35rem 0.8rem', borderRadius: 6,
                                  background: 'var(--accent)', color: '#000', border: 'none',
                                  fontSize: '0.8rem', fontWeight: 600, cursor: 'pointer',
                                }}
                              >
                                {label} <span style={{ opacity: 0.75, fontWeight: 400 }}>({sizeKb} KB)</span>
                              </button>
                            </div>
                          )
                        })()}
                      </div>
                      {meta && (
                        <details style={{ marginTop: '0.75rem' }}>
                          <summary style={{ fontSize: '0.75rem', color: 'var(--text2)', cursor: 'pointer', userSelect: 'none' }}>Token Data (CBOR)</summary>
                          <pre style={{
                            marginTop: '0.4rem', padding: '0.75rem',
                            background: 'var(--bg2)', borderRadius: 'var(--radius)',
                            fontSize: '0.72rem', overflowX: 'auto', whiteSpace: 'pre-wrap', wordBreak: 'break-all',
                          }}>
                            {JSON.stringify(meta, (_, v) => {
                              if (v instanceof Uint8Array) return v.byteLength === 36
                                ? Array.from(v).map(b => b.toString(16).padStart(2, '0')).join('')
                                : `<bytes:${v.byteLength}>`
                              if (typeof v === 'string' && v.length > 200) return v.slice(0, 200) + '…'
                              return v
                            }, 2)}
                          </pre>
                        </details>
                      )}
                      {!meta && glyphMetaLoading.has(token.ref) && (
                        <p style={{ marginTop: '0.5rem', fontSize: '0.78rem', color: 'var(--text2)' }}>Loading metadata…</p>
                      )}
                      {!meta && !glyphMetaLoading.has(token.ref) && (
                        <details style={{ marginTop: '0.5rem' }}>
                          <summary style={{ fontSize: '0.75rem', color: 'var(--text2)', cursor: 'pointer', userSelect: 'none' }}>No metadata found — debug info</summary>
                          <div style={{ marginTop: '0.4rem', padding: '0.6rem', background: 'var(--bg2)', borderRadius: 'var(--radius)', fontSize: '0.72rem', fontFamily: 'monospace', color: 'var(--text2)' }}>
                            <div>Genesis txid: {token.refTxid}:{token.refVout}</div>
                            <div>Current UTXO tx: {token.utxos[0]?.txid ?? '—'}</div>
                            <div style={{ marginTop: '0.25rem', color: 'var(--text2)' }}>
                              Walked backwards up to 30 hops following the glyph-vin chain, then
                              queried the backend wallet spend index (forward walk from genesis).
                              Still not found — enable <code>txindex=1</code> in radiant.conf and rescan,
                              or run <code>rescanblockchain</code> if wallet tracking was recently updated.
                            </div>
                          </div>
                        </details>
                      )}
                    </div>
                  </div>
                )
              })()}
            </div>
          </div>

          {/* ── Send token modal (rendered inline here, visually fixed) ── */}
          </>
          )}

          {mintSubTab === 'mint' && (
          <div className="card">
            <h2>Mint New Glyph Token</h2>

            {/* Token type */}
            <div className="form-row" style={{ marginBottom: '1rem', gap: '0.5rem', flexWrap: 'wrap' }}>
              {([
                { t: 'nft' as const, label: 'NFT' },
                { t: 'ft' as const, label: 'Fungible (FT)' },
                { t: 'user' as const, label: 'User' },
                { t: 'container' as const, label: 'Container' },
              ]).map(({ t, label }) => (
                <button key={t} className={mintType === t ? 'primary' : ''}
                  onClick={() => setMintType(t)}
                  style={{ padding: '0.3rem 1.2rem', fontSize: '0.85rem' }}>
                  {label}
                </button>
              ))}
            </div>

            {/* Deploy method — FT only */}
            {mintType === 'ft' && (
              <div className="form-row" style={{ marginBottom: '1rem', gap: '0.5rem', alignItems: 'center', flexWrap: 'wrap' }}>
                <span style={{ fontSize: '0.8rem', color: 'var(--text2)' }}>Deploy method:</span>
                {([
                  { m: 'direct' as const, label: 'Direct Mint' },
                  { m: 'dmint'  as const, label: 'dMint' },
                ]).map(({ m, label }) => (
                  <button key={m} className={mintDeployMethod === m ? 'primary' : ''}
                    onClick={() => setMintDeployMethod(m)}
                    style={{ padding: '0.3rem 1.2rem', fontSize: '0.85rem' }}>
                    {label}
                  </button>
                ))}
                {mintDeployMethod === 'dmint' && (
                  <span style={{ fontSize: '0.75rem', color: 'var(--text2)', marginLeft: '0.25rem' }}>
                    Deploys a proof-of-work minting contract. Miners receive tokens on each valid submission.
                  </span>
                )}
              </div>
            )}

            {/* Content source */}
            <div style={{ marginBottom: '1rem' }}>
              <label style={{ fontSize: '0.8rem', color: 'var(--text2)', display: 'block', marginBottom: '0.4rem' }}>Content {mintType === 'ft' && mintDeployMethod === 'dmint' ? '(token image / icon)' : ''}</label>
              <div className="form-row" style={{ gap: '1rem', marginBottom: '0.5rem' }}>
                {(['none', 'file', 'url', 'text'] as const).map(m => (
                  <label key={m} style={{ display: 'flex', alignItems: 'center', gap: '0.3rem', fontSize: '0.85rem', cursor: 'pointer' }}>
                    <input type="radio" name="mintDataMode" checked={mintDataMode === m} onChange={() => { setMintDataMode(m); setMintFileBytes(null); setMintFilePreview(null); setMintFileMime(''); setMintFileName(''); setMintContentUrl(''); setMintText('') }} />
                    {m === 'none' ? 'None' : m === 'file' ? 'File' : m === 'url' ? 'URL' : 'Text'}
                  </label>
                ))}
              </div>

              {mintDataMode === 'file' && (() => {
                const loadFile = (f: File) => {
                  setMintFileMime(f.type || 'application/octet-stream')
                  setMintFileName(f.name)
                  const reader = new FileReader()
                  reader.onload = ev => {
                    const buf = new Uint8Array(ev.target!.result as ArrayBuffer)
                    setMintFileBytes(buf)
                    if (f.type.startsWith('image/')) setMintFilePreview(URL.createObjectURL(f))
                    else setMintFilePreview(null)
                  }
                  reader.readAsArrayBuffer(f)
                }
                const clearFile = () => { setMintFileBytes(null); setMintFileName(''); setMintFileMime(''); setMintFilePreview(null) }
                return mintFileBytes ? (
                  /* ── File selected: Photonic-style metadata row ── */
                  <div style={{ display: 'flex', alignItems: 'center', gap: '0.75rem', background: 'var(--bg3)', borderRadius: 'var(--radius)', padding: '0.6rem 0.75rem', border: '1px solid var(--border)' }}>
                    {/* Thumbnail */}
                    <div style={{ width: 56, height: 56, flexShrink: 0, borderRadius: 6, overflow: 'hidden', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
                      {mintFilePreview
                        ? <img src={mintFilePreview} alt="" style={{ width: '100%', height: '100%', objectFit: 'cover' }} />
                        : <span style={{ fontSize: '1.5rem' }}>📄</span>}
                    </div>
                    {/* Metadata */}
                    <div style={{ flex: 1, minWidth: 0 }}>
                      <div style={{ fontWeight: 600, fontSize: '0.85rem', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{mintFileName}</div>
                      <div style={{ color: 'var(--text2)', fontSize: '0.75rem', marginTop: '0.15rem' }}>{mintFileMime}</div>
                      <div style={{ color: 'var(--text2)', fontSize: '0.75rem' }}>{(mintFileBytes.length / 1024).toFixed(2)} kB</div>
                    </div>
                    {/* Remove */}
                    <button onClick={clearFile}
                      style={{ flexShrink: 0, background: 'var(--bg2)', border: '1px solid var(--border)', borderRadius: 6, padding: '0.35rem 0.6rem', fontSize: '1rem', cursor: 'pointer', color: 'var(--text2)', lineHeight: 1 }}
                      title="Remove file">🗑</button>
                  </div>
                ) : (
                  /* ── Drop zone ── */
                  <div
                    onClick={() => document.getElementById('mintFileInput')?.click()}
                    onDragOver={e => e.preventDefault()}
                    onDrop={e => { e.preventDefault(); const f = e.dataTransfer.files[0]; if (f) loadFile(f) }}
                    style={{ border: '2px dashed var(--border)', borderRadius: 'var(--radius)', padding: '2rem', textAlign: 'center', cursor: 'pointer' }}>
                    <div style={{ fontSize: '2rem', marginBottom: '0.5rem', opacity: 0.4 }}>⬆</div>
                    <div style={{ color: 'var(--text2)', fontSize: '0.85rem' }}>Click or drag a file here</div>
                    <div style={{ color: 'var(--text2)', fontSize: '0.75rem', marginTop: '0.25rem', opacity: 0.7 }}>Image, text or other content (max 512 KB)</div>
                  </div>
                )
              })()}
              <input id="mintFileInput" type="file" style={{ display: 'none' }}
                onChange={e => { const f = e.target.files?.[0]; if (f) { const loadFile = (file: File) => { setMintFileMime(file.type || 'application/octet-stream'); setMintFileName(file.name); const reader = new FileReader(); reader.onload = ev => { const buf = new Uint8Array(ev.target!.result as ArrayBuffer); setMintFileBytes(buf); if (file.type.startsWith('image/')) setMintFilePreview(URL.createObjectURL(file)); else setMintFilePreview(null) }; reader.readAsArrayBuffer(file) }; loadFile(f) } }} />

              {mintDataMode === 'url' && (
                <input value={mintContentUrl} onChange={e => setMintContentUrl(e.target.value)}
                  placeholder="https://example.com/image.png" style={{ width: '100%' }} />
              )}

              {mintDataMode === 'text' && (
                <textarea value={mintText} onChange={e => setMintText(e.target.value)}
                  placeholder="Enter text content…"
                  style={{ width: '100%', minHeight: '100px', fontFamily: 'monospace', fontSize: '0.85rem', resize: 'vertical', boxSizing: 'border-box' }} />
              )}
            </div>

            {/* Funding UTXO selector */}
            {fundingUtxos.length > 0 && (
              <div className="form-group" style={{ marginBottom: '1rem' }}>
                <label>Mint to address</label>
                <select value={mintFundingKey} onChange={e => setMintFundingKey(e.target.value)} style={{ width: '100%', fontFamily: 'monospace', fontSize: '0.82rem' }}>
                  {fundingUtxos.map(u => (
                    <option key={`${u.txid}:${u.vout}`} value={`${u.txid}:${u.vout}`}>
                      {u.address}  —  {maskAmt(u.amount.toFixed(8))} RXD available
                    </option>
                  ))}
                </select>
                <span style={{ fontSize: '0.72rem', color: 'var(--text2)' }}>
                  Token will be minted to this address. Fee paid from the same UTXO.
                </span>
              </div>
            )}

            {/* Core fields */}
            <div className="form-row" style={{ gap: '0.75rem' }}>
              <div className="form-group" style={{ flex: 2 }}>
                <label>Name</label>
                <input value={mintName} onChange={e => setMintName(e.target.value)} placeholder="My Token" />
              </div>
              {mintType === 'ft' && (
                <div className="form-group" style={{ flex: 1 }}>
                  <label>Ticker</label>
                  <input value={mintTicker} onChange={e => setMintTicker(e.target.value.toUpperCase())} placeholder="TKN" maxLength={8} />
                </div>
              )}
            </div>

            {mintType === 'ft' && mintDeployMethod === 'direct' && (
              <div className="form-group" style={{ marginTop: '0.75rem' }}>
                <label>Initial supply (atomic units)</label>
                <input type="number" value={mintSupply} onChange={e => setMintSupply(e.target.value)} placeholder="1000000" step="1" min="1" />
                {mintSupply && <span style={{ fontSize: '0.72rem', color: 'var(--text2)' }}>
                  = {(parseInt(mintSupply) / 1e8).toFixed(8)} RXD locked in token output
                </span>}
              </div>
            )}

            {mintType === 'ft' && mintDeployMethod === 'dmint' && (
              <div style={{ marginTop: '0.75rem', padding: '0.75rem', background: 'var(--bg2)', border: '1px solid var(--border)', borderRadius: 'var(--radius)' }}>
                <div style={{ fontSize: '0.78rem', fontWeight: 600, color: 'var(--text2)', marginBottom: '0.75rem', textTransform: 'uppercase', letterSpacing: '0.05em' }}>dMint Parameters</div>

                <div className="form-row" style={{ gap: '0.75rem', marginBottom: '0.75rem' }}>
                  <div className="form-group" style={{ flex: 1 }}>
                    <label>Total mints</label>
                    <input type="number" value={dmintNumMints} onChange={e => setDmintNumMints(e.target.value)} placeholder="1000" step="1" min="1" />
                  </div>
                  <div className="form-group" style={{ flex: 1 }}>
                    <label>Reward (atomic units)</label>
                    <input type="number" value={dmintReward} onChange={e => setDmintReward(e.target.value)} placeholder="1000" step="1" min="1" />
                  </div>
                </div>

                <div className="form-row" style={{ gap: '0.75rem', marginBottom: '0.75rem' }}>
                  <div className="form-group" style={{ flex: 1 }}>
                    <label>Initial difficulty</label>
                    <input type="number" value={dmintDifficulty} onChange={e => setDmintDifficulty(e.target.value)} placeholder="10" step="1" min="1" />
                  </div>
                  <div className="form-group" style={{ flex: 1 }}>
                    <label>Premine (atomic units)</label>
                    <input type="number" value={dmintPremine} onChange={e => setDmintPremine(e.target.value)} placeholder="0" step="1" min="0" />
                  </div>
                </div>

                <div className="form-row" style={{ gap: '0.75rem', marginBottom: '0.75rem' }}>
                  <div className="form-group" style={{ flex: 1 }}>
                    <label>Algorithm</label>
                    <select value={dmintAlgorithm} onChange={e => setDmintAlgorithm(e.target.value as 'sha256d' | 'blake3' | 'k12')} style={{ width: '100%' }}>
                      <option value="blake3">BLAKE3</option>
                      <option value="sha256d">SHA-256d</option>
                      <option value="k12">KangarooTwelve</option>
                    </select>
                  </div>
                  <div className="form-group" style={{ flex: 1 }}>
                    <label>Num contracts</label>
                    <input type="number" value={dmintNumContracts} onChange={e => setDmintNumContracts(e.target.value)} placeholder="1" step="1" min="1" max="8" />
                  </div>
                </div>

                <div className="form-row" style={{ gap: '0.75rem', marginBottom: dmintDaaMode === 'asert' ? '0.75rem' : '0.25rem' }}>
                  <div className="form-group" style={{ flex: 1 }}>
                    <label>DAA mode</label>
                    <select value={dmintDaaMode} onChange={e => setDmintDaaMode(e.target.value as 'fixed' | 'asert' | 'lwma')} style={{ width: '100%' }}>
                      <option value="asert">ASERT</option>
                      <option value="lwma">LWMA</option>
                      <option value="fixed">Fixed</option>
                    </select>
                  </div>
                  {dmintDaaMode !== 'fixed' && (
                    <div className="form-group" style={{ flex: 1 }}>
                      <label>Target block time (s)</label>
                      <input type="number" value={dmintTargetBlockTime} onChange={e => setDmintTargetBlockTime(e.target.value)} placeholder="60" step="1" min="1" />
                    </div>
                  )}
                </div>

                {dmintDaaMode === 'asert' && (
                  <div className="form-group" style={{ marginBottom: '0.5rem' }}>
                    <label>ASERT half life (s)</label>
                    <input type="number" value={dmintHalfLife} onChange={e => setDmintHalfLife(e.target.value)} placeholder="240" step="1" min="1" />
                    <span style={{ fontSize: '0.72rem', color: 'var(--text2)' }}>Time for difficulty to halve or double</span>
                  </div>
                )}

                <div style={{ fontSize: '0.72rem', color: 'var(--text2)', marginTop: '0.5rem' }}>
                  Total supply: {dmintNumMints && dmintReward
                    ? ((parseInt(dmintNumMints) || 0) * (parseInt(dmintReward) || 0) + (parseInt(dmintPremine) || 0)).toLocaleString()
                    : '—'} atomic units
                  {dmintPremine && parseInt(dmintPremine) > 0 ? ` (${parseInt(dmintPremine).toLocaleString()} premine + ${((parseInt(dmintNumMints)||0)*(parseInt(dmintReward)||0)).toLocaleString()} minable)` : ''}
                </div>
              </div>
            )}

            <div className="form-group" style={{ marginTop: '0.75rem' }}>
              <label>Description</label>
              <input value={mintDesc} onChange={e => setMintDesc(e.target.value)} placeholder="A brief description…" />
            </div>

            {!(mintType === 'ft' && mintDeployMethod === 'dmint') && (
              <div className="form-group" style={{ marginTop: '0.75rem' }}>
                <label>License</label>
                <input value={mintLicense} onChange={e => setMintLicense(e.target.value)} placeholder="e.g. CC0, MIT, All rights reserved…" />
              </div>
            )}

            {/* Author — for NFT and Container tokens */}
            {(mintType === 'nft' || mintType === 'container') && (
              <div className="form-group" style={{ marginTop: '0.75rem' }}>
                <label>Author (User token)</label>
                <select value={mintAuthor} onChange={e => setMintAuthor(e.target.value)} style={{ width: '100%' }}>
                  <option value="">— None —</option>
                  {glyphTokens
                    .filter(t => tokenTypeInfo(t.isSingleton, glyphMeta.get(t.ref)).label === 'User')
                    .map(t => {
                      const m = glyphMeta.get(t.ref)
                      return <option key={t.ref} value={t.ref}>{m?.name ?? t.ref.slice(0, 16) + '…'}</option>
                    })}
                </select>
              </div>
            )}

            {/* Container — for NFT tokens */}
            {mintType === 'nft' && (
              <div className="form-group" style={{ marginTop: '0.75rem' }}>
                <label>Container</label>
                <select value={mintContainer} onChange={e => setMintContainer(e.target.value)} style={{ width: '100%' }}>
                  <option value="">— None —</option>
                  {glyphTokens
                    .filter(t => tokenTypeInfo(t.isSingleton, glyphMeta.get(t.ref)).label === 'Container')
                    .map(t => {
                      const m = glyphMeta.get(t.ref)
                      return <option key={t.ref} value={t.ref}>{m?.name ?? t.ref.slice(0, 16) + '…'}</option>
                    })}
                </select>
              </div>
            )}

            {/* Attributes — not shown for dMint FT */}
            {!(mintType === 'ft' && mintDeployMethod === 'dmint') && (
            <div style={{ marginTop: '0.75rem' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', marginBottom: '0.4rem' }}>
                <label style={{ fontSize: '0.8rem', color: 'var(--text2)' }}>Attributes</label>
                <button onClick={() => setMintAttrs(prev => [...prev, { name: '', value: '' }])}
                  style={{ fontSize: '0.72rem', padding: '1px 8px' }}>+ Add</button>
              </div>
              {mintAttrs.map((attr, i) => (
                <div key={i} className="form-row" style={{ gap: '0.5rem', marginBottom: '0.4rem' }}>
                  <input value={attr.name} onChange={e => setMintAttrs(prev => prev.map((a, j) => j === i ? { ...a, name: e.target.value } : a))}
                    placeholder="Name" style={{ flex: 1 }} />
                  <input value={attr.value} onChange={e => setMintAttrs(prev => prev.map((a, j) => j === i ? { ...a, value: e.target.value } : a))}
                    placeholder="Value" style={{ flex: 2 }} />
                  <button onClick={() => setMintAttrs(prev => prev.filter((_, j) => j !== i))}
                    style={{ padding: '0 0.5rem', color: 'var(--text2)' }}>×</button>
                </div>
              ))}
            </div>
            )}

            {/* Immutable — not shown for dMint FT */}
            {!(mintType === 'ft' && mintDeployMethod === 'dmint') && (
              <div style={{ marginTop: '0.75rem', display: 'flex', alignItems: 'center', gap: '0.75rem' }}>
                <label style={{ fontSize: '0.85rem' }}>
                  <input type="checkbox" checked={mintImmutable} onChange={e => setMintImmutable(e.target.checked)} style={{ marginRight: '0.4rem' }} />
                  Immutable
                </label>
                <span style={{ fontSize: '0.75rem', color: 'var(--text2)' }}>
                  {mintImmutable ? 'Metadata cannot be changed after minting' : 'Token owner can update metadata'}
                </span>
              </div>
            )}

            {/* Fee estimate + action row */}
            <div style={{ marginTop: '1.25rem', display: 'flex', alignItems: 'center', gap: '0.75rem', flexWrap: 'wrap' }}>
              <button onClick={handleEstimateFee} disabled={mintRunning || !mintName}
                style={{ fontSize: '0.85rem', padding: '0.35rem 1rem' }}>
                Estimate Fee
              </button>
              {mintFeeEstimate && (
                <span style={{ fontSize: '0.82rem', color: 'var(--text2)' }}>
                  ≈ <span style={{ color: 'var(--text)', fontFamily: 'monospace' }}>
                    {(mintFeeEstimate.feeSats / 1e8).toFixed(8)} RXD
                  </span>
                  {' '}fee &nbsp;·&nbsp; {mintFeeEstimate.txBytes.toLocaleString()} bytes
                  {' '}@ {(mintFeeEstimate.feeRatePerKb / 1e8).toFixed(4)} RXD/kB
                </span>
              )}
            </div>

            <button className="primary" style={{ marginTop: '0.75rem' }}
              onClick={handleMintToken}
              disabled={
                mintRunning || !mintName ||
                (mintType === 'ft' && mintDeployMethod === 'direct' && (!mintTicker || !mintSupply)) ||
                (mintType === 'ft' && mintDeployMethod === 'dmint'  && (!mintTicker || !dmintNumMints || !dmintReward))
              }>
              {mintRunning ? 'Signing…'
                : mintType === 'ft' && mintDeployMethod === 'dmint' ? 'Deploy dMint FT'
                : mintType === 'nft' ? 'Mint NFT'
                : mintType === 'ft'  ? 'Mint FT'
                : mintType === 'user' ? 'Mint User Token'
                : 'Mint Container'}
            </button>

          </div>
          )}
        </>
      )}

      {/* ── SECURITY ── */}
      {tab === 'security' && (
        <>
          {summary && !summary.encrypted && (
            <div className="card" style={{
              borderColor: 'rgba(245,158,11,0.5)',
              borderLeftWidth: '3px',
              display: 'flex', gap: '0.85rem', alignItems: 'flex-start',
            }}>
              <span style={{ fontSize: '1.3rem', lineHeight: 1, paddingTop: '0.05rem', flexShrink: 0 }}>⚠️</span>
              <div>
                <div style={{ fontWeight: 600, color: '#f59e0b', marginBottom: '0.3rem' }}>Wallet is not encrypted</div>
                <div style={{ fontSize: '0.85rem', color: 'var(--text2)', lineHeight: 1.55 }}>
                  Anyone with access to the wallet file can spend your funds without needing a password.
                  Encrypt your wallet with a passphrase to protect against unauthorised access.
                </div>
              </div>
            </div>
          )}

          {summary && summary.encrypted && (
            <div className="card">
              <h2>Lock / Unlock</h2>
              <div style={{ marginBottom: '0.75rem' }}>
                <span className={`badge ${isLocked ? 'red' : 'green'}`}>{isLocked ? 'Locked' : 'Unlocked'}</span>
                {isUnlocked && unlockUntil && (
                  <span style={{ color: 'var(--text2)', marginLeft: '0.75rem', fontSize: '0.85rem' }}>
                    locks in {fmtCountdown(secondsLeft)}
                  </span>
                )}
              </div>
              {isLocked && (
                <div className="form-row">
                  <div className="form-group" style={{ flex: 1 }}>
                    <label>Passphrase</label>
                    <input type="password" value={unlockPw} onChange={e => setUnlockPw(e.target.value)}
                      onKeyDown={e => e.key === 'Enter' && handleUnlock()} placeholder="Wallet passphrase" />
                  </div>
                  <div className="form-group">
                    <label>Duration</label>
                    <select value={unlockDuration} onChange={e => setUnlockDuration(Number(e.target.value))}>
                      {UNLOCK_DURATIONS.map(d => <option key={d.seconds} value={d.seconds}>{d.label}</option>)}
                    </select>
                  </div>
                  <button className="primary" onClick={handleUnlock} style={{ alignSelf: 'flex-end' }}>Unlock</button>
                </div>
              )}
              {isUnlocked && <button onClick={handleLock}>Lock Wallet</button>}
            </div>
          )}

          {summary && !summary.encrypted && (
            <div className="card">
              <h2>Encrypt Wallet</h2>
              <p style={{ color: 'var(--text2)', fontSize: '0.85rem', marginBottom: '0.75rem' }}>
                Encrypts the wallet with a passphrase. radiantd will shut down after encrypting — remember your passphrase, there is no recovery.
              </p>
              <div className="form-row">
                <div className="form-group" style={{ flex: 1 }}>
                  <label>Passphrase</label>
                  <input type="password" value={encryptPw} onChange={e => setEncryptPw(e.target.value)} placeholder="New passphrase" />
                </div>
                <div className="form-group" style={{ flex: 1 }}>
                  <label>Confirm passphrase</label>
                  <input type="password" value={encryptPw2} onChange={e => setEncryptPw2(e.target.value)} placeholder="Confirm" />
                </div>
                <button className="primary" onClick={handleEncrypt} disabled={!encryptPw || encryptPw !== encryptPw2} style={{ alignSelf: 'flex-end' }}>Encrypt</button>
              </div>
            </div>
          )}

          {summary && summary.encrypted && (
            <div className="card">
              <h2>Change Passphrase</h2>
              <div className="form-group">
                <label>Current passphrase</label>
                <input type="password" value={oldPw} onChange={e => setOldPw(e.target.value)} placeholder="Current passphrase" />
              </div>
              <div className="form-row" style={{ marginTop: '0.75rem' }}>
                <div className="form-group" style={{ flex: 1 }}>
                  <label>New passphrase</label>
                  <input type="password" value={newPw} onChange={e => setNewPw(e.target.value)} placeholder="New passphrase" />
                </div>
                <div className="form-group" style={{ flex: 1 }}>
                  <label>Confirm new passphrase</label>
                  <input type="password" value={newPw2} onChange={e => setNewPw2(e.target.value)} placeholder="Confirm" />
                </div>
                <button className="primary" onClick={handleChangePassphrase}
                  disabled={!oldPw || !newPw || newPw !== newPw2} style={{ alignSelf: 'flex-end' }}>Change</button>
              </div>
            </div>
          )}

          <SeedImport walletName={walletName} onImported={() => walletName !== null && loadAddresses(walletName)} />

          <div className="card">
            <h2>Backup Wallet</h2>
            <p style={{ color: 'var(--text2)', fontSize: '0.85rem', marginBottom: '0.75rem' }}>
              Downloads a copy of the wallet file to your device. The node creates
              a safe backup first — no private keys are ever sent in plaintext.
            </p>
            <button className="primary" onClick={handleBackup} disabled={backupBusy}>
              {backupBusy ? 'Preparing backup…' : 'Download backup'}
            </button>
          </div>
        </>
      )}

      {txModal && (
        <div
          onClick={() => setTxModal(null)}
          style={{ position: 'fixed', inset: 0, background: 'rgba(0,0,0,0.65)', zIndex: 9998, display: 'flex', alignItems: 'center', justifyContent: 'center' }}
        >
          <div
            onClick={e => e.stopPropagation()}
            style={{ background: 'var(--bg2)', border: '1px solid var(--border)', borderRadius: 'var(--radius)', padding: '1.5rem', maxWidth: '560px', width: '92vw', maxHeight: '75vh', display: 'flex', flexDirection: 'column', gap: '0.75rem' }}
          >
            <p style={{ fontSize: '0.75rem', color: 'var(--text2)', textTransform: 'uppercase', letterSpacing: '0.06em', margin: 0 }}>
              Transactions · {txModal.label || <code style={{ textTransform: 'none' }}>{txModal.address.slice(0, 12)}…</code>}
            </p>
            <div style={{ overflowY: 'auto', flex: 1, paddingRight: '0.5rem' }}>
              {(txModal.txids ?? []).length === 0 ? (
                <p style={{ color: 'var(--text2)', fontSize: '0.85rem', textAlign: 'center', padding: '1rem 0' }}>No transactions</p>
              ) : ([...new Set(txModal.txids ?? [])]).map(txid => (
                <div key={txid} style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', padding: '0.4rem 0', borderBottom: '1px solid var(--border)' }}>
                  <a
                    href={explorerTxUrl(txid)}
                    target="_blank"
                    rel="noopener noreferrer"
                    style={{ fontFamily: 'monospace', fontSize: '0.72rem', color: 'var(--accent)', flex: 1 }}
                    title={txid}
                  >
                    {txid.slice(0, 12)}…{txid.slice(-12)}
                  </a>
                  <button
                    onClick={() => handleCopy(txid)}
                    style={{ padding: '2px 6px', lineHeight: 0, color: copied === txid ? '#22c55e' : undefined, flexShrink: 0 }}
                    title={copied === txid ? 'Copied!' : 'Copy txid'}
                  >
                    {copied === txid ? <CopiedIcon size={13} /> : <CopyIcon size={13} />}
                  </button>
                </div>
              ))}
            </div>
            <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
              <button onClick={() => setTxModal(null)} style={{ fontSize: '0.8rem', padding: '0.3rem 0.9rem' }}>Close</button>
            </div>
          </div>
        </div>
      )}

      {qrAddr && (
        <div
          onClick={() => setQrAddr(null)}
          style={{ position: 'fixed', inset: 0, background: 'rgba(0,0,0,0.65)', zIndex: 9998, display: 'flex', alignItems: 'center', justifyContent: 'center' }}
        >
          <div
            onClick={e => e.stopPropagation()}
            style={{ background: 'var(--bg2)', border: '1px solid var(--border)', borderRadius: 'var(--radius)', padding: '1.5rem', textAlign: 'center', maxWidth: '300px', width: '90vw' }}
          >
            <p style={{ fontSize: '0.75rem', color: 'var(--text2)', textTransform: 'uppercase', letterSpacing: '0.06em', marginBottom: '1rem' }}>
              Receive Address
            </p>
            {qrDataUrl
              ? <img src={qrDataUrl} alt="QR code" style={{ width: 220, height: 220, imageRendering: 'pixelated', borderRadius: 4 }} />
              : <div style={{ width: 220, height: 220, background: 'var(--bg3)', borderRadius: 4, margin: '0 auto', display: 'flex', alignItems: 'center', justifyContent: 'center', color: 'var(--text2)', fontSize: '0.8rem' }}>Generating…</div>
            }
            <p style={{ marginTop: '0.85rem', fontFamily: 'monospace', fontSize: '0.72rem', color: 'var(--text)', wordBreak: 'break-all', lineHeight: 1.5 }}>
              {qrAddr}
            </p>
            <div style={{ display: 'flex', gap: '0.5rem', justifyContent: 'center', marginTop: '0.85rem' }}>
              <button onClick={() => handleCopy(qrAddr)} style={{ padding: '0.3rem 0.6rem', lineHeight: 0, color: copied === qrAddr ? '#22c55e' : undefined }} title={copied === qrAddr ? 'Copied!' : 'Copy address'}>
                {copied === qrAddr ? <CopiedIcon size={16} /> : <CopyIcon size={16} />}
              </button>
              <button onClick={() => setQrAddr(null)} style={{ fontSize: '0.8rem', padding: '0.3rem 0.9rem' }}>
                Close
              </button>
            </div>
          </div>
        </div>
      )}

      <ToastContainer toasts={toasts} onDismiss={dismissToast} />
    </div>
  )
}
