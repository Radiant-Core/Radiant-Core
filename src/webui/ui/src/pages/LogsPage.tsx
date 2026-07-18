import { useState, useRef, useEffect, useCallback, useMemo } from 'react'
import { api } from '../lib/api'

const POLL_MS = 1500
const MAX_LINES = 5000        // cap DOM/memory growth on long sessions
const INITIAL_TAIL = 1000

// Light heuristic colouring for the common debug.log severities.
function lineColor(line: string): string | undefined {
  if (/\bERROR\b|error:/i.test(line))            return 'var(--red)'
  if (/\bwarn(ing)?\b/i.test(line))              return 'var(--accent)'
  return undefined
}

export default function LogsPage() {
  const [lines, setLines]     = useState<string[]>([])
  const [filter, setFilter]   = useState('')
  const [follow, setFollow]   = useState(true)
  const [enabled, setEnabled] = useState(true)
  const [logPath, setLogPath] = useState('')
  const [error, setError]     = useState<string | null>(null)

  // Byte offset for the next incremental fetch, and the incomplete trailing
  // line carried between chunks (a chunk can end mid-line).
  const cursorRef  = useRef<number | null>(null)
  const carryRef   = useRef('')
  const scrollRef  = useRef<HTMLDivElement>(null)
  // Whether the viewport is pinned to the bottom; drives auto-scroll.
  const atBottomRef = useRef(true)

  const appendChunk = useCallback((data: string) => {
    if (!data) return
    const combined = carryRef.current + data
    const parts = combined.split('\n')
    carryRef.current = parts.pop() ?? ''   // last element is the partial line
    if (parts.length === 0) return
    setLines(prev => {
      const next = prev.concat(parts)
      return next.length > MAX_LINES ? next.slice(next.length - MAX_LINES) : next
    })
  }, [])

  // Initial load.
  useEffect(() => {
    let cancelled = false
    api.nodeLogs({ tail: INITIAL_TAIL })
      .then(res => {
        if (cancelled) return
        setEnabled(res.enabled)
        setLogPath(res.path)
        cursorRef.current = res.next
        carryRef.current = ''
        setLines(res.data ? res.data.replace(/\n$/, '').split('\n') : [])
        setError(null)
      })
      .catch(e => { if (!cancelled) setError(e instanceof Error ? e.message : String(e)) })
    return () => { cancelled = true }
  }, [])

  // Follow: poll for appended bytes.
  useEffect(() => {
    if (!follow || !enabled) return
    let cancelled = false
    const poll = async () => {
      if (cursorRef.current === null) return
      try {
        const res = await api.nodeLogs({ from: cursorRef.current })
        if (cancelled) return
        cursorRef.current = res.next
        appendChunk(res.data)
        setError(null)
      } catch (e) {
        if (!cancelled) setError(e instanceof Error ? e.message : String(e))
      }
    }
    const id = setInterval(poll, POLL_MS)
    return () => { cancelled = true; clearInterval(id) }
  }, [follow, enabled, appendChunk])

  // Auto-scroll to bottom while following and pinned to the bottom.
  useEffect(() => {
    if (follow && atBottomRef.current && scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight
    }
  }, [lines, follow])

  const onScroll = () => {
    const el = scrollRef.current
    if (!el) return
    atBottomRef.current = el.scrollHeight - el.scrollTop - el.clientHeight < 40
  }

  const visible = useMemo(() => {
    if (!filter.trim()) return lines
    const f = filter.toLowerCase()
    return lines.filter(l => l.toLowerCase().includes(f))
  }, [lines, filter])

  const download = () => {
    const blob = new Blob([lines.join('\n')], { type: 'text/plain' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = 'debug.log'
    a.click()
    URL.revokeObjectURL(url)
  }

  const copy = () => { navigator.clipboard?.writeText(visible.join('\n')).catch(() => {}) }

  return (
    <div>
      <h1 style={{ fontSize: '1.1rem', fontWeight: 700, marginBottom: '0.5rem' }}>Debug Log</h1>
      <p style={{ color: 'var(--text2)', fontSize: '0.85rem', marginBottom: '1rem', wordBreak: 'break-all' }}>
        {enabled
          ? <>Tailing <code style={{ fontFamily: 'monospace' }}>{logPath}</code></>
          : 'Logging to file is disabled (-nodebuglogfile). Nothing to show.'}
      </p>

      <div className="form-row" style={{ marginBottom: '0.5rem', flexWrap: 'wrap', gap: '0.5rem' }}>
        <input
          value={filter}
          onChange={e => setFilter(e.target.value)}
          placeholder="Filter (substring)…"
          style={{ fontFamily: 'monospace', flex: 1, minWidth: '160px' }}
          autoComplete="off"
          spellCheck={false}
        />
        <button
          className={follow ? 'primary' : ''}
          onClick={() => {
            const next = !follow
            setFollow(next)
            if (next) atBottomRef.current = true
          }}
          disabled={!enabled}
          title={follow ? 'Pause auto-refresh' : 'Resume auto-refresh'}
        >
          {follow ? '⏸ Pause' : '▶ Follow'}
        </button>
        <button onClick={copy} disabled={!visible.length}>Copy</button>
        <button onClick={download} disabled={!lines.length}>Download</button>
        <button onClick={() => { setLines([]); carryRef.current = '' }}>Clear</button>
      </div>

      {error && (
        <p style={{ color: 'var(--red)', fontSize: '0.8rem', marginBottom: '0.5rem' }}>{error}</p>
      )}

      <div
        ref={scrollRef}
        onScroll={onScroll}
        className="card"
        style={{
          fontFamily: 'monospace',
          fontSize: '0.8rem',
          lineHeight: 1.5,
          height: '65vh',
          overflowY: 'auto',
          padding: '0.75rem 1rem',
          whiteSpace: 'pre-wrap',
          wordBreak: 'break-word',
        }}
      >
        {visible.length === 0 ? (
          <p style={{ color: 'var(--text2)' }}>
            {filter.trim() ? 'No lines match the filter.' : 'No log output yet.'}
          </p>
        ) : (
          visible.map((l, i) => (
            <div key={i} style={{ color: lineColor(l) }}>{l || ' '}</div>
          ))
        )}
      </div>

      <p style={{ color: 'var(--text2)', fontSize: '0.75rem', marginTop: '0.5rem' }}>
        Showing {visible.length.toLocaleString()}
        {filter.trim() && <> of {lines.length.toLocaleString()}</>} lines
        {lines.length >= MAX_LINES && <> · buffer capped at {MAX_LINES.toLocaleString()}</>}
      </p>
    </div>
  )
}
