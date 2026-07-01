import { useState } from 'react'
import { api, setToken } from '../lib/api'
import radiantLogo from '../assets/images/radiant-darkmode.png'
import './LoginPage.css'

interface Props {
  authMode: string
  onLogin: (token?: string) => void
}

export default function LoginPage({ authMode, onLogin }: Props) {
  const [cookieToken, setCookieToken] = useState('')
  const [password, setPassword]       = useState('')
  const [error, setError]             = useState('')
  const [loading, setLoading]         = useState(false)

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
        // Cookie mode: token is what the user pastes from webui.cookie
        setToken(cookieToken.trim())
        onLogin(cookieToken.trim())
      }
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : 'Login failed')
      setToken(null)
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
