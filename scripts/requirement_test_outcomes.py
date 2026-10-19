#!/usr/bin/env python3
"""Requirement test-outcome classification from traceability-summary-*.md.

Port of the inline step "Enforce test execution coverage" in
.github/workflows/ci-standards-compliance.yml, extended for the verdict contract
(docs/testing/test-verdict-contract.md) so a requirement carrying SKIP_NOT_IMPLEMENTED or
INCONCLUSIVE is reported as INCOMPLETE and never as verified.

Row format written by tools/test/Convert-Logs-To-JUnit.ps1:
  | [#N](url) | suite | test-issue | PASS | FAIL | SKIP [| NOT_IMPL | INCONCL] |
PASS/FAIL/SKIP are the JUnit tallies; NOT_IMPL and INCONCL are exact-verdict subsets
(already inside SKIP / FAIL-or-SKIP).  Old 3-count rows (no trailing columns) are still read.

Classes (per requirement, accumulated over all rows / configurations):
  failing     fail > 0
  incomplete  no failure, but not_impl > 0 or inconcl > 0   (required work missing / not verified)
  verified    pass > 0 and nothing above    (pure pass, or pass + genuine N/A skips)
  skip_only   no pass, no failure, nothing incomplete       (no evidence)

Usage:
  python scripts/requirement_test_outcomes.py <dir> [--fail-on-incomplete]
Exit 0, or 1 when --fail-on-incomplete and any requirement is failing or incomplete.
"""
import argparse
import re
import sys
from pathlib import Path

ROW_RE = re.compile(
    r'^\| \[#(\d+)\]\([^)]+\) \| ([^|]+) \| ([^|]*) \| (\d+) \| (\d+) \| (\d+) \|(?: (\d+) \| (\d+) \|)?')


def parse_rows(text):
    """Yield (req, pass, fail, skip, not_impl, inconcl) from one summary file's text."""
    in_table = False
    for line in text.splitlines():
        if '| Requirement |' in line:
            in_table = True
            continue
        if in_table:
            m = ROW_RE.match(line)
            if m:
                g = m.groups()
                yield (g[0], int(g[3]), int(g[4]), int(g[5]),
                       int(g[6] or 0), int(g[7] or 0))
            elif line.startswith('###') or not line.strip():
                in_table = False


def accumulate(texts):
    tested = {}
    for text in texts:
        for req, p, f, s, ni, ic in parse_rows(text):
            t = tested.setdefault(req, dict(passed=0, fail=0, skip=0, not_impl=0, inconcl=0))
            t['passed'] += p
            t['fail'] += f
            t['skip'] += s
            t['not_impl'] += ni
            t['inconcl'] += ic
    return tested


def classify(t):
    if t['fail'] > 0:
        return 'failing'
    if t['not_impl'] > 0 or t['inconcl'] > 0:
        return 'incomplete'
    if t['passed'] > 0:
        return 'verified'
    return 'skip_only'


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('directory')
    ap.add_argument('--fail-on-incomplete', action='store_true',
                    help='exit 1 when any requirement is failing or incomplete')
    args = ap.parse_args(argv)

    files = sorted(Path(args.directory).rglob('traceability-summary-*.md'))
    if not files:
        print('No test execution traceability artifacts found.')
        return 0
    tested = accumulate(f.read_text(encoding='utf-8') for f in files)
    classes = {k: sorted((r for r, t in tested.items() if classify(t) == k), key=int)
               for k in ('verified', 'failing', 'incomplete', 'skip_only')}

    print(f'Requirements with test outcomes: {len(tested)}')
    print(f"  Verified: {len(classes['verified'])}  |  Failing: {len(classes['failing'])}"
          f"  |  Incomplete: {len(classes['incomplete'])}  |  Skip-only (no evidence): {len(classes['skip_only'])}")
    print('  (Verified = PASS present, no failure, nothing SKIP_NOT_IMPLEMENTED/INCONCLUSIVE;'
          ' Incomplete = SKIP_NOT_IMPLEMENTED or INCONCLUSIVE present)')
    if classes['failing']:
        print('  Failing requirements: ' + ','.join('#' + r for r in classes['failing']))
    if classes['incomplete']:
        print('  Incomplete requirements: ' + ','.join('#' + r for r in classes['incomplete']))

    if args.fail_on_incomplete and (classes['failing'] or classes['incomplete']):
        print('FAIL: mandatory verification has failing or incomplete requirements', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
