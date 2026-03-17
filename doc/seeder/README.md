# Radiant DNS Seeder Deployment Guide

The DNS seeder (`radiant-seeder`) is a network crawler that exposes reliable Radiant nodes via DNS. It is built as part of Radiant Core but runs as a separate service.

## Overview

- **Source Code**: `src/seeder/`
- **Binary**: `radiant-seeder` (built with the main project)
- **Purpose**: Provides peer discovery for new nodes joining the network
- **Version Filtering**: Supports minimum client version and block height enforcement

## Architecture

```
┌─────────────────┐     DNS Query      ┌──────────────────┐
│   New Node      │ ─────────────────► │  DNS Seeder      │
│   (radiantd)    │                    │  (radiant-seeder)│
└─────────────────┘                    └──────────────────┘
        │                                      │
        │         Peer List (A records)        │
        │ ◄─────────────────────────────────── │
        │                                      │
        ▼                                      ▼
┌─────────────────┐                    ┌──────────────────┐
│  Connect to     │                    │  Crawls network  │
│  returned peers │                    │  every few hours │
└─────────────────┘                    └──────────────────┘
```

## Deployment Requirements

### Hardware
- **VPS**: 1 vCPU, 1GB RAM minimum (can share with existing website)
- **Network**: Static IP, low latency
- **Uptime**: 99.9%+ recommended
- **Ports**: UDP 53 (DNS) must be open inbound

### DNS Configuration

The seeder acts as an authoritative DNS server for a subdomain. You need two
DNS records in the `radiantcore.org` zone:

**Step 1 — NS record** (delegates `seed.radiantcore.org` to your VPS):
```
seed.radiantcore.org.  IN  NS  vps.radiantcore.org.
```

**Step 2 — A record** (points to your VPS IP):
```
vps.radiantcore.org.   IN  A   <YOUR_VPS_IP>
```

> **Cloudflare note**: If `radiantcore.org` is behind Cloudflare, the `vps`
> A record must have the **orange cloud OFF** (DNS only / grey cloud).
> Cloudflare does not proxy UDP port 53.

## Building

### Option A: Build on VPS (Native)

```bash
# Install dependencies (Ubuntu 22.04/24.04)
sudo apt-get update && sudo apt-get install -y \
    build-essential cmake ninja-build pkg-config libtool autoconf automake \
    libevent-dev libboost-chrono-dev libboost-filesystem-dev \
    libboost-test-dev libboost-thread-dev libssl-dev \
    libminiupnpc-dev libzmq3-dev libdb++-dev git python3

# Clone and build (seeder only — no daemon, wallet, or GUI)
git clone https://github.com/Radiant-Core/Radiant-Core.git
cd Radiant-Core
mkdir build && cd build
cmake -GNinja .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_RADIANT_SEEDER=ON \
    -DBUILD_RADIANT_DAEMON=OFF \
    -DBUILD_RADIANT_WALLET=OFF \
    -DBUILD_RADIANT_QT=OFF \
    -DBUILD_RADIANT_CLI=OFF \
    -DBUILD_RADIANT_TX=OFF \
    -DBUILD_RADIANT_ZMQ=OFF
ninja radiant-seeder

# Install
sudo cp src/seeder/radiant-seeder /usr/local/bin/
```

### Option B: Docker

```bash
docker build -f docker/Dockerfile.seeder -t radiant-seeder .

docker run -d --name radiant-seeder \
    -p 5353:53/udp \
    --restart=unless-stopped \
    radiant-seeder \
    -host=seed.radiantcore.org \
    -ns=vps.radiantcore.org \
    -mbox=admin.radiantcore.org \
    -minclientversion=2.1.2 \
    -minheight=410000
```

## Running

### Recommended Command (V2 Hard Fork Enforcement)

```bash
./radiant-seeder \
    -host=seed.radiantcore.org \
    -ns=vps.radiantcore.org \
    -mbox=admin.radiantcore.org \
    -port=5353 \
    -minclientversion=2.1.2 \
    -minheight=410000
```

This ensures the seeder **only returns nodes** that are:
1. Running Radiant Core **v2.1.2 or higher** (subversion string check)
2. Synced past **block 410,000** (V2 hard fork activation height)

### Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `-host` | DNS hostname to serve | *(required)* |
| `-ns` | Hostname of the NS server | *(required)* |
| `-mbox` | SOA email (@ replaced with .) | *(required)* |
| `-port` | UDP port to listen on | `53` |
| `-threads` | Crawler threads | `96` |
| `-dnsthreads` | DNS server threads | `4` |
| `-minclientversion` | Minimum client version (e.g. `2.1.2`) | none |
| `-minheight` | Minimum block height nodes must report | last checkpoint |
| `-onion` | Tor proxy for .onion crawling | none |
| `-proxyipv4` | SOCKS5 proxy for IPv4 | none |
| `-proxyipv6` | SOCKS5 proxy for IPv6 | none |
| `-filter` | Service flag filter whitelist | `1,5,9,13` |
| `-wipeban` | Wipe ban list on startup | `0` |
| `-wipeignore` | Wipe ignore list on startup | `0` |
| `-reseed` | Reseed from fixed seed list | `0` |

### Version Filtering Details

**`-minclientversion`** parses the subversion string from the Bitcoin protocol
VERSION handshake. Radiant Core reports strings like `/radiant-node:2.1.2/`.
The seeder extracts the version after the colon and compares numerically.
Nodes below the minimum are excluded from DNS responses but continue to be
crawled (they may upgrade later).

**`-minheight`** ensures nodes report a starting height past the specified
block. For the V2 hard fork, use `410000` to guarantee returned nodes are on
the correct post-fork chain.

### Running as Non-Root

DNS requires port 53 (privileged). Use iptables to redirect:

```bash
# Redirect incoming UDP port 53 to unprivileged port 5353
sudo iptables -t nat -A PREROUTING -p udp --dport 53 -j REDIRECT --to-port 5353

# Make persistent across reboots
sudo apt-get install -y iptables-persistent
sudo netfilter-persistent save

# Now run seeder on unprivileged port 5353
./radiant-seeder -host=seed.radiantcore.org -ns=vps.radiantcore.org -port=5353
```

## Complete VPS Setup (radiantcore.org)

Step-by-step guide for the VPS hosting `radiantcore.org`.

### 1. DNS Records

Add in your DNS provider (Cloudflare, Namecheap, etc.):

| Type | Name | Value | TTL |
|------|------|-------|-----|
| NS | `seed` | `vps.radiantcore.org` | 86400 |
| A | `vps` | `<YOUR_VPS_IP>` | 300 |

### 2. Firewall

```bash
sudo ufw allow 53/udp
sudo ufw status
```

### 3. Create Service User

```bash
sudo useradd -r -s /usr/sbin/nologin -d /opt/radiant-seeder radiant-seeder
sudo mkdir -p /opt/radiant-seeder
sudo chown radiant-seeder:radiant-seeder /opt/radiant-seeder
```

### 4. Port Redirect (run as non-root)

```bash
sudo iptables -t nat -A PREROUTING -p udp --dport 53 -j REDIRECT --to-port 5353
sudo apt-get install -y iptables-persistent
sudo netfilter-persistent save
```

### 5. Systemd Service

Create `/etc/systemd/system/radiant-seeder.service`:

```ini
[Unit]
Description=Radiant DNS Seeder
After=network.target

[Service]
Type=simple
User=radiant-seeder
Group=radiant-seeder
WorkingDirectory=/opt/radiant-seeder
ExecStart=/usr/local/bin/radiant-seeder \
    -host=seed.radiantcore.org \
    -ns=vps.radiantcore.org \
    -mbox=admin.radiantcore.org \
    -port=5353 \
    -threads=24 \
    -dnsthreads=4 \
    -minclientversion=2.1.2 \
    -minheight=410000
Restart=always
RestartSec=10
LimitNOFILE=8192

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable radiant-seeder
sudo systemctl start radiant-seeder
```

### 6. Verify

```bash
# Check service status
sudo systemctl status radiant-seeder

# Check seeder logs
journalctl -u radiant-seeder -f

# Test DNS resolution (from another machine)
dig seed.radiantcore.org

# Should return multiple A records (IPs of Radiant nodes)
```

The seeder takes **a few hours** to crawl enough nodes. Initially it seeds
from the hardcoded fixed IPs in `chainparamsseeds.h`, then discovers more
peers by asking each node for its address list.

### 7. Expected Timeline

| Time | Expected State |
|------|---------------|
| 0-1 hours | Crawling fixed seeds, 10-50 nodes discovered |
| 1-8 hours | 100-500 nodes discovered, 50+ good nodes |
| 1-7 days | 500-2000 nodes discovered, 100+ good nodes |
| 30+ days | Stable peer database, ready for static seed list generation |

## Monitoring

### Check DNS Resolution

```bash
dig seed.radiantcore.org
```

Should return multiple A records (peer IPs).

### Check Seeder Stats

The seeder prints a status line to stdout:

```
[26-03-16 18:30:00] 150/450 available (200 tried in 3600s, 50 new, 400 active), 5 banned; 120 DNS requests, 85 db queries
```

### View Dump File

The seeder periodically writes `dnsseed.dump` in its working directory:

```bash
head -20 /opt/radiant-seeder/dnsseed.dump
```

## Updating chainparams.cpp

The DNS seed `seed.radiantcore.org` has been added to mainnet in
`src/chainparams.cpp`:

```cpp
vSeeds.emplace_back("seed.radiantcore.org");
```

Nodes running this version (or later) will automatically query the seeder
when they need peers.

## Seed List Generation

After running 30+ days, the seeder can generate static seed lists:

```bash
# The dump file is written automatically
cat /opt/radiant-seeder/dnsseed.dump

# Process into fixed seed format
cd contrib/seeds
python3 makeseeds.py < /opt/radiant-seeder/dnsseed.dump > nodes_main.txt
python3 generate-seeds.py . > ../../src/chainparamsseeds.h
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Port 53 permission denied | Use iptables redirect (see above) |
| No peers found | Check firewall, ensure port 7333 outbound is open |
| DNS not resolving | Verify NS record with `dig -t NS seed.radiantcore.org` |
| Low peer count | Wait longer (crawling takes days to stabilize) |
| Only old-version nodes | Check `-minclientversion` is set correctly |
| Seeder exits immediately | Check `-host` and `-ns` are both provided |

## Current Mainnet Seeders

| Domain | Operator | Status |
|--------|----------|--------|
| `seed.radiantcore.org` | Radiant Core | Deploying |

## See Also

- [src/seeder/README.md](/src/seeder/README.md) - Source code documentation
- [contrib/seeds/](/contrib/seeds/) - Seed list generation scripts
- [docker/Dockerfile.seeder](/docker/Dockerfile.seeder) - Docker build
