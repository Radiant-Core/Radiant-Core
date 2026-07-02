import { useState, useEffect, useCallback, useRef } from 'react'
import { api, Peer, BanEntry } from '../lib/api'
import './PeersPage.css'

type TrafficPoint = { recv: number; sent: number }
const WINDOWS = { '5m': 300, '15m': 900, '30m': 1800 } as const
type WindowKey = keyof typeof WINDOWS

function fmtBytes(b: number) {
  if (b >= 1e9) return `${(b / 1e9).toFixed(2)} GB`
  if (b >= 1e6) return `${(b / 1e6).toFixed(2)} MB`
  if (b >= 1e3) return `${(b / 1e3).toFixed(1)} KB`
  return `${b} B`
}
function fmtRate(bps: number) {
  if (bps >= 1024 * 1024) return `${(bps / 1024 / 1024).toFixed(2)} MB/s`
  return `${(bps / 1024).toFixed(1)} kB/s`
}

export default function PeersPage({ refreshKey = 0 }: { refreshKey?: number }) {
  const [peers, setPeers]       = useState<Peer[]>([])
  const [banned, setBanned]     = useState<BanEntry[]>([])
  const [tab, setTab]           = useState<'peers' | 'banned' | 'traffic'>('peers')
  const [expandedId, setExpandedId] = useState<number | null>(null)
  const [addNode, setAddNode]   = useState('')
  const [banAddr, setBanAddr]   = useState('')
  const [error, setError]       = useState('')
  const [msg, setMsg]           = useState('')

  // Traffic tab state
  const [trafficPoints, setTrafficPoints] = useState<TrafficPoint[]>([])
  const [trafficTotals, setTrafficTotals] = useState<{ recv: number; sent: number } | null>(null)
  const [trafficWindow, setTrafficWindow] = useState<WindowKey>('5m')
  const prevTrafficRef = useRef<{ recv: number; sent: number; time: number } | null>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)

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

  // Traffic polling — only runs when Traffic tab is active
  useEffect(() => {
    if (tab !== 'traffic') return
    const poll = async () => {
      try {
        const res = await api.rpc('getnettotals', [])
        const info = res.result as { totalbytesrecv: number; totalbytessent: number } | null
        if (!info) return
        setTrafficTotals({ recv: info.totalbytesrecv, sent: info.totalbytessent })
        const now = Date.now()
        if (prevTrafficRef.current) {
          const dt = (now - prevTrafficRef.current.time) / 1000
          if (dt > 0) {
            const recv = Math.max(0, (info.totalbytesrecv - prevTrafficRef.current.recv) / dt)
            const sent = Math.max(0, (info.totalbytessent - prevTrafficRef.current.sent) / dt)
            setTrafficPoints(pts => [...pts, { recv, sent }].slice(-1800))
          }
        }
        prevTrafficRef.current = { recv: info.totalbytesrecv, sent: info.totalbytessent, time: now }
      } catch { /* ignore */ }
    }
    poll()
    const id = setInterval(poll, 1000)
    return () => clearInterval(id)
  }, [tab]) // eslint-disable-line

  // Canvas redraw
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas || tab !== 'traffic') return
    const W = canvas.offsetWidth || 600
    const H = canvas.offsetHeight || 200
    const dpr = window.devicePixelRatio || 1
    canvas.width = W * dpr
    canvas.height = H * dpr
    const ctx = canvas.getContext('2d')!
    ctx.scale(dpr, dpr)

    ctx.fillStyle = '#000'
    ctx.fillRect(0, 0, W, H)

    const windowSecs = WINDOWS[trafficWindow]
    const visible = trafficPoints.slice(-windowSecs)
    if (visible.length < 2) {
      ctx.fillStyle = 'rgba(255,255,255,0.2)'
      ctx.font = '12px monospace'
      ctx.fillText('Collecting data…', W / 2 - 60, H / 2)
      return
    }

    const maxRate = Math.max(...visible.map(p => Math.max(p.recv, p.sent)), 512)

    // Grid lines
    ctx.strokeStyle = 'rgba(255,255,255,0.08)'
    ctx.lineWidth = 1
    for (let i = 1; i < 8; i++) {
      const y = Math.round(H * i / 8) + 0.5
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke()
    }

    // Bars — green recv, red sent, overlapping from baseline
    const N = visible.length
    visible.forEach((pt, i) => {
      const x = (i / N) * W
      const w = Math.max(1, W / N + 0.5)

      const rH = (pt.recv / maxRate) * H
      ctx.fillStyle = '#22c55e'
      ctx.fillRect(x, H - rH, w, rH)

      const sH = (pt.sent / maxRate) * H
      ctx.fillStyle = '#ef4444'
      ctx.fillRect(x, H - sH, w, sH)
    })

    // Scale label top-left
    ctx.fillStyle = 'rgba(255,255,255,0.55)'
    ctx.font = '11px monospace'
    ctx.fillText(fmtRate(maxRate), 6, 15)
  }, [trafficPoints, trafficWindow, tab])

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
        <button className={tab === 'peers'   ? 'primary' : ''} onClick={() => setTab('peers')}>Peers ({peers.length})</button>
        <button className={tab === 'banned'  ? 'primary' : ''} onClick={() => setTab('banned')}>Banned ({banned.length})</button>
        <button className={tab === 'traffic' ? 'primary' : ''} onClick={() => setTab('traffic')}>Traffic</button>
        {tab !== 'traffic' && <button onClick={load}>Refresh</button>}
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

      {tab === 'traffic' && (
        <div>
          {/* Chart card */}
          <div className="card" style={{ padding: 0, overflow: 'hidden' }}>
            <canvas
              ref={canvasRef}
              style={{ display: 'block', width: '100%', height: '220px', background: '#000' }}
            />
          </div>

          {/* Controls + totals */}
          <div style={{ display: 'flex', gap: '0.75rem', alignItems: 'center', flexWrap: 'wrap', marginBottom: '1rem' }}>
            <div style={{ display: 'flex', gap: '0.25rem' }}>
              {(Object.keys(WINDOWS) as WindowKey[]).map(w => (
                <button key={w} className={trafficWindow === w ? 'primary' : ''} style={{ padding: '0.25rem 0.6rem', fontSize: '0.78rem' }}
                  onClick={() => setTrafficWindow(w)}>{w}</button>
              ))}
            </div>
            <button style={{ padding: '0.25rem 0.6rem', fontSize: '0.78rem' }}
              onClick={() => { setTrafficPoints([]); prevTrafficRef.current = null }}>Reset</button>
          </div>

          {/* Totals card */}
          {trafficTotals && (
            <div className="card">
              <h2>Totals</h2>
              <div className="stat-grid" style={{ gridTemplateColumns: 'repeat(auto-fill, minmax(140px, 1fr))' }}>
                <div className="stat">
                  <span className="stat-label" style={{ display: 'flex', alignItems: 'center', gap: '0.35rem' }}>
                    <span style={{ display: 'inline-block', width: 10, height: 10, borderRadius: 2, background: '#22c55e' }} />
                    Received
                  </span>
                  <span className="stat-value" style={{ fontSize: '0.95rem' }}>{fmtBytes(trafficTotals.recv)}</span>
                </div>
                <div className="stat">
                  <span className="stat-label" style={{ display: 'flex', alignItems: 'center', gap: '0.35rem' }}>
                    <span style={{ display: 'inline-block', width: 10, height: 10, borderRadius: 2, background: '#ef4444' }} />
                    Sent
                  </span>
                  <span className="stat-value" style={{ fontSize: '0.95rem' }}>{fmtBytes(trafficTotals.sent)}</span>
                </div>
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  )
}
