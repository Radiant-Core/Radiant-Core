import { useState, useEffect, useCallback, useMemo } from 'react'
import { api, NodeStatus, MiningInfo, explorerBlockUrl, explorerTxUrl } from '../lib/api'

type NodeTab = 'overview' | 'blocks' | 'mempool'
type SortKey = 'size' | 'fee' | 'feerate' | 'age'

type BlockInfo = { hash: string; height: number; time: number; nTx: number; size: number }
type MempoolEntry = { txid: string; size: number; fee: number; time: number }

function fmtAge(ts: number): string {
  const sec = Math.floor(Date.now() / 1000) - ts
  if (sec < 60) return `${sec}s`
  if (sec < 3600) return `${Math.floor(sec / 60)}m ${sec % 60}s`
  return `${Math.floor(sec / 3600)}h ${Math.floor((sec % 3600) / 60)}m`
}

export default function NodePage({ refreshKey = 0 }: { refreshKey?: number }) {
  const [status, setStatus]   = useState<NodeStatus | null>(null)
  const [mining, setMining]   = useState<MiningInfo | null>(null)
  const [error, setError]     = useState('')
  const [nodeTab, setNodeTab] = useState<NodeTab>('overview')

  const [recentBlocks, setRecentBlocks]   = useState<BlockInfo[]>([])
  const [blocksLoading, setBlocksLoading] = useState(false)

  const [mempool, setMempool]               = useState<MempoolEntry[]>([])
  const [mempoolLoading, setMempoolLoading] = useState(false)
  const [mempoolSort, setMempoolSort]       = useState<SortKey>('age')
  const [mempoolSortDir, setMempoolSortDir] = useState<'asc' | 'desc'>('asc')
  const [mempoolPage, setMempoolPage]       = useState(0)
  const MEMPOOL_PER_PAGE = 25

  const load = useCallback(async () => {
    try {
      const [s, m] = await Promise.all([api.nodeStatus(), api.nodeMining()])
      setStatus(s); setMining(m); setError('')
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : 'Failed to load node status')
    }
  }, [])

  const loadBlocks = useCallback(async (tipHeight: number) => {
    if (tipHeight <= 0) return
    setBlocksLoading(true)
    try {
      const heights = Array.from({ length: Math.min(10, tipHeight + 1) }, (_, i) => tipHeight - i)
      const hashResults = await Promise.all(heights.map(h => api.rpc('getblockhash', [h])))
      const blockResults = await Promise.all(hashResults.map(r => api.rpc('getblock', [r.result as string, 1])))
      setRecentBlocks(blockResults.map(r => {
        const b = r.result as { hash: string; height: number; time: number; nTx: number; size: number }
        return { hash: b.hash, height: b.height, time: b.time, nTx: b.nTx, size: b.size }
      }))
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : 'Failed to load blocks')
    }
    setBlocksLoading(false)
  }, [])

  const loadMempool = useCallback(async () => {
    setMempoolLoading(true); setMempoolPage(0)
    try {
      const res = await api.rpc('getrawmempool', [true])
      const raw = res.result as Record<string, { size: number; fees: { base: number }; fee?: number; time: number }> | null
      setMempool(raw ? Object.entries(raw).map(([txid, d]) => ({ txid, size: d.size, fee: d.fees?.base ?? d.fee ?? 0, time: d.time })) : [])
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : 'Failed to load mempool')
    }
    setMempoolLoading(false)
  }, [])

  useEffect(() => { load(); const id = setInterval(load, 10000); return () => clearInterval(id) }, [load])
  useEffect(() => { if (refreshKey > 0) load() }, [refreshKey]) // eslint-disable-line

  useEffect(() => {
    if (nodeTab === 'blocks' && status) loadBlocks(status.blocks)
    if (nodeTab === 'mempool') loadMempool()
  }, [nodeTab]) // eslint-disable-line

  useEffect(() => {
    if (refreshKey <= 0) return
    if (nodeTab === 'blocks' && status) loadBlocks(status.blocks)
    if (nodeTab === 'mempool') loadMempool()
  }, [refreshKey]) // eslint-disable-line

  const sortedMempool = useMemo(() => {
    const now = Date.now() / 1000
    return [...mempool].sort((a, b) => {
      let diff = 0
      if (mempoolSort === 'size')    diff = a.size - b.size
      if (mempoolSort === 'fee')     diff = a.fee - b.fee
      if (mempoolSort === 'feerate') diff = (a.fee / a.size) - (b.fee / b.size)
      if (mempoolSort === 'age')     diff = (now - b.time) - (now - a.time)
      return mempoolSortDir === 'desc' ? -diff : diff
    })
  }, [mempool, mempoolSort, mempoolSortDir])

  const fmt = (n: number) => n.toLocaleString()
  const pct = (p: number) => `${(p * 100).toFixed(2)}%`
  const uptime = (s: number) => { const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60); return `${h}h ${m}m` }
  const formatHashrate = (h: number) => {
    if (h >= 1e18) return `${(h / 1e18).toFixed(2)} EH/s`
    if (h >= 1e15) return `${(h / 1e15).toFixed(2)} PH/s`
    if (h >= 1e12) return `${(h / 1e12).toFixed(2)} TH/s`
    if (h >= 1e9)  return `${(h / 1e9).toFixed(2)} GH/s`
    if (h >= 1e6)  return `${(h / 1e6).toFixed(2)} MH/s`
    if (h >= 1e3)  return `${(h / 1e3).toFixed(2)} KH/s`
    return `${h.toFixed(0)} H/s`
  }
  const sortTh = (key: SortKey, label: string) => (
    <th key={key} style={{ cursor: 'pointer', userSelect: 'none', whiteSpace: 'nowrap' }}
      onClick={() => { if (mempoolSort === key) setMempoolSortDir(d => d === 'asc' ? 'desc' : 'asc'); else { setMempoolSort(key); setMempoolSortDir('desc') } }}>
      {label}{mempoolSort === key ? (mempoolSortDir === 'desc' ? ' ▼' : ' ▲') : ''}
    </th>
  )

  return (
    <div>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: '1rem', flexWrap: 'wrap', gap: '0.5rem' }}>
        <h1 style={{ fontSize: '1.1rem', fontWeight: 700 }}>Node</h1>
        <div style={{ display: 'flex', gap: '0.25rem', flexWrap: 'wrap' }}>
          {(['overview', 'blocks', 'mempool'] as NodeTab[]).map(t => (
            <button key={t} className={nodeTab === t ? 'primary' : ''} style={{ fontSize: '0.82rem', padding: '0.3rem 0.7rem' }}
              onClick={() => setNodeTab(t)}>
              {t.charAt(0).toUpperCase() + t.slice(1)}
            </button>
          ))}
          <button onClick={() => { load(); if (nodeTab === 'blocks' && status) loadBlocks(status.blocks); if (nodeTab === 'mempool') loadMempool() }}>
            Refresh
          </button>
        </div>
      </div>

      {error && <p className="error-text" style={{ marginBottom: '0.75rem' }}>{error}</p>}

      {/* ── Overview ─────────────────────────────── */}
      {nodeTab === 'overview' && status && (
        <>
          <div className="card">
            <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between', marginBottom: '0.75rem' }}>
              <h2 style={{ margin: 0 }}>Overview</h2>
              <span style={{ fontSize: '0.8rem', color: 'var(--text2)' }}>Uptime: {uptime(status.uptime)}</span>
            </div>
            <div className="stat-grid">
              <div className="stat"><span className="stat-label">Network</span><span className="stat-value" style={{ textTransform: 'capitalize' }}>{status.network}</span></div>
              <div className="stat"><span className="stat-label">Version</span><span className="stat-value" style={{ fontSize: '0.85rem' }}>{status.version}</span></div>
              <div className="stat">
                <span className="stat-label">Block Height</span>
                <a href={explorerBlockUrl(status.blocks)} target="_blank" rel="noopener noreferrer" className="stat-value" style={{ color: 'var(--accent)', textDecoration: 'none' }}>{fmt(status.blocks)}</a>
              </div>
              <div className="stat"><span className="stat-label">Headers</span><span className="stat-value">{fmt(status.headers)}</span></div>
              <div className="stat"><span className="stat-label">Sync Progress</span><span className="stat-value">{pct(status.verificationprogress)}</span></div>
              <div className="stat"><span className="stat-label">Connections</span><span className="stat-value">{status.connections}</span></div>
            </div>
          </div>

          <div className="card">
            <h2>Sync Status</h2>
            <div style={{ display: 'flex', gap: '0.75rem', alignItems: 'center', flexWrap: 'wrap' }}>
              <span className={`badge ${status.initialblockdownload ? 'amber' : 'green'}`}>{status.initialblockdownload ? 'Syncing' : 'Synced'}</span>
              <div style={{ flex: 1, background: 'var(--bg3)', borderRadius: '3px', height: '6px', minWidth: '120px' }}>
                <div style={{ height: '100%', width: pct(status.verificationprogress), background: 'var(--accent)', borderRadius: '3px', transition: 'width 1s ease' }} />
              </div>
              <span style={{ color: 'var(--text2)', fontSize: '0.8rem' }}>{pct(status.verificationprogress)}</span>
            </div>
            {status.bestblockhash && (
              <p style={{ marginTop: '0.75rem', color: 'var(--text2)', fontSize: '0.8rem', fontFamily: 'monospace', wordBreak: 'break-all' }}>
                Best block:{' '}
                <a href={explorerBlockUrl(status.blocks)} target="_blank" rel="noopener noreferrer" style={{ color: 'var(--accent)', textDecoration: 'none' }}>{status.bestblockhash}</a>
              </p>
            )}
          </div>

          {mining && (
            <div className="card">
              <h2>Mining</h2>
              <div className="stat-grid">
                <div className="stat"><span className="stat-label">Network Hashrate</span><span className="stat-value">{formatHashrate(mining.networkhashps)}</span></div>
                <div className="stat"><span className="stat-label">Difficulty</span><span className="stat-value">{mining.difficulty.toLocaleString(undefined, { maximumFractionDigits: 2 })}</span></div>
                <div className="stat"><span className="stat-label">Pooled Transactions</span><span className="stat-value">{mining.pooledtx}</span></div>
              </div>
            </div>
          )}
        </>
      )}

      {/* ── Recent Blocks ─────────────────────────── */}
      {nodeTab === 'blocks' && (
        <div className="card" style={{ padding: 0, overflow: 'hidden' }}>
          {blocksLoading && recentBlocks.length === 0 ? (
            <div style={{ padding: '2rem', textAlign: 'center', color: 'var(--text2)' }}>Loading…</div>
          ) : (
            <table className="table">
              <thead>
                <tr>
                  <th>Height</th>
                  <th>Time</th>
                  <th style={{ textAlign: 'right' }}>Txs</th>
                  <th style={{ textAlign: 'right' }}>Size</th>
                  <th>Hash</th>
                </tr>
              </thead>
              <tbody>
                {recentBlocks.map(b => (
                  <tr key={b.hash}>
                    <td style={{ fontWeight: 600 }}>
                      <a href={explorerBlockUrl(b.height)} target="_blank" rel="noopener noreferrer" style={{ color: 'var(--accent)' }}>{b.height.toLocaleString()}</a>
                    </td>
                    <td style={{ color: 'var(--text2)', fontSize: '0.82rem' }}>{new Date(b.time * 1000).toLocaleString()}</td>
                    <td style={{ textAlign: 'right', fontSize: '0.82rem' }}>{b.nTx.toLocaleString()}</td>
                    <td style={{ textAlign: 'right', fontSize: '0.82rem', color: 'var(--text2)', fontFamily: 'monospace' }}>
                      {b.size >= 1024 ? `${(b.size / 1024).toFixed(1)} KB` : `${b.size} B`}
                    </td>
                    <td style={{ fontFamily: 'monospace', fontSize: '0.72rem', color: 'var(--text2)' }}>
                      {b.hash.slice(0, 16)}…{b.hash.slice(-8)}
                    </td>
                  </tr>
                ))}
                {recentBlocks.length === 0 && !blocksLoading && (
                  <tr><td colSpan={5} style={{ textAlign: 'center', color: 'var(--text2)', padding: '2rem' }}>No data</td></tr>
                )}
              </tbody>
            </table>
          )}
        </div>
      )}

      {/* ── Mempool ───────────────────────────────── */}
      {nodeTab === 'mempool' && (
        <div>
          <div className="card" style={{ marginBottom: '0.75rem' }}>
            <div className="stat-grid">
              <div className="stat"><span className="stat-label">Transactions</span><span className="stat-value">{mempool.length.toLocaleString()}</span></div>
              <div className="stat"><span className="stat-label">Total Size</span><span className="stat-value">{(mempool.reduce((s, t) => s + t.size, 0) / 1024).toFixed(1)} KB</span></div>
              <div className="stat"><span className="stat-label">Total Fees</span><span className="stat-value">{mempool.reduce((s, t) => s + t.fee, 0).toFixed(4)} RXD</span></div>
            </div>
          </div>

          <div className="card" style={{ padding: 0, overflow: 'hidden' }}>
            {mempoolLoading && mempool.length === 0 ? (
              <div style={{ padding: '2rem', textAlign: 'center', color: 'var(--text2)' }}>Loading…</div>
            ) : (
              <table className="table">
                <thead>
                  <tr>
                    <th>TxID</th>
                    {sortTh('size', 'Size')}
                    {sortTh('fee', 'Fee (RXD)')}
                    {sortTh('feerate', 'sat/B')}
                    {sortTh('age', 'Age')}
                  </tr>
                </thead>
                <tbody>
                  {sortedMempool.slice(mempoolPage * MEMPOOL_PER_PAGE, (mempoolPage + 1) * MEMPOOL_PER_PAGE).map(tx => (
                    <tr key={tx.txid}>
                      <td style={{ fontFamily: 'monospace', fontSize: '0.72rem' }}>
                        <a href={explorerTxUrl(tx.txid)} target="_blank" rel="noopener noreferrer" style={{ color: 'var(--accent)', textDecoration: 'none' }}>
                          {tx.txid.slice(0, 14)}…{tx.txid.slice(-8)}
                        </a>
                      </td>
                      <td style={{ fontSize: '0.82rem' }}>{tx.size} B</td>
                      <td style={{ fontSize: '0.82rem', fontFamily: 'monospace' }}>{tx.fee.toFixed(8)}</td>
                      <td style={{ fontSize: '0.82rem' }}>{(tx.fee * 1e8 / tx.size).toFixed(2)}</td>
                      <td style={{ fontSize: '0.82rem', color: 'var(--text2)' }}>{fmtAge(tx.time)}</td>
                    </tr>
                  ))}
                  {mempool.length === 0 && !mempoolLoading && (
                    <tr><td colSpan={5} style={{ textAlign: 'center', color: 'var(--text2)', padding: '2rem' }}>Mempool is empty</td></tr>
                  )}
                </tbody>
              </table>
            )}
          </div>

          {mempool.length > MEMPOOL_PER_PAGE && (
            <div style={{ display: 'flex', justifyContent: 'center', gap: '0.5rem', marginTop: '0.75rem', alignItems: 'center' }}>
              <button disabled={mempoolPage === 0} onClick={() => setMempoolPage(p => p - 1)}>← Prev</button>
              <span style={{ fontSize: '0.82rem', color: 'var(--text2)' }}>{mempoolPage + 1} / {Math.ceil(mempool.length / MEMPOOL_PER_PAGE)}</span>
              <button disabled={(mempoolPage + 1) * MEMPOOL_PER_PAGE >= mempool.length} onClick={() => setMempoolPage(p => p + 1)}>Next →</button>
            </div>
          )}
        </div>
      )}
    </div>
  )
}
