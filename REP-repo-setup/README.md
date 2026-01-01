# Radiant Enhancement Proposals (REPs)

Radiant Enhancement Proposals (REPs) are design documents providing information to the Radiant community, or describing a new feature for Radiant or its processes or environment. REPs are the primary mechanism for proposing major new features, collecting community input on issues, and documenting design decisions.

## REP Summary

| REP | Title | Author | Status | Type |
|-----|-------|--------|--------|------|
| **[2001](REP-2001.md)** | REP Process Definition | Radiant Core Contributors | 🔄 Active | Process |

*Legend: 📝 Draft | 🔄 Active | ✅ Final | ❌ Rejected | 🚫 Withdrawn*

## Quick Start

- **[REP-2001](REP-2001.md)** - REP Process Definition (start here)
- **[REP Template](rep-template.md)** - Template for creating new REPs
- **[Active REPs](active/)** - Currently under consideration
- **[Final REPs](final/)** - Accepted and implemented
- **[All REPs](README.md)** - Complete list by category

## REP Categories

### Standard Track (REP-1 to REP-999)
Protocol changes that affect most or all Radiant implementations:
- Consensus rule changes
- P2P network protocol changes
- New opcodes or transaction types
- Block validation changes
- RPC interface changes

### Informational (REP-1001 to REP-1999)
Design issues, guidelines, and informational content:
- Architecture overviews
- Security best practices
- Mining guidelines
- Wallet integration standards

### Process (REP-2000 to REP-2999)
Process changes and governance:
- REP process changes
- Release processes
- Security disclosure processes
- Governance guidelines

## REP Status Legend

- 📝 **Draft**: Initial proposal, being discussed
- 🔄 **Active**: Under consideration, implementation may begin
- ✅ **Final**: Accepted and implemented
- ❌ **Rejected**: Considered and rejected
- 🚫 **Withdrawn**: Author withdrew the proposal

## Current REPs

### Standard Track REPs (Protocol Changes)

| REP | Title | Author | Status | Description |
|-----|-------|--------|--------|-------------|
| *No Standard Track REPs yet* | | | | |

### Informational REPs (Guidelines & Overviews)

| REP | Title | Author | Status | Description |
|-----|-------|--------|--------|-------------|
| *No Informational REPs yet* | | | | |

### Process REPs (Governance & Procedures)

| REP | Title | Author | Status | Description |
|-----|-------|--------|--------|-------------|
| **[2001](REP-2001.md)** | REP Process Definition | Radiant Core Contributors | 🔄 Active | Defines the REP process, format, and guidelines for submitting and managing REPs |

## Submitting a REP

1. **Discuss First**: Talk about your idea on community channels
2. **Use Template**: Copy [rep-template.md](rep-template.md) 
3. **Create PR**: Submit as a pull request to this repository
4. **Community Review**: Participate in the review process
5. **Implementation**: Develop reference implementation (for Standard Track)

## Community

- **Discussions**: [GitHub Discussions](https://github.com/Radiant-Core/REP/discussions)
- **Issues**: [GitHub Issues](https://github.com/Radiant-Core/REP/issues)
- **Core Repository**: [Radiant-Core/Radiant-Core](https://github.com/Radiant-Core/Radiant-Core)

## Repository Structure

```
REP/
├── README.md              # This file
├── REP-2001.md            # REP Process Definition
├── rep-template.md        # Template for new REPs
├── active/                # Active REPs
├── final/                 # Final REPs
├── rejected/              # Rejected REPs
├── withdrawn/             # Withdrawn REPs
└── draft/                 # Draft REPs (PRs)
```

## License

All REPs are licensed under the MIT License unless otherwise specified in the individual REP.

---

*Inspired by Bitcoin Improvement Proposals (BIPs), Ethereum Improvement Proposals (EIPs), and Python Enhancement Proposals (PEPs).*
