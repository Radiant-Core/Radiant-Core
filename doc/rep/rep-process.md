# Radiant Enhancement Proposals (REPs)

## Overview

Radiant Enhancement Proposals (REPs) are design documents providing information to the Radiant community, or describing a new feature for Radiant or its processes or environment. REPs are the primary mechanism for proposing major new features, collecting community input on issues, and documenting design decisions.

The REP process is inspired by Bitcoin Improvement Proposals (BIPs) and adapted for the Radiant ecosystem's unique needs and architecture.

**Repository:** [https://github.com/Radiant-Core/REP](https://github.com/Radiant-Core/REP)

All REPs are maintained in the dedicated REP repository. Please submit new REPs and participate in discussions there.

## REP Types

### REP-XXX: Standard Track
Standard Track REPs describe changes that affect most or all Radiant implementations, such as:
- Protocol changes (consensus, P2P network)
- New opcodes or transaction types
- Changes to block validation rules
- RPC interface changes

### REP-XXX: Informational
Informational REPs describe design issues, or general guidelines, but do not propose a new feature. Informational REPs do not necessarily represent a consensus or recommendation.

### REP-XXX: Process
Process REPs describe changes to REP processes themselves, or propose changes to the Radiant development process.

## REP Format and Structure

Each REP must have the following structure:

```
REP: <REP number>
Title: <Short descriptive title>
Author: <List of authors' real names and/or email addresses>
Status: <Draft | Active | Final | Rejected | Withdrawn>
Type: <Standard | Informational | Process>
Created: <date created on, in ISO 8601 (yyyy-mm-dd) format>
License: <code license, usually MIT>
```

### Required Sections

#### Preamble
The preamble contains metadata about the REP and should follow the format above exactly.

#### Abstract
A short (~200 word) description of the technical issue being addressed.

#### Specification
The technical specification should describe the syntax and semantics of any new feature. The specification should be detailed enough to allow competing, interoperable implementations for any of the Radiant implementations.

#### Motivation
The motivation is critical for REPs that want to change the Radiant protocol. It should clearly explain why the existing protocol is inadequate to address the problem that the REP solves.

#### Rationale
The rationale fleshes out the specification by describing what motivated the design and why particular design decisions were made. It should describe alternate designs that were considered and related work.

#### Backwards Compatibility
All REPs must include a section on backwards compatibility, describing how the proposal affects existing implementations and what migration path is available.

#### Reference Implementation
The reference implementation must be completed before any REP is given status "Final", but it need not be completed before the REP is accepted. It is better to finish the specification and rationale first and then begin coding.

#### Security Considerations
All REPs must discuss security implications, including potential attack vectors and mitigation strategies.

## REP Lifecycle

### Draft
The initial status of a REP. The author may edit the REP at will. A Draft REP should not be implemented.

### Active
Active status indicates that the REP is being considered for adoption. Implementation may begin for Active REPs, but the specification may still change.

### Final
Final status indicates that the REP has been accepted and implemented. The specification is considered stable.

### Rejected
Rejected status indicates that the REP has been considered and rejected by the community.

### Withdrawn
Withdrawn status indicates that the author has withdrawn the REP.

## REP Numbering

REPs are numbered sequentially. Each new REP gets the next available number in its category.

Standard Track REPs: REP-1, REP-2, REP-3, ...
Informational REPs: REP-1001, REP-1002, REP-1003, ...
Process REPs: REP-2001, REP-2002, REP-2003, ...

## Submission Process

### 1. Pre-Draft Discussion
Before writing a REP, discuss the idea on the Radiant community channels (GitHub discussions, Discord, etc.) to gauge interest and gather feedback.

### 2. Draft Creation
Create the REP following the format above. The draft should be:
- Clear and concise
- Technically sound
- Well-reasoned

### 3. GitHub Pull Request
Submit the REP as a pull request to the **Radiant REP repository**: [https://github.com/Radiant-Core/REP](https://github.com/Radiant-Core/REP)

Use the REP template from the repository and follow the submission guidelines.

### 4. Community Review
The REP will be reviewed by the community. Expect feedback and be prepared to revise the proposal.

### 5. Implementation Consideration
For Standard Track REPs, a reference implementation should be developed during the review process.

### 6. Acceptance
REPs are accepted through rough consensus and running code. The maintainers will evaluate the REP based on:
- Technical merit
- Community support
- Implementation quality
- Backwards compatibility

## REP Repository Structure

The REP repository is located at: [https://github.com/Radiant-Core/REP](https://github.com/Radiant-Core/REP)

```
REP/
├── README.md              # This file
├── REP-0001.md          # Standard Track REPs
├── REP-0002.md
├── ...
├── REP-1001.md          # Informational REPs
├── REP-1002.md
├── ...
├── REP-2001.md          # Process REPs
├── REP-2002.md
├── ...
├── rep-template.md      # Template for new REPs
└── .github/             # GitHub templates and workflows
```

Visit the repository to view all existing REPs, submit new ones, and participate in discussions.

## Writing Guidelines

### Technical Requirements
- Be precise and unambiguous
- Include examples where helpful
- Consider edge cases
- Address security implications

### Style Guidelines
- Use clear, simple language
- Avoid unnecessary jargon
- Structure with clear headings
- Include relevant references

### Review Process
- Expect multiple rounds of review
- Be responsive to feedback
- Update the REP based on community input
- Maintain professional discourse

## Existing REPs

As of this document's creation, no REPs have been formally accepted. The following areas are candidates for initial REPs:

### Potential Standard Track REPs
- PSRT Protocol Enhancements
- Additional Script Opcodes
- Network Protocol Improvements
- Consensus Rule Modifications

### Potential Informational REPs
- Radiant Architecture Overview
- Security Best Practices
- Mining Guidelines
- Wallet Integration Standards

### Potential Process REPs
- REP Process Definition (this document)
- Release Process
- Security Disclosure Process
- Governance Guidelines

## References

This REP process is heavily inspired by:
- [Bitcoin Improvement Proposals (BIPs)](https://github.com/bitcoin/bips)
- [Ethereum Improvement Proposals (EIPs)](https://eips.ethereum.org/)
- [Python Enhancement Proposals (PEPs)](https://peps.python.org/)

## License

This document is licensed under the MIT License.

---

*Note: This REP process document itself should be formalized as REP-2001 (Process) in the [REP repository](https://github.com/Radiant-Core/REP) once the repository structure is established.*
