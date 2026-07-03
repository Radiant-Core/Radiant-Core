import { useState, useEffect, useCallback, useRef } from 'react'
import { api, Peer, BanEntry } from '../lib/api'
import './PeersPage.css'

type TrafficPoint = { recv: number; sent: number }
const WINDOWS = { '1m': 60, '5m': 300, '15m': 900, '30m': 1800 } as const
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

interface Props {
  refreshKey?: number
  trafficPoints: TrafficPoint[]
  trafficTotals: { recv: number; sent: number } | null
  onResetTraffic: () => void
}

export default function PeersPage({ refreshKey = 0, trafficPoints, trafficTotals, onResetTraffic }: Props) {
  const [peers, setPeers]       = useState<Peer[]>([])
  const [banned, setBanned]     = useState<BanEntry[]>([])
  const [tab, setTab]           = useState<'peers' | 'banned' | 'traffic'>('peers')
  const [expandedId, setExpandedId] = useState<number | null>(null)
  const [addNode, setAddNode]   = useState('')
  const [banAddr, setBanAddr]   = useState('')
  const [error, setError]       = useState('')
  const [msg, setMsg]           = useState('')

  const [trafficWindow, setTrafficWindow] = useState<WindowKey>('5m')
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

  // Canvas redraw — filled area chart (Activity Monitor style)
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const W = canvas.offsetWidth || 600
    const H = canvas.offsetHeight || 240
    const dpr = window.devicePixelRatio || 1
    canvas.width = W * dpr
    canvas.height = H * dpr
    const ctx = canvas.getContext('2d')!
    ctx.scale(dpr, dpr)

    ctx.fillStyle = '#0d1117'
    ctx.fillRect(0, 0, W, H)

    const PAD_L = 54, PAD_R = 8, PAD_T = 10, PAD_B = 20
    const cW = W - PAD_L - PAD_R
    const cH = H - PAD_T - PAD_B

    const windowSecs = WINDOWS[trafficWindow]
    const visible = trafficPoints.slice(-windowSecs)
    if (visible.length < 2) {
      ctx.fillStyle = 'rgba(255,255,255,0.25)'
      ctx.font = '12px monospace'
      ctx.textAlign = 'center'
      ctx.fillText('Collecting data…', PAD_L + cW / 2, PAD_T + cH / 2)
      return
    }

    const maxRate = Math.max(...visible.map(p => Math.max(p.recv, p.sent)), 512)
    const N = visible.length
    // Pin "now" to the right edge; each point is 1 second wide in window space.
    // When N < windowSecs, data is anchored right and leaves blank space on the left.
    const xOf = (i: number) => PAD_L + ((windowSecs - (N - 1 - i)) / windowSecs) * cW
    const yOf = (v: number) => PAD_T + (1 - v / maxRate) * cH
    const bottom = PAD_T + cH

    // Horizontal grid + Y-axis rate labels
    ctx.font = '10px monospace'
    ctx.textAlign = 'right'
    for (let i = 0; i <= 4; i++) {
      const y = Math.round(PAD_T + (i / 4) * cH) + 0.5
      ctx.strokeStyle = 'rgba(255,255,255,0.07)'
      ctx.lineWidth = 1
      ctx.beginPath(); ctx.moveTo(PAD_L, y); ctx.lineTo(W - PAD_R, y); ctx.stroke()
      ctx.fillStyle = 'rgba(255,255,255,0.38)'
      ctx.fillText(fmtRate(maxRate * (1 - i / 4)), PAD_L - 5, y + 3)
    }

    // Filled area + line for one series
    const drawSeries = (values: number[], fill: string, stroke: string) => {
      ctx.beginPath()
      ctx.moveTo(xOf(0), bottom)
      values.forEach((v, i) => ctx.lineTo(xOf(i), yOf(v)))
      ctx.lineTo(xOf(N - 1), bottom)
      ctx.closePath()
      ctx.fillStyle = fill
      ctx.fill()

      ctx.beginPath()
      values.forEach((v, i) => i === 0 ? ctx.moveTo(xOf(0), yOf(v)) : ctx.lineTo(xOf(i), yOf(v)))
      ctx.strokeStyle = stroke
      ctx.lineWidth = 1.5
      ctx.lineJoin = 'round'
      ctx.stroke()
    }

    drawSeries(visible.map(p => p.recv), 'rgba(34,197,94,0.22)', '#22c55e')
    drawSeries(visible.map(p => p.sent), 'rgba(239,68,68,0.22)', '#ef4444')

    // X-axis time labels — always based on the selected window, not data count
    const fmtAgo = (s: number) => s === 0 ? 'now' : s < 60 ? `-${s}s` : `-${Math.floor(s / 60)}m`
    ctx.fillStyle = 'rgba(255,255,255,0.35)'
    ctx.font = '10px monospace'
    for (let i = 0; i <= 5; i++) {
      const frac = i / 5
      const x = PAD_L + frac * cW
      ctx.textAlign = i === 0 ? 'left' : i === 5 ? 'right' : 'center'
      ctx.fillText(fmtAgo(Math.round(windowSecs * (1 - frac))), x, H - 4)
    }
  }, [trafficPoints, trafficWindow])

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
              style={{ display: 'block', width: '100%', height: '240px' }}
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
              onClick={onResetTraffic}>Reset</button>
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
