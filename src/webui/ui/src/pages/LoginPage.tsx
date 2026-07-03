import { useState } from 'react'
import { api, setToken } from '../lib/api'
import radiantLogo from '../assets/images/radiant-darkmode.png'
import './LoginPage.css'

interface Props {
  authMode: string
  onLogin: (token?: string) => void
  onNodeDown: () => void
}

const TRANSIENT_PHRASES = ['initializing', 'No response', 'Malformed response']

export default function LoginPage({ authMode, onLogin, onNodeDown }: Props) {
  const [cookieToken, setCookieToken] = useState('')
  const [password, setPassword]       = useState('')
  const [error, setError]   = useState('')
  const [loading, setLoading] = useState(false)

  const doLogin = async (pw: string, token: string) => {
    setError('')
    setLoading(true)
    try {
      if (authMode === 'password') {
        const res = await api.login(pw)
        setToken(res.token)
        onLogin(res.token)
      } else {
        setToken(token.trim())
        onLogin(token.trim())
      }
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : 'Login failed'
      setToken(null)
      const isTransient = TRANSIENT_PHRASES.some(p => msg.includes(p))
      if (isTransient) {
        // Node went away — return to connecting splash where the probe handles recovery
        onNodeDown()
        return
      } else {
        setError(msg)
      }
    } finally {
      setLoading(false)
    }
  }

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault()
    doLogin(password, cookieToken)
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
