import { useState, useEffect, useCallback } from 'react'
import { api, NodeStatus, MiningInfo, explorerBlockUrl } from '../lib/api'

export default function NodePage({ refreshKey = 0 }: { refreshKey?: number }) {
  const [status, setStatus] = useState<NodeStatus | null>(null)
  const [mining, setMining] = useState<MiningInfo | null>(null)
  const [error, setError]   = useState('')

  const load = useCallback(async () => {
    try {
      const [s, m] = await Promise.all([api.nodeStatus(), api.nodeMining()])
      setStatus(s)
      setMining(m)
      setError('')
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : 'Failed to load node status')
    }
  }, [])

  useEffect(() => {
    load()
    const id = setInterval(load, 10000)
    return () => clearInterval(id)
  }, [load])

  // Trigger a manual refresh from parent
  useEffect(() => { if (refreshKey > 0) load() }, [refreshKey]) // eslint-disable-line

  const fmt = (n: number) => n.toLocaleString()
  const pct = (p: number) => `${(p * 100).toFixed(2)}%`
  const uptime = (s: number) => {
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60)
    return `${h}h ${m}m`
  }
  const formatHashrate = (h: number) => {
    if (h >= 1e18) return `${(h / 1e18).toFixed(2)} EH/s`
    if (h >= 1e15) return `${(h / 1e15).toFixed(2)} PH/s`
    if (h >= 1e12) return `${(h / 1e12).toFixed(2)} TH/s`
    if (h >= 1e9)  return `${(h / 1e9).toFixed(2)} GH/s`
    if (h >= 1e6)  return `${(h / 1e6).toFixed(2)} MH/s`
    if (h >= 1e3)  return `${(h / 1e3).toFixed(2)} KH/s`
    return `${h.toFixed(0)} H/s`
  }

  return (
    <div>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: '1rem' }}>
        <h1 style={{ fontSize: '1.1rem', fontWeight: 700 }}>Node Status</h1>
        <button onClick={load}>Refresh</button>
      </div>

      {error && <p className="error-text">{error}</p>}

      {status && (
        <>
          <div className="card">
            <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between', marginBottom: '0.75rem' }}>
              <h2 style={{ margin: 0 }}>Overview</h2>
              <span style={{ fontSize: '0.8rem', color: 'var(--text2)' }}>Uptime: {uptime(status.uptime)}</span>
            </div>
            <div className="stat-grid">
              <div className="stat">
                <span className="stat-label">Network</span>
                <span className="stat-value" style={{ textTransform: 'capitalize' }}>{status.network}</span>
              </div>
              <div className="stat">
                <span className="stat-label">Version</span>
                <span className="stat-value" style={{ fontSize: '0.85rem' }}>{status.version}</span>
              </div>
              <div className="stat">
                <span className="stat-label">Block Height</span>
                <a href={explorerBlockUrl(status.blocks)} target="_blank" rel="noopener noreferrer"
                  className="stat-value" style={{ color: 'var(--accent)', textDecoration: 'none' }}>
                  {fmt(status.blocks)}
                </a>
              </div>
              <div className="stat">
                <span className="stat-label">Headers</span>
                <span className="stat-value">{fmt(status.headers)}</span>
              </div>
              <div className="stat">
                <span className="stat-label">Sync Progress</span>
                <span className="stat-value">{pct(status.verificationprogress)}</span>
              </div>
              <div className="stat">
                <span className="stat-label">Connections</span>
                <span className="stat-value">{status.connections}</span>
              </div>
            </div>
          </div>

          <div className="card">
            <h2>Sync Status</h2>
            <div style={{ display: 'flex', gap: '0.75rem', alignItems: 'center', flexWrap: 'wrap' }}>
              <span className={`badge ${status.initialblockdownload ? 'amber' : 'green'}`}>
                {status.initialblockdownload ? 'Syncing' : 'Synced'}
              </span>
              <div style={{ flex: 1, background: 'var(--bg3)', borderRadius: '3px', height: '6px', minWidth: '120px' }}>
                <div style={{
                  height: '100%',
                  width: pct(status.verificationprogress),
                  background: 'var(--accent)',
                  borderRadius: '3px',
                  transition: 'width 1s ease',
                }} />
              </div>
              <span style={{ color: 'var(--text2)', fontSize: '0.8rem' }}>{pct(status.verificationprogress)}</span>
            </div>
            {status.bestblockhash && (
              <p style={{ marginTop: '0.75rem', color: 'var(--text2)', fontSize: '0.8rem', fontFamily: 'monospace', wordBreak: 'break-all' }}>
                Best block:{' '}
                <a href={explorerBlockUrl(status.blocks)} target="_blank" rel="noopener noreferrer"
                  style={{ color: 'var(--accent)', textDecoration: 'none' }}>
                  {status.bestblockhash}
                </a>
              </p>
            )}
          </div>
        </>
      )}

      {mining && (
        <div className="card">
          <h2>Mining</h2>
          <div className="stat-grid">
            <div className="stat">
              <span className="stat-label">Network Hashrate</span>
              <span className="stat-value">{formatHashrate(mining.networkhashps)}</span>
            </div>
            <div className="stat">
              <span className="stat-label">Difficulty</span>
              <span className="stat-value">{mining.difficulty.toLocaleString(undefined, { maximumFractionDigits: 2 })}</span>
            </div>
            <div className="stat">
              <span className="stat-label">Pooled Transactions</span>
              <span className="stat-value">{mining.pooledtx}</span>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}
