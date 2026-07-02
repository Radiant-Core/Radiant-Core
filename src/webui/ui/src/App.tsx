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

  useEffect(() => {
    setOnUnauthorized(() => setAuthed(false))
    api.authInfo().then(info => setAuthMode(info.mode)).catch(() => {})
  }, [])

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
          </div>
        </div>
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
