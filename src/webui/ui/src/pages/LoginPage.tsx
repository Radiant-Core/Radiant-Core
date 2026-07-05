import { useState, useEffect } from 'react'
import { api, setToken } from '../lib/api'
import radiantLogo from '../assets/images/radiant-darkmode.png'
import './LoginPage.css'

interface Props {
  onLogin: (token?: string) => void
}

export default function LoginPage({ onLogin }: Props) {
  const [authMode, setAuthMode]   = useState<'password' | 'cookie'>('password')
  const [cookieToken, setCookieToken] = useState('')
  const [password, setPassword]   = useState('')
  const [error, setError]         = useState('')
  const [loading, setLoading]     = useState(false)

  // Detect auth mode once on mount. Silent — the form stays usable even if
  // the node is not up yet; the user will see an error on submit instead.
  useEffect(() => {
    let cancelled = false
    api.authInfo()
      .then(info => { if (!cancelled) setAuthMode(info.mode as 'password' | 'cookie') })
      .catch(() => {})
    return () => { cancelled = true }
  }, [])

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault()
    setError('')
    setLoading(true)
    try {
      if (authMode === 'password') {
        const res = await api.login(password)
        setToken(res.token)
        onLogin(res.token)
      } else {
        setToken(cookieToken.trim())
        onLogin(cookieToken.trim())
      }
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : 'Login failed'
      setToken(null)
      // Connection errors — node not up or still initializing
      if (
        msg.includes('No response') ||
        msg.includes('Malformed response') ||
        msg.includes('initializing') ||
        msg.includes('fetch') ||
        msg.toLowerCase().includes('network')
      ) {
        setError('Cannot connect to node — is it running with webui=1?')
        // Re-probe auth mode so the form switches correctly once node comes up
        api.authInfo()
          .then(info => setAuthMode(info.mode as 'password' | 'cookie'))
          .catch(() => {})
      } else {
        setError(msg)
      }
    } finally {
      setLoading(false)
    }
  }

  return (
    <div className="login-wrap">
      <form className="login-card" onSubmit={handleSubmit}>
        <img src={radiantLogo} alt="Radiant Core" className="login-logo" />
        <h1>Radiant Core</h1>
        <p className="login-sub">Web Interface</p>

        {authMode === 'password' ? (
          <div className="form-group" style={{ width: '100%' }}>
            <label>Password</label>
            <input
              type="password"
              placeholder="Enter -webuipassword"
              value={password}
              onChange={e => setPassword(e.target.value)}
              autoFocus
            />
          </div>
        ) : (
          <div className="form-group" style={{ width: '100%' }}>
            <label>
              Access token{' '}
              <span className="login-hint">(from <code>webui.cookie</code> in data dir)</span>
            </label>
            <input
              type="text"
              placeholder="Paste token here"
              value={cookieToken}
              onChange={e => setCookieToken(e.target.value)}
              autoFocus
            />
          </div>
        )}

        {error && <p className="error-text">{error}</p>}

        <button type="submit" className="primary" disabled={loading} style={{ width: '100%', marginTop: '0.5rem' }}>
          {loading ? 'Authenticating…' : 'Sign in'}
        </button>
      </form>
    </div>
  )
}
