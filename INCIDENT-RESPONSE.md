# Incident Response Plan

**Project:** cpp-CAN  
**Standard:** IEC 62443-4-2 CR 6.2.1  
**Owner:** Matt Jones <matt@jellybaby.com>

## 1. Scope

This plan covers security incidents affecting the cpp-CAN library and its downstream
integrations (automotive, industrial, and embedded deployments).

## 2. Incident Categories

| Severity | Description | Response SLA |
|----------|-------------|--------------|
| Critical | Memory corruption, RCE, safety-path data corruption | 24 hours |
| High | Thread safety violation, privilege escalation, DoS in safety path | 72 hours |
| Medium | Local information disclosure, non-critical DoS | 14 days |
| Low | Configuration weaknesses, documentation gaps | 90 days |

## 3. Detection

Incidents may be detected via:
- Private vulnerability reports to `matt@jellybaby.com`
- GitHub Dependabot / OSV alerts (`cpfusa vuln` in CI)
- User-reported operational anomalies
- Internal code review or `cpfusa cyber` findings

## 4. Response Procedure

### 4.1 Triage (within SLA above)
1. Acknowledge receipt to the reporter.
2. Reproduce in an isolated environment.
3. Assign severity using the table above.

### 4.2 Containment
1. Assess whether a workaround can be documented immediately.
2. For Critical/High: create a private branch; do not push to public `main` until a patch is ready.

### 4.3 Remediation
1. Develop and test a fix.
2. Update `vuln.json` via `cpfusa vuln`.
3. Open a private PR, obtain review, merge.
4. Tag a patch release `vMAJOR.MINOR.PATCH+1`.
5. Publish a GitHub Security Advisory crediting the reporter.

### 4.4 Post-mortem
For Critical incidents: produce a written post-mortem within 7 days of closure
covering root cause, timeline, fix, and preventive measures.

## 5. Contact

Security reports: **matt@jellybaby.com**  
Subject line: `[cpp-CAN SECURITY] <short description>`
