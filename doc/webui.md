# Radiant Core Web UI

The Radiant Core Web UI is an optional, browser-based interface for managing a running `radiantd` or `Radiant-Qt` node. It provides wallet management, token (Glyph) operations, an interactive RPC console, and node monitoring — all served directly from the daemon over a local HTTP endpoint with no external dependencies.

## Building

The Web UI is compiled in by default. It can be excluded with the `WITH_WEBUI` CMake option if a smaller binary is preferred:

```bash
cmake -GNinja .. -DWITH_WEBUI=OFF
ninja
```

When included, the pre-built frontend assets are embedded directly into the binary. No separate web server or Node.js installation is required at runtime.

## Enabling at Runtime

The Web UI is disabled by default even when compiled in. Enable it via the command line or `radiant.conf`:

```bash
radiantd -webui=1
```

Or in `radiant.conf`:

```
webui=1
```

The node must also be running with `-server=1` (which is the default for `radiantd`).

The WebUI shares the same HTTP server as the RPC interface, so it is accessible on whatever `rpcport` is configured (default 8332):

```
http://127.0.0.1:8332/webui/
```

## Configuration Options

| Option | Default | Description |
|---|---|---|
| `-webui` | `0` | Enable the Web UI endpoint (served on the RPC port) |
| `-webuipassword` | *(none)* | Fixed password for authentication (see [Authentication](#authentication) below) |
| `-webuiassets` | *(none)* | Serve UI files from this directory instead of embedded assets (development use) |

Because the WebUI runs on the RPC port, all existing RPC options (`-rpcport`, `-rpcbind`, `-rpcallowip`) also govern WebUI connectivity.

### Example

```
webui=1
rpcport=17332
webuipassword=my-strong-password
```

Accessible at `http://127.0.0.1:17332/webui/`.

## Authentication

Two authentication modes are supported:

**Cookie-based (default):** On startup, the daemon writes a randomly-generated token to `webui.cookie` in the data directory. The browser reads this file once on first login and caches the token in `localStorage`. This mode is recommended for local use — no password needs to be configured.

**Password-based:** Set `-webuipassword=<password>` to require a fixed password instead. The password is hashed with bcrypt before comparison; login attempts are rate-limited to protect against brute force.

Sessions are stateless bearer tokens valid for the lifetime of the daemon process.

## Security

The Web UI is designed for local use only.

- **Bound to loopback by default.** `-webuibind` defaults to `127.0.0.1`, so the UI is not reachable from other machines unless explicitly changed.
- **DNS rebinding protection.** All requests are validated against an allowlist of permitted `Host` headers (`127.0.0.1`, `localhost`, `::1`, and any address from `-webuibind` or `-rpcbind`). Requests with unexpected `Host` headers are rejected with HTTP 403.
- **No TLS.** The Web UI does not terminate HTTPS. If remote access is needed, place it behind a reverse proxy (nginx, Caddy) that handles TLS termination and restricts access.
- **Private keys never leave the daemon.** All transaction signing is performed server-side by the wallet RPC layer. The browser only handles unsigned parameters and receives signed hex or txids.

## Features

### Node

The **Node** page shows the current sync status, software version, network, block height, mempool size, uptime, and connection count with a live progress bar during initial block download.

The **Peers** sub-tab lists all connected peers with address, version, direction (inbound/outbound), ping, and data transfer totals. Peers can be disconnected or banned directly from the UI.

### Wallets

Wallets are loaded and unloaded from a dropdown. Multiple wallets can be managed simultaneously. New wallets can be created from the UI.

Each loaded wallet exposes the following tabs:

#### Overview
Balance (confirmed, unconfirmed, immature), and a summary of the most recent transactions.

#### Transactions
Full transaction history from `listsinceblock`, sorted newest-first with pagination (25 per page).

Dust transactions (absolute amount < 0.001 RXD) are hidden by default — these are typically the 1-satoshi singleton outputs created during Glyph token minting and add noise to the list. A **Hide dust** checkbox with a count of hidden entries allows them to be revealed.

#### UTXOs
All unspent outputs from `listunspent`, with address, label, amount, confirmations, and txid:vout.

#### Send
Send RXD to any address. By default the wallet selects inputs automatically (`sendtoaddress`).

**Manual input selection:** Checking "Choose inputs manually" reveals a scrollable UTXO table. Each row can be toggled (click anywhere on the row or the checkbox). Select All / Clear buttons are provided. The selected total and UTXO count are shown below the table, with a warning if the selection is insufficient to cover the requested amount. When UTXOs are selected, the send path uses `createrawtransaction` → `fundrawtransaction` (adds change and fee) → `signrawtransactionwithwallet` → `sendrawtransaction`.

#### PSBT
Create a funded Partially Signed Bitcoin Transaction (`walletcreatefundedpsbt`) or sign an existing PSBT (`walletprocesspsbt`). Useful for offline signing workflows and hardware wallet integration.

#### Sign / Verify
Sign an arbitrary message with a wallet address (`signmessage`) and verify signed messages (`verifymessage`).

#### Consolidate
Combine many small UTXOs into a single output to reduce future transaction fees.

#### Tokens
Lists all Glyph-protocol tokens (fungible tokens and NFTs) held by the wallet, fetched via `listglyph`. For each token, the reference outpoint, type, balance or singleton value, and address are displayed. Metadata (name, ticker, description, image) is decoded from the on-chain CBOR envelope and shown inline.

**Minting new tokens** is available via the Mint sub-panel:

- **Direct FT:** Deploys a fungible token with a fixed supply using a commit + reveal transaction pair signed by the wallet (`mintglyph`).
- **NFT:** Deploys a non-fungible singleton token (`mintglyph` with singleton type).
- **dMint FT:** Deploys a proof-of-work mintable fungible token using the dMint V2 contract (`mintdmint`). Configuration options include total mints, reward per mint, initial difficulty, algorithm (BLAKE3 / SHA256d / K12), difficulty adjustment algorithm (ASERT / LWMA / fixed), target block time, ASERT half-life, number of contracts, and premine amount.

All token types support optional embedded content (image, file, or URL) stored in the Glyph `main` CBOR field. A preview of the image is shown before broadcasting. After a successful mint, the FT reference, NFT reference, and contract references are displayed with copy buttons.

Both commit and reveal transactions are validated with `testmempoolaccept` before broadcasting.

#### Security
Encrypt the wallet, change the passphrase, lock, and unlock. The current lock state is shown in the wallet selector header.

### RPC Console

An interactive RPC console with:

- Tab-completion for all known RPC commands (shown in a dropdown as you type)
- Up/Down arrow command history (last 50 commands)
- JSON-formatted output for object and array results
- Error messages rendered with proper newlines (help text displays correctly)

Any RPC command supported by the node can be run, including wallet commands. Parameters are parsed as JSON if valid, otherwise treated as strings.

### Settings

Configure the block explorer base URL used for transaction and address links throughout the UI (default: `https://explorer2.rxd-radiant.com`). The setting is persisted in browser `localStorage`.

## Development

To work on the frontend with hot-reload, run the Vite dev server alongside a local node that has `-webuiassets` pointing to the built UI output, or configure the Vite proxy to forward API calls to the daemon:

```bash
cd src/webui/ui
npm install
npm run dev
```

The frontend is a React + TypeScript single-page application built with Vite. The production build is embedded into the binary at compile time via `src/webui/embed_assets.py`, which generates `src/webui/assets.h` containing the gzipped assets as C++ byte arrays.

To rebuild the embedded assets after frontend changes:

```bash
cd src/webui/ui && npm run build
cd ../..
python3 src/webui/embed_assets.py src/webui/ui/dist src/webui/assets.h
```

Then recompile the daemon.
