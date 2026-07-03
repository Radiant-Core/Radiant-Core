import { useState, useEffect, useCallback } from 'react'
import { api, getToken, setToken, setOnUnauthorized } from './lib/api'
import radiantLogo from './assets/images/radiant-darkmode.png'
import LoginPage from './pages/LoginPage'
import NodePage from './pages/NodePage'
import WalletsPage from './pages/WalletsPage'
import PeersPage from './pages/PeersPage'
import ConsolePage from './pages/ConsolePage'
import SettingsPage from './pages/SettingsPage'
import './App.css'

type Page = 'node' | 'wallets' | 'peers' | 'console' | 'settings'

function IconEyeOff() {
  return (
    <svg stroke="currentColor" fill="currentColor" strokeWidth="0" viewBox="0 0 24 24" aria-hidden="true" height="1em" width="1em" xmlns="http://www.w3.org/2000/svg">
      <path d="M8.073 12.194 4.212 8.333c-1.52 1.657-2.096 3.317-2.106 3.351L2 12l.105.316C2.127 12.383 4.421 19 12.054 19c.929 0 1.775-.102 2.552-.273l-2.746-2.746a3.987 3.987 0 0 1-3.787-3.787zM12.054 5c-1.855 0-3.375.404-4.642.998L3.707 2.293 2.293 3.707l18 18 1.414-1.414-3.298-3.298c2.638-1.953 3.579-4.637 3.593-4.679l.105-.316-.105-.316C21.98 11.617 19.687 5 12.054 5zm1.906 7.546c.187-.677.028-1.439-.492-1.96s-1.283-.679-1.96-.492L10 8.586A3.955 3.955 0 0 1 12.054 8c2.206 0 4 1.794 4 4a3.94 3.94 0 0 1-.587 2.053l-1.507-1.507z" />
    </svg>
  )
}

function IconEye() {
  return (
    <svg stroke="currentColor" fill="currentColor" strokeWidth="0" viewBox="0 0 24 24" aria-hidden="true" height="1em" width="1em" xmlns="http://www.w3.org/2000/svg">
      <path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z" />
    </svg>
  )
}

export default function App() {
  const [authed, setAuthed]   = useState(!!getToken())
  const [page, setPage]       = useState<Page>('node')
  const [authMode, setAuthMode] = useState<string>('cookie')
  const [nodeInfo, setNodeInfo] = useState<{ network: string; blocks: number } | null>(null)
  const [masked, setMasked]   = useState(false)
  const [refreshKey, setRefreshKey] = useState(0)
  const [navOpen, setNavOpen] = useState(false)
  const [serverReady, setServerReady] = useState(false)
  const [probeKey, setProbeKey] = useState(0)

  // Wire up the unauthorized handler once; re-probe when the node restarts.
  useEffect(() => {
    setOnUnauthorized(() => {
      setAuthed(false)
      setServerReady(false)         // show connecting splash while node is restarting
      setProbeKey(k => k + 1)       // restart the probe
    })
  }, [])

  // Probe runs on first mount and again whenever probeKey increments.
  // A stale token in localStorage won't cause an infinite 401 loop because
  // api.ts only fires _onUnauthorized when there was a token to expire;
  // after setToken(null) clears it the next probe sends no header and gets 200.
  useEffect(() => {
    let cancelled = false
    let retryId: ReturnType<typeof setTimeout> | null = null

    const probe = async () => {
      try {
        const info = await api.authInfo()
        if (!cancelled) { setAuthMode(info.mode); setServerReady(true) }
      } catch {
        if (!cancelled) retryId = setTimeout(probe, 3000)
      }
    }

    probe()
    return () => { cancelled = true; if (retryId) clearTimeout(retryId) }
  }, [probeKey])

  const fetchNodeInfo = useCallback(() => {
    if (!getToken()) return
    api.nodeStatus()
      .then(s => setNodeInfo({ network: s.network, blocks: s.blocks }))
      .catch(() => {})
  }, [])

  useEffect(() => {
    if (!authed) { setNodeInfo(null); return }
    fetchNodeInfo()
    const id = setInterval(fetchNodeInfo, 30000)
    return () => clearInterval(id)
  }, [authed, fetchNodeInfo])

  // SSE: instant block notifications via ticket-auth EventSource
  useEffect(() => {
    if (!authed) return
    let es: EventSource | null = null
    let retryTimeout: ReturnType<typeof setTimeout> | null = null
    let cancelled = false

    const connect = async () => {
      try {
        const { ticket } = await api.sseTicket()
        if (cancelled) return
        es = new EventSource(`/webui/api/events?ticket=${encodeURIComponent(ticket)}`)
        es.addEventListener('block', (e: MessageEvent) => {
          const data = JSON.parse(e.data) as { height: number }
          setNodeInfo(prev => prev ? { ...prev, blocks: data.height } : prev)
          setRefreshKey(k => k + 1)
        })
        es.onerror = () => {
          es?.close()
          es = null
          if (!cancelled) retryTimeout = setTimeout(connect, 5000)
        }
      } catch {
        if (!cancelled) retryTimeout = setTimeout(connect, 5000)
      }
    }

    connect()

    return () => {
      cancelled = true
      es?.close()
      if (retryTimeout) clearTimeout(retryTimeout)
    }
  }, [authed])

  if (!serverReady) {
    return (
      <div className="connect-splash">
        <img src={radiantLogo} alt="Radiant" className="connect-logo" />
        <span className="connect-name">Radiant Core</span>
        <span className="connect-status">Connecting to node…</span>
        <button className="connect-refresh-btn" onClick={() => window.location.reload()}>
          Refresh
        </button>
        <span className="connect-hint">
          If the node is not running, start <code>radiant-qt</code> or <code>radiantd</code> with{' '}
          <code>-webui=1</code>, or add <code>webui=1</code> to <code>radiant.conf</code>.
        </span>
      </div>
    )
  }

  if (!authed) {
    return (
      <LoginPage
        authMode={authMode}
        onLogin={token => { setToken(token ?? getToken()!); setAuthed(true) }}
      />
    )
  }

  const nav: Array<{ id: Page; label: string }> = [
    { id: 'node',     label: 'Node' },
    { id: 'wallets',  label: 'Wallet' },
    { id: 'peers',    label: 'Peers' },
    { id: 'console',  label: 'Console' },
    { id: 'settings', label: 'Settings' },
  ]

  return (
    <div className="app-shell">
      <header className="topbar">
        <div className="topbar-inner">
          <div className="brand">
            <img src={radiantLogo} alt="Radiant" className="brand-logo" />
            <div className="brand-info">
              <span className="brand-name">Radiant Core</span>
              {nodeInfo && (
                <span className="brand-sub">
                  {nodeInfo.network} · block {nodeInfo.blocks.toLocaleString()}
                </span>
              )}
            </div>
          </div>
          <nav className="topnav">
            {nav.map(n => (
              <button
                key={n.id}
                className={page === n.id ? 'active' : ''}
                onClick={() => setPage(n.id)}
              >
                {n.label}
              </button>
            ))}
          </nav>
          <div className="topbar-end">
            <button
              onClick={() => setRefreshKey(k => k + 1)}
              title="Refresh current tab"
              style={{
                background: 'none', border: 'none', cursor: 'pointer',
                color: 'var(--text2)', fontSize: '1.1rem', lineHeight: 1,
                padding: '0.25rem 0.4rem', borderRadius: 'var(--radius)',
              }}
            >↺</button>
            <button
              onClick={() => setMasked(m => !m)}
              title={masked ? 'Show amounts' : 'Hide amounts'}
              style={{
                background: 'none', border: 'none', cursor: 'pointer',
                color: masked ? 'var(--accent)' : 'var(--text2)',
                fontSize: '1.1rem', lineHeight: 1, padding: '0.25rem 0.4rem',
                borderRadius: 'var(--radius)', display: 'flex', alignItems: 'center',
              }}
            >
              {masked ? <IconEye /> : <IconEyeOff />}
            </button>
            <button
              className="logout-btn"
              onClick={async () => {
                try { await api.logout() } catch { /* ignore */ }
                setToken(null)
                setAuthed(false)
              }}
            >
              Logout
            </button>
            <button
              className="hamburger"
              onClick={() => setNavOpen(o => !o)}
              aria-label="Menu"
            >
              {navOpen ? '✕' : '☰'}
            </button>
          </div>
        </div>
        {navOpen && (
          <nav className="mobile-nav">
            {nav.map(n => (
              <button
                key={n.id}
                className={page === n.id ? 'active' : ''}
                onClick={() => { setPage(n.id); setNavOpen(false) }}
              >
                {n.label}
              </button>
            ))}
            <div className="mobile-nav-actions">
              <button onClick={() => { setRefreshKey(k => k + 1); setNavOpen(false) }}>↺ Refresh</button>
              <button onClick={() => setMasked(m => !m)}>{masked ? 'Show amounts' : 'Hide amounts'}</button>
            </div>
          </nav>
        )}
      </header>

      <main className="content">
        {page === 'node'     && <NodePage refreshKey={refreshKey} />}
        {page === 'wallets'  && <WalletsPage masked={masked} refreshKey={refreshKey} />}
        {page === 'peers'    && <PeersPage refreshKey={refreshKey} />}
        {page === 'console'  && <ConsolePage />}
        {page === 'settings' && <SettingsPage />}
      </main>
    </div>
  )
}
