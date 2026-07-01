import { useState } from 'react'
import { mnemonicToSeedSync, validateMnemonic } from '@scure/bip39'
import { wordlist } from '@scure/bip39/wordlists/english.js'
import { HDKey } from '@scure/bip32'
import { sha256 } from '@noble/hashes/sha2.js'
import { ripemd160 } from '@noble/hashes/legacy.js'
import { api } from '../lib/api'

// Base58 encoder (no external lib needed — BigInt is universal)
const B58_ALPHA = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
function base58Encode(bytes: Uint8Array): string {
  let n = BigInt('0x' + Array.from(bytes, b => b.toString(16).padStart(2, '0')).join(''))
  let out = ''
  while (n > 0n) { out = B58_ALPHA[Number(n % 58n)] + out; n /= 58n }
  for (const b of bytes) { if (b !== 0) break; out = '1' + out }
  return out
}

// WIF: 0x80 + privkey (32 B) + 0x01 (compressed) + 4-byte SHA256d checksum
function toWIF(privKey: Uint8Array): string {
  const payload = new Uint8Array(34)
  payload[0] = 0x80
  payload.set(privKey, 1)
  payload[33] = 0x01
  const checksum = sha256(sha256(payload)).subarray(0, 4)
  const full = new Uint8Array(38)
  full.set(payload)
  full.set(checksum, 34)
  return base58Encode(full)
}

// P2PKH address from compressed public key (Radiant mainnet version byte 0x00, same as Bitcoin)
function toAddress(pubKey: Uint8Array): string {
  const hash160 = ripemd160(sha256(pubKey))
  const payload = new Uint8Array(21)
  payload[0] = 0x00
  payload.set(hash160, 1)
  const checksum = sha256(sha256(payload)).subarray(0, 4)
  const full = new Uint8Array(25)
  full.set(payload)
  full.set(checksum, 21)
  return base58Encode(full)
}

type CoinType = '512' | '0'

interface DerivedEntry {
  path: string
  address: string
  wif: string
  selected: boolean
}

interface Props {
  walletName: string | null
  onImported: () => void
}

export default function SeedImport({ walletName, onImported }: Props) {
  const [open, setOpen]           = useState(false)
  const [mnemonic, setMnemonic]   = useState('')
  const [passphrase, setPassphrase] = useState('')
  const [coinType, setCoinType]   = useState<CoinType>('512')
  const [count, setCount]         = useState(2)
  const [derived, setDerived]         = useState<DerivedEntry[]>([])
  const [label, setLabel]             = useState('')
  const [error, setError]             = useState('')
  const [msg, setMsg]                 = useState('')
  const [importing, setImporting]     = useState(false)
  const [mnemonicFocused, setMnemonicFocused] = useState(false)

  const reset = () => {
    setOpen(false); setMnemonic(''); setPassphrase('')
    setDerived([]); setError(''); setMsg('')
  }

  const handleDerive = () => {
    setError(''); setMsg(''); setDerived([])
    const words = mnemonic.trim().toLowerCase().replace(/\s+/g, ' ')
    if (!validateMnemonic(words, wordlist)) {
      setError('Invalid mnemonic — check spelling and word count (12 or 24 words)')
      return
    }
    try {
      const seed = mnemonicToSeedSync(words, passphrase)
      const root = HDKey.fromMasterSeed(seed)
      const entries: DerivedEntry[] = []
      for (let i = 0; i < Math.max(1, Math.min(count, 20)); i++) {
        const path = `m/44'/${coinType}'/0'/0/${i}`
        const child = root.derive(path)
        if (!child.privateKey || !child.publicKey) { setError('Failed to derive key at ' + path); return }
        entries.push({ path, address: toAddress(child.publicKey), wif: toWIF(child.privateKey), selected: true })
      }
      setDerived(entries)
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : 'Derivation failed')
    }
  }

  const toggle = (i: number) => setDerived(d => d.map((k, j) => j === i ? { ...k, selected: !k.selected } : k))
  const allSelected = derived.every(k => k.selected)
  const toggleAll = () => setDerived(d => d.map(k => ({ ...k, selected: !allSelected })))

  const handleImport = async () => {
    const toImport = derived.filter(k => k.selected)
    if (!toImport.length) { setError('Select at least one key to import'); return }
    setImporting(true); setError(''); setMsg('')
    let n = 0
    for (const key of toImport) {
      try {
        const res = await api.rpc('importprivkey', [key.wif, label || 'seed-import', false])
        if (res.error) throw new Error(
          typeof res.error === 'object' && res.error !== null
            ? String((res.error as Record<string, unknown>).message ?? res.error)
            : String(res.error)
        )
        n++
      } catch (e: unknown) {
        setError(`${key.path}: ${e instanceof Error ? e.message : String(e)}`)
        setImporting(false)
        return
      }
    }
    setMsg(`Imported ${n} key${n !== 1 ? 's' : ''}. Run rescanblockchain to update balances.`)
    setImporting(false)
    setMnemonic('')
    setPassphrase('')
    setDerived([])
    onImported()
  }

  if (!open) {
    return (
      <div className="card">
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <h2>Import from Seed Phrase</h2>
          <button onClick={() => setOpen(true)} style={{ fontSize: '0.8rem' }}>Open</button>
        </div>
        <p style={{ color: 'var(--text2)', fontSize: '0.8rem', marginTop: '0.25rem' }}>
          Derive private keys from a BIP-39 mnemonic and import them into the wallet.
        </p>
      </div>
    )
  }

  return (
    <div className="card">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.75rem' }}>
        <h2>Import from Seed Phrase</h2>
        <button onClick={reset} style={{ fontSize: '0.8rem' }}>Close</button>
      </div>

      <div style={{
        background: 'rgba(0,168,222,0.08)',
        border: '1px solid rgba(0,168,222,0.25)',
        borderRadius: 'var(--radius)',
        padding: '0.5rem 0.75rem',
        fontSize: '0.8rem',
        color: 'var(--text2)',
        marginBottom: '0.75rem',
      }}>
        Keys are derived <strong style={{ color: 'var(--text)' }}>locally in your browser</strong>.
        Only the WIF private key is sent to the node via <code>importprivkey</code>.
      </div>

      {error && <p className="error-text" style={{ marginBottom: '0.5rem' }}>{error}</p>}
      {msg   && <p className="success-text" style={{ marginBottom: '0.5rem' }}>{msg}</p>}

      <div className="form-group" style={{ marginBottom: '0.75rem' }}>
        <label>Mnemonic (12 or 24 words)</label>
        <textarea
          value={mnemonic}
          onChange={e => setMnemonic(e.target.value)}
          onFocus={() => setMnemonicFocused(true)}
          onBlur={() => setMnemonicFocused(false)}
          placeholder="word1 word2 word3 … word12"
          rows={3}
          style={{
            fontFamily: 'monospace', fontSize: '0.85rem', resize: 'vertical',
            filter: (!mnemonicFocused && mnemonic) ? 'blur(4px)' : 'none',
            transition: 'filter 0.15s ease',
          }}
          spellCheck={false}
          autoComplete="off"
          autoCorrect="off"
        />
      </div>

      <div className="form-row" style={{ marginBottom: '0.75rem', flexWrap: 'wrap' }}>
        <div className="form-group" style={{ minWidth: '240px', flex: 2 }}>
          <label>Coin type / derivation path</label>
          <select value={coinType} onChange={e => setCoinType(e.target.value as CoinType)}>
            <option value="512">512 — Radiant  m/44'/512'/0'/0/N</option>
            <option value="0">0 — Legacy Bitcoin  m/44'/0'/0'/0/N</option>
          </select>
        </div>
        <div className="form-group" style={{ maxWidth: '90px' }}>
          <label>Count</label>
          <input
            type="number" min={1} max={20} value={count}
            onChange={e => setCount(Math.max(1, Math.min(20, Number(e.target.value))))}
          />
        </div>
        <div className="form-group" style={{ flex: 2 }}>
          <label>BIP-39 passphrase (optional)</label>
          <input
            type="password" value={passphrase}
            onChange={e => setPassphrase(e.target.value)}
            placeholder="Leave blank if none"
            autoComplete="new-password"
          />
        </div>
        <button
          className="primary"
          onClick={handleDerive}
          disabled={!mnemonic.trim()}
          style={{ alignSelf: 'flex-end' }}
        >
          Derive
        </button>
      </div>

      {derived.length > 0 && (
        <>
          <table className="table" style={{ marginBottom: '0.75rem' }}>
            <thead>
              <tr>
                <th style={{ width: '32px' }}>
                  <input
                    type="checkbox"
                    checked={allSelected}
                    onChange={toggleAll}
                    title="Select all"
                  />
                </th>
                <th>Derivation path</th>
                <th>Derived address</th>
              </tr>
            </thead>
            <tbody>
              {derived.map((k, i) => (
                <tr key={k.path} onClick={() => toggle(i)} style={{ cursor: 'pointer' }}>
                  <td onClick={e => e.stopPropagation()}>
                    <input type="checkbox" checked={k.selected} onChange={() => toggle(i)} />
                  </td>
                  <td>
                    <code style={{ fontSize: '0.8rem', color: 'var(--accent)' }}>{k.path}</code>
                  </td>
                  <td onClick={e => e.stopPropagation()}>
                    <code style={{ fontSize: '0.8rem', wordBreak: 'break-all' }}>{k.address}</code>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>

          <div className="form-row">
            <div className="form-group" style={{ flex: 1 }}>
              <label>Label for imported keys</label>
              <input
                value={label}
                onChange={e => setLabel(e.target.value)}
                placeholder="seed-import"
              />
            </div>
            <button
              className="primary"
              onClick={handleImport}
              disabled={importing || !derived.some(k => k.selected) || walletName === null}
              style={{ alignSelf: 'flex-end' }}
            >
              {importing ? 'Importing…' : `Import ${derived.filter(k => k.selected).length} key${derived.filter(k => k.selected).length !== 1 ? 's' : ''}`}
            </button>
          </div>
        </>
      )}
    </div>
  )
}
