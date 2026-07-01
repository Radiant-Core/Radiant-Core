import { useState, useEffect } from 'react'
import { api, loadCachedSettings, cacheSettings, WebUISettings, EXPLORER_DEFAULTS, explorerTxUrl, explorerAddressUrl, explorerBlockUrl } from '../lib/api'

export default function SettingsPage() {
  const [settings, setSettings] = useState<WebUISettings>(loadCachedSettings)
  const [txTpl,      setTxTpl]      = useState(loadCachedSettings().explorerTxTemplate      ?? EXPLORER_DEFAULTS.tx)
  const [addressTpl, setAddressTpl] = useState(loadCachedSettings().explorerAddressTemplate ?? EXPLORER_DEFAULTS.address)
  const [blockTpl,   setBlockTpl]   = useState(loadCachedSettings().explorerBlockTemplate   ?? EXPLORER_DEFAULTS.block)
  const [status, setStatus] = useState<'idle' | 'saving' | 'saved' | 'error'>('idle')
  const [errorMsg, setErrorMsg] = useState('')

  useEffect(() => {
    api.getSettings()
      .then(s => {
        cacheSettings(s)
        setSettings(s)
        setTxTpl(s.explorerTxTemplate ?? EXPLORER_DEFAULTS.tx)
        setAddressTpl(s.explorerAddressTemplate ?? EXPLORER_DEFAULTS.address)
        setBlockTpl(s.explorerBlockTemplate ?? EXPLORER_DEFAULTS.block)
      })
      .catch(() => {})
  }, [])

  async function handleSave(e: React.FormEvent) {
    e.preventDefault()
    const next: WebUISettings = {
      ...settings,
      explorerTxTemplate:      txTpl.trim()      || undefined,
      explorerAddressTemplate: addressTpl.trim() || undefined,
      explorerBlockTemplate:   blockTpl.trim()   || undefined,
    }
    setStatus('saving')
    setErrorMsg('')
    try {
      const saved = await api.saveSettings(next)
      cacheSettings(saved)
      setSettings(saved)
      setStatus('saved')
      setTimeout(() => setStatus('idle'), 2000)
    } catch (err) {
      setStatus('error')
      setErrorMsg(err instanceof Error ? err.message : String(err))
    }
  }

  const inputStyle = {
    width: '100%', boxSizing: 'border-box' as const,
    padding: '0.5rem 0.65rem',
    background: 'var(--bg2)', border: '1px solid var(--border)',
    borderRadius: 6, color: 'var(--text)', fontSize: '0.82rem',
    outline: 'none', fontFamily: 'monospace',
  }
  const labelStyle = { display: 'block' as const, fontSize: '0.78rem', color: 'var(--text2)', marginBottom: '0.3rem' }

  const previewTx      = explorerTxUrl('a1b2c3…')
  const previewAddress = explorerAddressUrl('14XmXG…vgx1i')
  const previewBlock   = explorerBlockUrl(442454)

  return (
    <div style={{ maxWidth: 600, margin: '2rem auto', padding: '0 1rem' }}>
      <h2 style={{ fontSize: '1.15rem', fontWeight: 700, marginBottom: '1.5rem', color: 'var(--text)' }}>Settings</h2>

      <form onSubmit={handleSave}>
        <section style={{ background: 'var(--bg3)', border: '1px solid var(--border)', borderRadius: 'var(--radius)', padding: '1.25rem', marginBottom: '1.25rem' }}>
          <h3 style={{ fontSize: '0.85rem', fontWeight: 600, color: 'var(--text2)', textTransform: 'uppercase', letterSpacing: '0.05em', marginBottom: '0.25rem' }}>
            Block Explorer
          </h3>
          <p style={{ fontSize: '0.75rem', color: 'var(--text2)', marginBottom: '1rem' }}>
            URL templates for linking to transactions, addresses, and blocks. Use <code style={{ background: 'var(--bg2)', padding: '0 3px', borderRadius: 3 }}>{'{txid}'}</code>, <code style={{ background: 'var(--bg2)', padding: '0 3px', borderRadius: 3 }}>{'{address}'}</code>, <code style={{ background: 'var(--bg2)', padding: '0 3px', borderRadius: 3 }}>{'{height}'}</code> as placeholders.
          </p>

          <div style={{ display: 'flex', flexDirection: 'column', gap: '0.75rem' }}>
            <div>
              <label style={labelStyle}>Transaction URL</label>
              <input
                type="text"
                value={txTpl}
                onChange={e => { setTxTpl(e.target.value); setStatus('idle') }}
                style={inputStyle}
              />
            </div>
            <div>
              <label style={labelStyle}>Address URL</label>
              <input
                type="text"
                value={addressTpl}
                onChange={e => { setAddressTpl(e.target.value); setStatus('idle') }}
                style={inputStyle}
              />
            </div>
            <div>
              <label style={labelStyle}>Block URL</label>
              <input
                type="text"
                value={blockTpl}
                onChange={e => { setBlockTpl(e.target.value); setStatus('idle') }}
                style={inputStyle}
              />
            </div>
          </div>

          <div style={{ marginTop: '1rem', background: 'var(--bg2)', borderRadius: 6, padding: '0.65rem 0.75rem' }}>
            <div style={{ fontSize: '0.72rem', fontWeight: 600, color: 'var(--text2)', marginBottom: '0.4rem', textTransform: 'uppercase', letterSpacing: '0.04em' }}>
              Preview
            </div>
            {[previewTx, previewAddress, previewBlock].map((url, i) => (
              <div key={i} style={{ fontFamily: 'monospace', fontSize: '0.72rem', color: 'var(--text2)', lineHeight: 1.7, wordBreak: 'break-all' }}>{url}</div>
            ))}
          </div>
        </section>

        <div style={{ display: 'flex', alignItems: 'center', gap: '0.75rem' }}>
          <button
            type="submit"
            disabled={status === 'saving'}
            style={{
              padding: '0.5rem 1.25rem',
              background: 'var(--accent)', color: '#000',
              border: 'none', borderRadius: 6,
              fontWeight: 600, fontSize: '0.85rem',
              cursor: status === 'saving' ? 'not-allowed' : 'pointer',
              opacity: status === 'saving' ? 0.7 : 1,
            }}
          >
            {status === 'saving' ? 'Saving…' : 'Save Settings'}
          </button>
          {status === 'saved' && (
            <span style={{ fontSize: '0.82rem', color: '#22c55e', fontWeight: 500 }}>Saved</span>
          )}
          {status === 'error' && (
            <span style={{ fontSize: '0.82rem', color: '#ef4444' }}>{errorMsg || 'Failed to save'}</span>
          )}
        </div>
      </form>
    </div>
  )
}
