# Radiant DNS Seeder Deployment Guide

The DNS seeder (`radiant-seeder`) is a network crawler that exposes reliable Radiant nodes via DNS. It is built as part of Radiant Core Node but runs as a separate service.

## Overview

- **Source Code**: `src/seeder/`
- **Binary**: `radiant-seeder` (built with the main project)
- **Purpose**: Provides peer discovery for new nodes joining the network

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
- **VPS**: 1 vCPU, 1GB RAM minimum
- **Network**: Static IP, low latency
- **Uptime**: 99.9%+ recommended

### DNS Configuration
You need control over a domain's DNS records. Example setup for `seed.radiantcore.org`:

1. **NS Record** (in parent zone):
   ```
   seed.radiantcore.org.  IN  NS  vps.radiantcore.org.
   ```

2. **A Record** (pointing to your VPS):
   ```
   vps.radiantcore.org.   IN  A   203.0.113.50
   ```

## Building

The seeder is built automatically with the main project:

```bash
mkdir build && cd build
cmake -GNinja .. -DBUILD_RADIANT_SEEDER=ON
ninja radiant-seeder
```

## Running

### Basic Usage

```bash
./radiant-seeder -host=seed.radiantcore.org -ns=vps.radiantcore.org -mbox=admin.radiantcore.org
```

### Command Line Options

| Option | Description | Example |
|--------|-------------|---------|
| `-host` | DNS hostname to serve | `seed.radiantcore.org` |
| `-ns` | Hostname of the NS server | `vps.radiantcore.org` |
| `-mbox` | SOA email (@ replaced with .) | `admin.radiantcore.org` |
| `-port` | DNS port (default: 53) | `5353` |
| `-threads` | Crawler threads (default: 24) | `12` |
| `-dnsthreads` | DNS server threads (default: 4) | `2` |
| `-onion` | Tor proxy for .onion crawling | `127.0.0.1:9050` |

### Running as Non-Root

DNS typically requires port 53 (privileged). Use iptables to redirect:

```bash
# Redirect port 53 to unprivileged port 5353
sudo iptables -t nat -A PREROUTING -p udp --dport 53 -j REDIRECT --to-port 5353

# Run seeder on port 5353
./radiant-seeder -host=seed.radiantcore.org -ns=vps.radiantcore.org -port=5353
```

## Systemd Service

Create `/etc/systemd/system/radiant-seeder.service`:

```ini
[Unit]
Description=Radiant DNS Seeder
After=network.target

[Service]
Type=simple
User=radiant
Group=radiant
ExecStart=/usr/local/bin/radiant-seeder \
    -host=seed.radiantcore.org \
    -ns=vps.radiantcore.org \
    -mbox=admin.radiantcore.org \
    -port=5353
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl enable radiant-seeder
sudo systemctl start radiant-seeder
```

## Updating chainparams.cpp

Once your seeder is running and stable (30+ days recommended), add it to the node's default seed list:

**File**: `src/chainparams.cpp`

```cpp
// Mainnet seeds
vSeeds.emplace_back("seed.radiantcore.org");

// Testnet seeds  
vSeeds.emplace_back("testnet-seed.radiantcore.org");
```

## Monitoring

### Check DNS Resolution

```bash
dig seed.radiantcore.org
```

Should return multiple A records (peer IPs).

### Check Seeder Logs

```bash
journalctl -u radiant-seeder -f
```

### Verify Peer Count

The seeder maintains statistics. After running for a few days, you should see:
- 1000+ known nodes
- 100+ good nodes (responsive, correct protocol)

## Seed List Generation

The seeder can generate static seed lists for compilation into releases:

```bash
# Run seeder for 30+ days, then:
./radiant-seeder -dumpfile=dnsseed.dump

# Use contrib/seeds scripts to process
cd contrib/seeds
python3 makeseeds.py < dnsseed.dump > nodes_main.txt
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Port 53 permission denied | Use iptables redirect (see above) |
| No peers found | Check network connectivity, firewall |
| DNS not resolving | Verify NS records, check seeder logs |
| Low peer count | Wait longer (crawling takes time) |

## Current Mainnet Seeders

| Domain | Operator | Status |
|--------|----------|--------|
| TBD | TBD | Planned |

## See Also

- [src/seeder/README.md](/src/seeder/README.md) - Source code documentation
- [contrib/seeds/](/contrib/seeds/) - Seed list generation scripts
