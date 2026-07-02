import { useState, useEffect, useCallback } from 'react'
import { api, Peer, BanEntry } from '../lib/api'
import './PeersPage.css'

export default function PeersPage({ refreshKey = 0 }: { refreshKey?: number }) {
  const [peers, setPeers]       = useState<Peer[]>([])
  const [banned, setBanned]     = useState<BanEntry[]>([])
  const [tab, setTab]           = useState<'peers' | 'banned'>('peers')
  const [expandedId, setExpandedId] = useState<number | null>(null)
  const [addNode, setAddNode]   = useState('')
  const [banAddr, setBanAddr]   = useState('')
  const [error, setError]       = useState('')
  const [msg, setMsg]           = useState('')

  const load = useCallback(async () => {
    try {
      const [p, b] = await Promise.all([api.nodePeers(), api.nodeBanned()])
      setPeers(p.peers)
      setBanned(b.banned)
      setError('')
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : 'Failed to load peer data')
    }
  }, [])

  useEffect(() => { load(); const id = setInterval(load, 15000); return () => clearInterval(id) }, [load])
  useEffect(() => { if (refreshKey > 0) load() }, [refreshKey]) // eslint-disable-line

  const fmtBytes = (b: number) => {
    if (b > 1e9) return `${(b / 1e9).toFixed(1)} GB`
    if (b > 1e6) return `${(b / 1e6).toFixed(1)} MB`
    if (b > 1e3) return `${(b / 1e3).toFixed(1)} KB`
    return `${b} B`
  }

  const fmtPing = (ms: number) => {
    if (ms < 0) return 'N/A'
    if (ms >= 1000) return `${(ms / 1000).toFixed(1)} s`
    return `${Math.round(ms)} ms`
  }

  const fmtDuration = (since: number) => {
    const sec = Math.floor(Date.now() / 1000) - since
    if (sec < 60)   return `${sec}s`
    const m = Math.floor(sec / 60)
    if (m < 60)     return `${m}m`
    const h = Math.floor(m / 60)
    const rm = m % 60
    if (h < 48)     return `${h}h ${rm}m`
    return `${Math.floor(h / 24)}d ${h % 24}h`
  }

  const fmtTime = (ts: number) => ts > 0 ? new Date(ts * 1000).toLocaleString() : 'Never'

  const extractHost = (addr: string) => {
    if (addr.startsWith('[')) return addr.slice(1, addr.lastIndexOf(']'))
    const i = addr.lastIndexOf(':')
    return i > 0 ? addr.slice(0, i) : addr
  }

  const disconnect = async (id: number) => {
    try { await api.peerDisconnect(id); load() } catch (e: unknown) { setError(e instanceof Error ? e.message : 'Failed') }
  }

  const ban = async (addr: string) => {
    try { await api.peerBan(addr); setMsg(`Banned ${addr}`); load() } catch (e: unknown) { setError(e instanceof Error ? e.message : 'Failed') }
  }

  const unban = async (addr: string) => {
    try { await api.peerUnban(addr); setMsg(`Unbanned ${addr}`); load() } catch (e: unknown) { setError(e instanceof Error ? e.message : 'Failed') }
  }

  const toggle = (id: number) => setExpandedId(prev => prev === id ? null : id)

  return (
    <div>
      <div style={{ display: 'flex', gap: '0.5rem', marginBottom: '1rem', alignItems: 'center' }}>
        <h1 style={{ fontSize: '1.1rem', fontWeight: 700, flex: 1 }}>Network</h1>
        <button className={tab === 'peers'  ? 'primary' : ''} onClick={() => setTab('peers')}>Peers ({peers.length})</button>
        <button className={tab === 'banned' ? 'primary' : ''} onClick={() => setTab('banned')}>Banned ({banned.length})</button>
        <button onClick={load}>Refresh</button>
      </div>

      {error && <p className="error-text"   style={{ marginBottom: '0.75rem' }}>{error}</p>}
      {msg   && <p className="success-text" style={{ marginBottom: '0.75rem' }}>{msg}</p>}

      {tab === 'peers' && (
        <>
          <div className="card">
            <div className="form-row">
              <div className="form-group" style={{ flex: 1 }}>
                <label>Add node</label>
                <input value={addNode} onChange={e => setAddNode(e.target.value)} placeholder="192.0.2.1:7333" />
              </div>
              <button onClick={async () => {
                try { await api.peerAdd(addNode); setMsg('Node added'); setAddNode(''); load() }
                catch (e: unknown) { setError(e instanceof Error ? e.message : 'Failed') }
              }}>Add</button>
            </div>
          </div>

          <div className="card" style={{ padding: 0, overflow: 'hidden' }}>
            <table className="table peers-table">
              <thead>
                <tr className="peers-head">
                  <th style={{ width: '1.5rem' }}></th>
                  <th>Address</th>
                  <th>Version</th>
                  <th>Direction</th>
                  <th>Ping</th>
                  <th>Height</th>
                  <th>Connected</th>
                  <th style={{ width: '120px' }}>Actions</th>
                </tr>
              </thead>
              <tbody>
                {peers.map(p => {
                  const expanded = expandedId === p.id
                  return (
                    <>
                      <tr key={p.id} className={`peers-row${expanded ? ' peers-row-expanded' : ''}`} onClick={() => toggle(p.id)}>
                        <td className="peers-expand-cell">
                          <span className={`peers-arrow${expanded ? ' open' : ''}`}>▶</span>
                        </td>
                        <td style={{ fontFamily: 'monospace', fontSize: '0.82rem' }}>{p.addr}</td>
                        <td className="peers-version">{p.subver || '—'}</td>
                        <td><span className={`badge ${p.inbound ? 'amber' : 'blue'}`}>{p.inbound ? 'Inbound' : 'Outbound'}</span></td>
                        <td style={{ color: 'var(--text2)', fontSize: '0.82rem' }}>{fmtPing(p.ping ?? -1)}</td>
                        <td style={{ color: 'var(--text2)', fontSize: '0.82rem' }}>{p.startingheight != null ? p.startingheight.toLocaleString() : '—'}</td>
                        <td style={{ color: 'var(--text2)', fontSize: '0.82rem' }}>{fmtDuration(p.conntime)}</td>
                        <td onClick={e => e.stopPropagation()}>
                          <div style={{ display: 'flex', gap: '0.3rem' }}>
                            <button className="peers-action-btn" onClick={() => disconnect(p.id)}>Disc</button>
                            <button className="peers-action-btn peers-ban-btn" onClick={() => ban(extractHost(p.addr))}>Ban</button>
                          </div>
                        </td>
                      </tr>
                      {expanded && (
                        <tr key={`${p.id}-detail`} className="peers-detail-row">
                          <td></td>
                          <td colSpan={7}>
                            <div className="peers-detail-grid">
                              <div className="peers-detail-item">
                                <span className="peers-detail-label">Received</span>
                                <span className="peers-detail-value">{fmtBytes(p.bytesrecv)}</span>
                              </div>
                              <div className="peers-detail-item">
                                <span className="peers-detail-label">Sent</span>
                                <span className="peers-detail-value">{fmtBytes(p.bytessent)}</span>
                              </div>
                              <div className="peers-detail-item">
                                <span className="peers-detail-label">Last Send</span>
                                <span className="peers-detail-value">{fmtTime(p.lastsend)}</span>
                              </div>
                              <div className="peers-detail-item">
                                <span className="peers-detail-label">Last Receive</span>
                                <span className="peers-detail-value">{fmtTime(p.lastrecv)}</span>
                              </div>
                              <div className="peers-detail-item">
                                <span className="peers-detail-label">Protocol</span>
                                <span className="peers-detail-value">{p.version ?? '—'}</span>
                              </div>
                              {p.addrlocal && (
                                <div className="peers-detail-item">
                                  <span className="peers-detail-label">Local Address</span>
                                  <span className="peers-detail-value" style={{ fontFamily: 'monospace', fontSize: '0.8rem' }}>{p.addrlocal}</span>
                                </div>
                              )}
                            </div>
                          </td>
                        </tr>
                      )}
                    </>
                  )
                })}
                {peers.length === 0 && (
                  <tr><td colSpan={8} style={{ color: 'var(--text2)', textAlign: 'center', padding: '1.5rem' }}>No connected peers</td></tr>
                )}
              </tbody>
            </table>
          </div>
        </>
      )}

      {tab === 'banned' && (
        <>
          <div className="card">
            <div className="form-row">
              <div className="form-group" style={{ flex: 1 }}>
                <label>Ban address/subnet</label>
                <input value={banAddr} onChange={e => setBanAddr(e.target.value)} placeholder="192.0.2.0/24" />
              </div>
              <button onClick={() => ban(banAddr)}>Ban</button>
            </div>
          </div>

          <div className="card">
            <table className="table">
              <thead><tr><th>Address</th><th>Banned until</th><th>Actions</th></tr></thead>
              <tbody>
                {banned.map((b, i) => (
                  <tr key={i}>
                    <td style={{ fontFamily: 'monospace' }}>{b.address}</td>
                    <td style={{ color: 'var(--text2)', fontSize: '0.85rem' }}>{new Date(b.banned_until * 1000).toLocaleString()}</td>
                    <td><button className="peers-action-btn" onClick={() => unban(b.address)}>Unban</button></td>
                  </tr>
                ))}
                {banned.length === 0 && (
                  <tr><td colSpan={3} style={{ color: 'var(--text2)', textAlign: 'center', padding: '1.5rem' }}>No banned peers</td></tr>
                )}
              </tbody>
            </table>
          </div>
        </>
      )}
    </div>
  )
}
