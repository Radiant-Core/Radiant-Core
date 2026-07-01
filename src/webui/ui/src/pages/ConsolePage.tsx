import { useState, useRef, useEffect, useCallback } from 'react'
import { api } from '../lib/api'

const RPC_COMMANDS = [
  // Blockchain
  'finalizeblock', 'getbestblockhash', 'getblock', 'getblockchaininfo',
  'getblockcount', 'getblockhash', 'getblockheader', 'getblockstats',
  'getchaintips', 'getchaintxstats', 'getdifficulty',
  'getdsproof', 'getdsprooflist', 'getdsproofscore',
  'getfinalizedblockhash',
  'getmempoolancestors', 'getmempooldescendants', 'getmempoolentry', 'getmempoolinfo',
  'getopenorders', 'getopenordersbywant',
  'getrawmempool',
  'getswapcount', 'getswapcountbywant', 'getswaphistory', 'getswaphistorybywant', 'getswapindexinfo',
  'gettxout', 'gettxoutproof', 'gettxoutsetinfo',
  'invalidateblock', 'parkblock', 'preciousblock', 'pruneblockchain',
  'reconsiderblock', 'savemempool', 'scantxoutset', 'unparkblock',
  'verifychain', 'verifytxoutproof',
  // Control
  'getmemoryinfo', 'getrpcinfo', 'help', 'logging', 'stop', 'uptime',
  // Generating
  'generate', 'generatetoaddress',
  // Mining
  'getblocktemplate', 'getblocktemplatelight', 'getmininginfo', 'getnetworkhashps',
  'prioritisetransaction', 'submitblock', 'submitblocklight', 'submitheader', 'validateblocktemplate',
  // Network
  'addnode', 'clearbanned', 'disconnectnode', 'getaddednodeinfo',
  'getconnectioncount', 'getexcessiveblock', 'getnettotals', 'getnetworkinfo',
  'getnodeaddresses', 'getpeerinfo',
  'listbanned', 'ping', 'setban', 'setexcessiveblock', 'setnetworkactive',
  // Raw Transactions
  'combinepsbt', 'combinerawtransaction', 'converttopsbt',
  'createpsbt', 'createrawtransaction', 'decodepsbt', 'decoderawtransaction',
  'decodescript', 'finalizepsbt', 'fundrawtransaction', 'getrawtransaction',
  'sendrawtransaction', 'signrawtransactionwithkey', 'testmempoolaccept',
  // Util
  'createmultisig', 'estimatefee', 'signmessagewithprivkey', 'validateaddress', 'verifymessage',
  // Wallet
  'abandontransaction', 'abortrescan', 'addmultisigaddress', 'backupwallet',
  'createwallet', 'dumpprivkey', 'dumpwallet', 'encryptwallet',
  'getaddressesbylabel', 'getaddressinfo', 'getbalance',
  'getnewaddress', 'getrawchangeaddress', 'getreceivedbyaddress',
  'getreceivedbylabel', 'gettransaction', 'getunconfirmedbalance',
  'getwalletinfo', 'importaddress', 'importmulti',
  'importprivkey', 'importprunedfunds', 'importpubkey', 'importwallet',
  'getglyphmetadata', 'keypoolrefill', 'listaddressgroupings', 'listglyph', 'listlabels', 'listlockunspent', 'mintdmint', 'mintglyph',
  'listreceivedbyaddress', 'listreceivedbylabel', 'listsinceblock',
  'listtransactions', 'listunspent', 'listwalletdir', 'listwallets',
  'loadwallet', 'lockunspent', 'removeprunedfunds', 'rescanblockchain',
  'sendmany', 'sendtoaddress', 'sethdseed', 'setlabel', 'settxfee',
  'signmessage', 'signrawtransactionwithwallet',
  'unloadwallet', 'walletcreatefundedpsbt', 'walletlock',
  'walletpassphrase', 'walletpassphrasechange', 'walletprocesspsbt',
  // ZMQ
  'getzmqnotifications',
].sort()

// Shell-style tokenizer: respects "quoted strings" and strips outer quotes so
// "[{\"txid\":\"...\"}]" becomes [{...}] before JSON.parse sees it.
function tokenize(cmd: string): string[] {
  const tokens: string[] = []
  let i = 0
  while (i < cmd.length) {
    while (i < cmd.length && /\s/.test(cmd[i])) i++
    if (i >= cmd.length) break
    if (cmd[i] === '"') {
      i++
      let tok = ''
      while (i < cmd.length && cmd[i] !== '"') {
        if (cmd[i] === '\\' && i + 1 < cmd.length) { i++; tok += cmd[i] }
        else tok += cmd[i]
        i++
      }
      i++ // closing quote
      tokens.push(tok)
    } else {
      let tok = ''
      while (i < cmd.length && !/\s/.test(cmd[i])) { tok += cmd[i]; i++ }
      tokens.push(tok)
    }
  }
  return tokens
}

function formatResult(value: unknown): string {
  if (value === null || value === undefined) return 'null'
  if (typeof value === 'string') return value
  if (typeof value === 'number' || typeof value === 'boolean') return String(value)
  return JSON.stringify(value, null, 2)
}

// RPC errors have {code, message}. Extract the message directly so that
// embedded newlines in help text render as actual line breaks, not "\n".
function formatError(value: unknown): string {
  if (typeof value === 'object' && value !== null) {
    const msg = (value as Record<string, unknown>).message
    if (typeof msg === 'string') return msg
  }
  return formatResult(value)
}

interface HistoryEntry {
  cmd: string
  result: string
  isError: boolean
}

export default function ConsolePage() {
  const [input, setInput]             = useState('')
  const [history, setHistory]         = useState<HistoryEntry[]>([])
  const [cmdHistory, setCmdHistory]   = useState<string[]>([])
  const [histIdx, setHistIdx]         = useState(-1)
  const [loading, setLoading]         = useState(false)
  const [suggestions, setSuggestions] = useState<string[]>([])
  const [sugIdx, setSugIdx]           = useState(-1)
  const bottomRef  = useRef<HTMLDivElement>(null)
  const inputRef   = useRef<HTMLInputElement>(null)
  const closeTimer = useRef<ReturnType<typeof setTimeout> | null>(null)

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [history])

  const updateSuggestions = useCallback((val: string) => {
    const parts = val.split(/\s+/)
    const firstWord = parts[0]
    if (!firstWord) { setSuggestions([]); setSugIdx(-1); return }

    // "help <partial>" — autocomplete the command argument
    if (val.includes(' ') && firstWord.toLowerCase() === 'help') {
      const partial = (parts[1] ?? '').toLowerCase()
      const matches = RPC_COMMANDS.filter(c => c.startsWith(partial))
      setSuggestions(matches.length === 1 && matches[0] === partial ? [] : matches.slice(0, 10))
      setSugIdx(-1)
      return
    }

    // No space yet — autocomplete the command name itself
    if (val.includes(' ')) { setSuggestions([]); setSugIdx(-1); return }
    const lower = firstWord.toLowerCase()
    const matches = RPC_COMMANDS.filter(c => c.startsWith(lower))
    setSuggestions(matches.length === 1 && matches[0] === lower ? [] : matches.slice(0, 10))
    setSugIdx(-1)
  }, [])

  const runCommand = async (cmd: string) => {
    const trimmed = cmd.trim()
    if (!trimmed) return

    setCmdHistory(prev => [trimmed, ...prev.slice(0, 49)])
    setHistIdx(-1)
    setInput('')
    setSuggestions([])
    setSugIdx(-1)
    setLoading(true)

    const parts = tokenize(trimmed)
    const method = parts[0]
    const params: unknown[] = parts.slice(1).map(p => {
      try { return JSON.parse(p) } catch { return p }
    })

    try {
      const res = await api.rpc(method, params)
      const formatted = res.error
        ? formatError(res.error)
        : formatResult(res.result)
      setHistory(h => [...h, { cmd: trimmed, result: formatted, isError: !!res.error }])
    } catch (e: unknown) {
      setHistory(h => [...h, {
        cmd: trimmed,
        result: e instanceof Error ? e.message : String(e),
        isError: true,
      }])
    } finally {
      setLoading(false)
      inputRef.current?.focus()
    }
  }

  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const val = e.target.value
    setInput(val)
    setHistIdx(-1)
    updateSuggestions(val)
  }

  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Escape') {
      setSuggestions([])
      setSugIdx(-1)
      return
    }

    if (e.key === 'Tab') {
      e.preventDefault()
      if (suggestions.length > 0) {
        const selected = sugIdx >= 0 ? suggestions[sugIdx] : suggestions[0]
        const isHelpArg = input.toLowerCase().startsWith('help ')
        setInput(isHelpArg ? `help ${selected} ` : selected + ' ')
        setSuggestions([])
        setSugIdx(-1)
      }
      return
    }

    if (suggestions.length > 0) {
      if (e.key === 'ArrowDown') {
        e.preventDefault()
        setSugIdx(i => Math.min(i + 1, suggestions.length - 1))
        return
      }
      if (e.key === 'ArrowUp') {
        e.preventDefault()
        setSugIdx(i => Math.max(i - 1, 0))
        return
      }
      if (e.key === 'Enter') {
        if (sugIdx >= 0) {
          e.preventDefault()
          const isHelpArg = input.toLowerCase().startsWith('help ')
          setInput(isHelpArg ? `help ${suggestions[sugIdx]} ` : suggestions[sugIdx] + ' ')
          setSuggestions([])
          setSugIdx(-1)
          return
        }
        // no item highlighted — close suggestions and run
        setSuggestions([])
        setSugIdx(-1)
        runCommand(input)
        return
      }
    } else {
      if (e.key === 'ArrowUp') {
        e.preventDefault()
        const idx = Math.min(histIdx + 1, cmdHistory.length - 1)
        setHistIdx(idx)
        if (cmdHistory[idx]) setInput(cmdHistory[idx])
        return
      }
      if (e.key === 'ArrowDown') {
        e.preventDefault()
        const idx = Math.max(histIdx - 1, -1)
        setHistIdx(idx)
        setInput(idx === -1 ? '' : cmdHistory[idx])
        return
      }
      if (e.key === 'Enter') {
        runCommand(input)
        return
      }
    }
  }

  const selectSuggestion = (cmd: string) => {
    if (closeTimer.current) clearTimeout(closeTimer.current)
    const isHelpArg = input.toLowerCase().startsWith('help ')
    setInput(isHelpArg ? `help ${cmd} ` : cmd + ' ')
    setSuggestions([])
    setSugIdx(-1)
    inputRef.current?.focus()
  }

  const handleBlur = () => {
    // small delay so onMouseDown on a suggestion fires first
    closeTimer.current = setTimeout(() => {
      setSuggestions([])
      setSugIdx(-1)
    }, 150)
  }

  const handleFocus = () => {
    if (closeTimer.current) clearTimeout(closeTimer.current)
  }

  return (
    <div>
      <h1 style={{ fontSize: '1.1rem', fontWeight: 700, marginBottom: '1rem' }}>RPC Console</h1>
      <p style={{ color: 'var(--text2)', fontSize: '0.85rem', marginBottom: '1rem' }}>
        Enter RPC commands.{' '}
        <kbd style={{ fontFamily: 'monospace', fontSize: '0.8rem', padding: '0 4px', background: 'var(--bg3)', borderRadius: '3px' }}>Tab</kbd>{' '}
        to autocomplete,{' '}
        <kbd style={{ fontFamily: 'monospace', fontSize: '0.8rem', padding: '0 4px', background: 'var(--bg3)', borderRadius: '3px' }}>↑↓</kbd>{' '}
        to cycle history.
      </p>

      <div className="card" style={{
        fontFamily: 'monospace',
        fontSize: '0.85rem',
        minHeight: '400px',
        maxHeight: '600px',
        overflowY: 'auto',
        display: 'flex',
        flexDirection: 'column',
        gap: '0.75rem',
        padding: '1rem',
      }}>
        {history.map((h, i) => (
          <div key={i}>
            <div style={{ color: 'var(--accent)' }}>
              <span style={{ color: 'var(--text2)' }}>&gt; </span>{h.cmd}
            </div>
            <pre style={{
              whiteSpace: 'pre-wrap',
              wordBreak: 'break-word',
              color: h.isError ? 'var(--red)' : 'var(--text)',
              marginTop: '0.25rem',
              paddingLeft: '1rem',
              borderLeft: `2px solid ${h.isError ? 'var(--red)' : 'var(--border)'}`,
            }}>
              {h.result}
            </pre>
          </div>
        ))}
        {history.length === 0 && (
          <p style={{ color: 'var(--text2)' }}>No commands yet. Type a command below.</p>
        )}
        <div ref={bottomRef} />
      </div>

      <div style={{ position: 'relative', marginTop: '0.5rem' }}>
        {suggestions.length > 0 && (
          <div style={{
            position: 'absolute',
            bottom: '100%',
            left: 0,
            right: 0,
            background: 'var(--bg2)',
            border: '1px solid var(--border)',
            borderRadius: 'var(--radius)',
            marginBottom: '4px',
            zIndex: 100,
            overflow: 'hidden',
            boxShadow: '0 -4px 12px rgba(0,0,0,0.4)',
          }}>
            {suggestions.map((s, i) => (
              <div
                key={s}
                onMouseDown={() => selectSuggestion(s)}
                style={{
                  padding: '0.3rem 0.75rem',
                  fontFamily: 'monospace',
                  fontSize: '0.85rem',
                  cursor: 'pointer',
                  background: i === sugIdx ? 'var(--bg3)' : 'transparent',
                  color: i === sugIdx ? 'var(--accent)' : 'var(--text)',
                  borderBottom: i < suggestions.length - 1 ? '1px solid var(--border)' : 'none',
                }}
              >
                {s}
              </div>
            ))}
          </div>
        )}
        <div className="form-row">
          <span style={{ color: 'var(--text2)', fontFamily: 'monospace', alignSelf: 'center' }}>&gt;</span>
          <input
            ref={inputRef}
            value={input}
            onChange={handleChange}
            onKeyDown={handleKeyDown}
            onBlur={handleBlur}
            onFocus={handleFocus}
            placeholder="getblockcount"
            style={{ fontFamily: 'monospace', flex: 1 }}
            autoFocus
            disabled={loading}
            autoComplete="off"
            spellCheck={false}
          />
          <button className="primary" onClick={() => runCommand(input)} disabled={loading || !input.trim()}>
            {loading ? '…' : 'Run'}
          </button>
          <button onClick={() => setHistory([])} style={{ marginLeft: '0.25rem' }}>Clear</button>
        </div>
      </div>
    </div>
  )
}
