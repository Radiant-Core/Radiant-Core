// API client for the Radiant Core WebUI backend.

const BASE = '/webui/api'

let _token: string | null = localStorage.getItem('webui_token')
let _onUnauthorized: (() => void) | null = null

export function setToken(t: string | null) {
  _token = t
  if (t) localStorage.setItem('webui_token', t)
  else    localStorage.removeItem('webui_token')
}

export function getToken() { return _token }

export function setOnUnauthorized(cb: () => void) { _onUnauthorized = cb }

async function requestBlob(method: string, path: string): Promise<Blob> {
  const headers: Record<string, string> = {}
  if (_token) headers['Authorization'] = `Bearer ${_token}`
  const res = await fetch(`${BASE}${path}`, { method, headers, cache: 'no-store' })
  if (!res.ok) {
    const text = await res.text()
    let msg = res.statusText
    try { msg = (JSON.parse(text) as { error: string }).error || msg } catch {}
    throw new Error(msg)
  }
  return res.blob()
}

async function request<T>(method: string, path: string, body?: unknown, isLogin = false, signal?: AbortSignal): Promise<T> {
  const headers: Record<string, string> = { 'Content-Type': 'application/json' }
  if (_token) headers['Authorization'] = `Bearer ${_token}`
  const res = await fetch(`${BASE}${path}`, {
    method,
    headers,
    body: body !== undefined ? JSON.stringify(body) : undefined,
    signal,
    cache: 'no-store',
  })
  if (res.status === 401) {
    if (isLogin) throw new Error('Invalid password')
    const hadToken = !!_token   // capture before clearing
    setToken(null)
    // Only fire the callback when a real session expired; if there was no
    // token the probe was already running unauthenticated — firing here would
    // re-increment probeKey and create an infinite probe loop.
    if (hadToken) _onUnauthorized?.()
    throw new Error('Session expired')
  }
  const text = await res.text()
  if (!text.trim()) throw new Error('No response from node — it may still be initializing, please try again')
  let json: unknown
  try {
    json = JSON.parse(text)
  } catch {
    throw new Error('Malformed response from node — it may still be initializing, please try again')
  }
  if (!res.ok) throw new Error((json as Record<string, string>).error || res.statusText)
  return json as T
}

export const api = {
  // Auth
  authInfo: (signal?: AbortSignal)    => request<{ mode: string }>('GET', '/auth/info', undefined, false, signal),
  login:    (password: string)        => request<{ token: string }>('POST', '/auth/login', { password }, true),
  logout:   ()                        => request<void>('POST', '/auth/logout'),

  // Node
  nodeStatus:  ()                     => request<NodeStatus>('GET', '/node/status'),
  nodeMining:  ()                     => request<MiningInfo>('GET', '/node/mining'),
  nodeFeatures:()                     => request<Record<string, unknown>>('GET', '/node/features'),
  nodePeers:   ()                     => request<{ peers: Peer[] }>('GET', '/node/peers'),
  nodeBanned:  ()                     => request<{ banned: BanEntry[] }>('GET', '/node/banned'),
  peerBan:     (address: string, bantime?: number) => request('POST', '/node/peers/ban', { address, bantime }),
  peerUnban:   (address: string)      => request('POST', '/node/peers/unban', { address }),
  peerDisconnect: (id: number)        => request('POST', '/node/peers/disconnect', { id }),
  peerAdd:     (node: string)         => request('POST', '/node/peers/add', { node }),

  // Verify
  verifyMessage: (address: string, signature: string, message: string) =>
    request<{ valid: boolean }>('POST', '/verifymessage', { address, signature, message }),

  // RPC console
  rpc: (method: string, params: unknown[], wallet?: string) =>
    request<{ result: unknown; error: unknown }>('POST', '/rpc',
      wallet ? { method, params, wallet } : { method, params }),

  // Wallets
  walletsLoaded:    ()                => request<WalletsLoaded>('GET', '/wallets/loaded'),
  walletsAvailable: ()                => request<unknown>('GET', '/wallets/available'),
  walletLoad:       (name: string)    => request('POST', '/wallets/load', { name }),
  walletCreate:     (name: string)    => request('POST', '/wallets/create', { name }),
  walletUnload:     (name: string)    => request('POST', '/wallets/unload', { name }),

  // Per-wallet
  walletSummary:   (name: string)     => request<WalletSummary>('GET', `/wallet/${encodeURIComponent(name)}/summary`),
  walletTxs:       (name: string)     => request<unknown>('GET', `/wallet/${encodeURIComponent(name)}/transactions`),
  walletAddresses: (name: string)     => request<AddressEntry[]>('GET', `/wallet/${encodeURIComponent(name)}/addresses`),
  walletNewAddress:(name: string, label?: string) =>
    request<string>('POST', `/wallet/${encodeURIComponent(name)}/receive-address`, { label: label ?? '' }),
  walletSetLabel:  (name: string, address: string, label: string) =>
    request<unknown>('POST', `/wallet/${encodeURIComponent(name)}/set-address-label`, { address, label }),
  walletSend:      (name: string, address: string, amount: number, comment?: string, changeAddress?: string) =>
    request<string>('POST', `/wallet/${encodeURIComponent(name)}/send`, { address, amount, comment, changeAddress }),
  walletSendWithInputs: (name: string, address: string, amount: number, inputs: Array<{txid: string; vout: number}>, changeAddress?: string) =>
    request<string>('POST', `/wallet/${encodeURIComponent(name)}/send-with-inputs`, { address, amount, inputs, changeAddress }),
  walletSignMessage:(name: string, address: string, message: string) =>
    request<string>('POST', `/wallet/${encodeURIComponent(name)}/signmessage`, { address, message }),
  walletUnlock:    (name: string, passphrase: string, timeout?: number) =>
    request('POST', `/wallet/${encodeURIComponent(name)}/unlock`, { passphrase, timeout: timeout ?? 300 }),
  walletLock:      (name: string)     => request('POST', `/wallet/${encodeURIComponent(name)}/lock`, {}),
  walletEncrypt:   (name: string, passphrase: string) =>
    request('POST', `/wallet/${encodeURIComponent(name)}/encrypt`, { passphrase }),
  walletChangePassphrase: (name: string, oldPassphrase: string, newPassphrase: string) =>
    request('POST', `/wallet/${encodeURIComponent(name)}/change-passphrase`, { oldPassphrase, newPassphrase }),
  walletUTXOs:    (name: string)        => request<UTXOEntry[]>('GET', `/wallet/${encodeURIComponent(name)}/utxos`),
  walletCoins:    (name: string, showAll = false) =>
    request<CoinSummary>('GET', `/wallet/${encodeURIComponent(name)}/coins${showAll ? '?all=1' : ''}`),
  walletGlyph:    (name: string)        => request<GlyphRPCEntry[]>('GET', `/wallet/${encodeURIComponent(name)}/glyph`),
  walletGlyphMetadata: (name: string, ref: string) =>
    request<{ metadata: string; source: string; txid: string }>('GET', `/wallet/${encodeURIComponent(name)}/glyph-metadata?ref=${ref}`),
  walletPSBTCreate:(name: string, inputs: unknown[], outputs: unknown[], changeAddress?: string) =>
    request<unknown>('POST', `/wallet/${encodeURIComponent(name)}/psbt/create`, { inputs, outputs, changeAddress }),
  walletPSBTSign: (name: string, psbt: string) =>
    request<unknown>('POST', `/wallet/${encodeURIComponent(name)}/psbt/sign`, { psbt }),
  walletBackupExport: (name: string) =>
    requestBlob('POST', `/wallet/${encodeURIComponent(name)}/backup-export`),

  // SSE
  sseTicket: () => request<{ ticket: string }>('POST', '/events/ticket', {}),

  // Settings
  getSettings:  ()                       => request<WebUISettings>('GET', '/settings'),
  saveSettings: (s: WebUISettings)       => request<WebUISettings>('POST', '/settings', s),
}

// Types
export interface MiningInfo {
  blocks: number
  currentblocksize: number
  currentblocktx: number
  difficulty: number
  networkhashps: number
  pooledtx: number
  chain: string
  warnings: string
}

export interface NodeStatus {
  version: string
  network: string
  blocks: number
  headers: number
  bestblockhash: string
  verificationprogress: number
  initialblockdownload: boolean
  connections: number
  mempoolsize: number
  uptime: number
}

export interface Peer {
  id: number
  addr: string
  subver: string
  inbound: boolean
  bytesrecv: number
  bytessent: number
  conntime: number
  lastsend: number
  lastrecv: number
  ping: number
  startingheight: number
  synced_headers?: number
  synced_blocks?: number
  version: number
  addrlocal: string
}

export interface BanEntry {
  address: string
  banned_until: number
  ban_created: number
}

export interface WalletsLoaded {
  walletSupportEnabled: boolean
  wallets: Array<{ name: string; encrypted: boolean; locked: boolean }>
}

export interface WalletSummary {
  name: string
  encrypted: boolean
  locked: boolean
  balance: number
  balanceSatoshi: number
  unconfirmed: number
  immature: number
}

export interface AddressEntry {
  address: string
  label: string
  amount: number
  confirmations: number
  txids?: string[]
  involvesWatchonly?: boolean
}

export interface UTXOEntry {
  txid: string
  vout: number
  address: string
  label: string
  scriptPubKey?: string
  amount: number
  confirmations: number
  spendable: boolean
  solvable: boolean
  safe: boolean
}

export interface CoinEntry {
  txid: string
  vout: number
  address: string
  amount: number
  confirmations?: number
  is_spent: boolean
  spent_by?: string
}

export interface CoinSummary {
  wallet: string
  count: number
  total_value: number
  utxos: CoinEntry[]
}

export interface GlyphRPCEntry {
  txid: string
  vout: number
  ref: string        // 72-char hex (36-byte LE token ref)
  type: 'ft' | 'nft'
  address: string
  scriptPubKey: string
  photons: number    // Radiant's base unit (analogous to satoshis)
  confirmations: number
  revealoutpoint?: string  // txid of the reveal/metadata tx containing CBOR (set when found)
}

export interface WebUISettings {
  explorerTxTemplate?:      string  // must contain {txid}
  explorerAddressTemplate?: string  // must contain {address}
  explorerBlockTemplate?:   string  // must contain {height}
}

const SETTINGS_CACHE_KEY = 'webui_settings'

const DEFAULT_TX_TEMPLATE = 'https://explorer.radiant4people.com/tx/{txid}'
const DEFAULT_ADDRESS_TEMPLATE = 'https://explorer.radiant4people.com/address/{address}'
const DEFAULT_BLOCK_TEMPLATE   = 'https://explorer.radiant4people.com/block-height/{height}'

export const EXPLORER_DEFAULTS = {
  tx:      DEFAULT_TX_TEMPLATE,
  address: DEFAULT_ADDRESS_TEMPLATE,
  block:   DEFAULT_BLOCK_TEMPLATE,
}

export function loadCachedSettings(): WebUISettings {
  try {
    const raw = localStorage.getItem(SETTINGS_CACHE_KEY)
    if (raw) {
      const parsed = JSON.parse(raw) as WebUISettings & { explorerBaseUrl?: string }
      // Migrate old explorerBaseUrl to per-type templates
      if (parsed.explorerBaseUrl && !parsed.explorerTxTemplate) {
        const base = parsed.explorerBaseUrl.replace(/\/$/, '')
        return {
          explorerTxTemplate:      `${base}/tx/{txid}`,
          explorerAddressTemplate: `${base}/address/{address}`,
          explorerBlockTemplate:   `${base}/block-height/{height}`,
        }
      }
      return parsed
    }
  } catch {}
  return {}
}

export function cacheSettings(s: WebUISettings) {
  localStorage.setItem(SETTINGS_CACHE_KEY, JSON.stringify(s))
}

export function explorerTxUrl(txid: string): string {
  return (loadCachedSettings().explorerTxTemplate || DEFAULT_TX_TEMPLATE).replace('{txid}', txid)
}

export function explorerAddressUrl(address: string): string {
  return (loadCachedSettings().explorerAddressTemplate || DEFAULT_ADDRESS_TEMPLATE).replace('{address}', address)
}

export function explorerBlockUrl(height: number): string {
  return (loadCachedSettings().explorerBlockTemplate || DEFAULT_BLOCK_TEMPLATE).replace('{height}', String(height))
}
