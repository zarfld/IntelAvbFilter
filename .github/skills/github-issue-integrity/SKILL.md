---
name: github-issue-integrity
description: Use whenever reading or mutating existing GitHub issue bodies, especially when adding backward Verified-by links, repairing traceability, recovering corrupted issue content, or considering cleanup/reformatting of an existing issue. Enforces append-only safety for backward links and exact-history recovery for corruption.
user-invocable: true
---

# GitHub Issue Integrity

Existing issue bodies are lifecycle records. Preserve their content unless the user explicitly requests a specific edit.

## Overriding rule: adding backward `Verified by:` links

When the requested task is to add a backward verification link to an existing GitHub issue:

1. Fetch/read the current issue body immediately before the edit.
2. Locate the existing `## Traceability` section.
3. Add **only** the requested line, normally:

   `- Verified by: #XXX (TEST-...: description)`

4. Preserve all existing content, including:
   - `Traces to:` links
   - `Related to:` links
   - `Depends on:` links
   - all other relationships
   - description text
   - technical details and IOCTL information
   - error handling
   - performance requirements
   - acceptance criteria
   - headings, prose, and unrelated formatting
5. Before writing, compare the proposed body to the fetched body and verify that the only semantic change is the requested backward-link addition.
6. If unrelated content appears wrong, stale, malformed, or inconsistent, **do not repair it as part of this operation**. Report it and obtain explicit user direction for any additional change.

### Forbidden during backward-link updates

Do not:

- change an existing `Traces to:` relationship, even if it appears incorrect;
- rewrite or improve prose;
- change technical specifications, performance values, IOCTL details, error handling, or acceptance criteria;
- delete any content;
- reorganize or normalize sections;
- perform opportunistic cleanup/refactoring of the issue body.

This restriction overrides Boy Scout cleanup, formatting normalization, and other improvement rules for this operation.

## General existing-issue mutation rule

For other explicitly requested issue edits:

- fetch the current body first;
- make only the requested changes;
- preserve unrelated content and relationships;
- do not infer missing facts or silently repair neighboring material;
- re-read/compare the final body before submitting when the edit is non-trivial.

## Corruption recovery workflow

When issue content is believed to be corrupted (wrong traceability, missing sections, overwritten prose, or similar), recovery is **manual and stepwise**. Do not combine steps or improvise a reconstruction.

1. Ask the user to identify/provide the last edit that appears to have caused the corruption.
2. Back up the current corrupted issue body to a separate Markdown file.
3. Create a Markdown recovery record from the user-provided recovery information.
4. Restore the issue from the last known-good content in GitHub edit history.
5. Verify the restored issue matches the recovery information.
6. Inspect the corrupted backup to determine whether any genuinely new/lost information should become a separate new issue or separately authorized change.

**Restore exactly from known-good GitHub history. Never "fix" corrupted content manually from assumptions.**

## Traceability syntax

If the requested mutation changes traceability relationships, also use `lifecycle-traceability`. Do not invent relationship syntax.

## Completion check

Before reporting success for an issue mutation, verify:

- the intended line/change exists;
- unrelated content is preserved;
- no required relationship was accidentally removed/rewritten;
- the issue number and referenced issue numbers are correct;
- the result still conforms to repository traceability syntax.