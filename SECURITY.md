# Security Policy

IntelAvbFilter is a pre-1.0 Windows kernel-mode NDIS Lightweight Filter project. Security reports are welcome and should be handled privately until they can be assessed and, where appropriate, remediated and disclosed in a coordinated way.

## Supported versions

IntelAvbFilter does not currently provide a long-term-support (LTS) branch or a fixed security-maintenance period.

| Version / branch | Security support |
| --- | --- |
| `master` | Best-effort security fixes |
| Latest tagged pre-release | Best-effort when practical; a fix may require upgrading to a newer commit or release |
| Older tags and commits | Not supported unless explicitly stated otherwise |

Because the project is under active development, reporters and downstream users should reproduce suspected vulnerabilities against the latest applicable `master` revision whenever practical.

## Reporting a vulnerability

**Do not report suspected security vulnerabilities in a public GitHub issue, pull request, discussion, or other public channel before coordinated disclosure.**

Use GitHub's private security-advisory reporting channel:

**https://github.com/zarfld/IntelAvbFilter/security/advisories/new**

A useful report should include, where applicable:

- affected release, tag, branch, and/or commit SHA;
- Windows version/build and relevant WDK/driver context;
- Intel Ethernet controller model and hardware/firmware details relevant to reproduction;
- whether the issue requires local access, administrative privilege, or network access;
- clear reproduction steps or a minimal proof of concept;
- observed and expected behavior;
- security impact, including confidentiality, integrity, availability, privilege-boundary, or kernel-stability implications;
- crash dumps, logs, traces, or Driver Verifier output, with secrets and unrelated personal data removed;
- any known workaround or suggested mitigation;
- whether the issue has already been disclosed to another vendor, coordinator, or CVE authority, and any applicable embargo constraints.

Please submit one vulnerability per report unless multiple findings are inseparable parts of the same root cause.

## What should be reported privately

Examples of issues that should use the private security channel include suspected:

- privilege escalation or unauthorized privileged driver operations;
- arbitrary or unintended kernel-memory access or corruption;
- unsafe IOCTL handling or access-control bypasses;
- exploitable crashes, denial of service, use-after-free, buffer overrun, or other memory-safety defects;
- unintended MMIO/register access that crosses an expected privilege or device boundary;
- vulnerabilities triggered by untrusted network traffic or malformed AVB/PTP/TSN input;
- information disclosure of sensitive kernel, device, or process data;
- security-relevant installation, signing, update, or trust-chain weaknesses attributable to this project.

Ordinary functional defects without a plausible security impact—such as unsupported hardware, timestamp-accuracy problems, performance regressions, or non-security test failures—should normally use the public issue tracker.

When uncertain, report privately first.

## Coordinated vulnerability disclosure process

IntelAvbFilter is maintained as an open-source project on a best-effort basis. The project does **not** promise a fixed response, remediation, or disclosure SLA.

For a credible report, the intended process is:

1. **Triage** — review the report, identify the affected component/version, and determine whether the issue is security-relevant.
2. **Reproduction and assessment** — reproduce the issue where feasible and assess exploitability, impact, affected configurations, and available mitigations.
3. **Coordination** — keep technical details private while a fix or mitigation is developed when coordinated disclosure is appropriate.
4. **Remediation and verification** — develop and verify a fix using the repository's normal engineering and test practices, including hardware-in-the-loop testing where relevant.
5. **Disclosure** — publish an advisory and remediation information when a fix/mitigation is available or at another mutually coordinated point. A CVE may be requested or referenced when appropriate.

If a report cannot be reproduced, is outside the project's scope, or is assessed as non-security-relevant, that conclusion may be communicated through the private report and the corresponding functional issue may later be tracked publicly without sensitive details.

Reporters are asked to allow a reasonable coordination period before public disclosure. If there is an urgent risk to users or active exploitation is suspected, state that prominently in the private report so it can be prioritized accordingly.

## Downstream and commercial use

This policy is intended to make the project's security posture easier to assess for downstream users, including organizations integrating IntelAvbFilter into commercial products.

Published security policies, advisories, test evidence, release metadata, and other project artifacts may be used as inputs to a downstream integrator's own software-supply-chain and cybersecurity due diligence, including assessments performed in connection with the EU Cyber Resilience Act (CRA).

This repository does **not** claim that IntelAvbFilter is CRA-certified, CRA-conformant, CE-marked, or otherwise approved for a particular regulatory use. Downstream manufacturers and integrators remain responsible for determining and satisfying the legal, security, conformity-assessment, vulnerability-handling, support-period, and reporting obligations applicable to their own products and use cases.

## No additional warranty

IntelAvbFilter is distributed under the GNU General Public License v3.0 (`GPL-3.0`). The warranty and liability terms in the repository's `LICENSE` continue to apply. This security policy describes reporting and coordination practices and does not create an additional warranty, service-level commitment, support contract, or guarantee of fitness for a particular purpose.
