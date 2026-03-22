#!/usr/bin/env python3
"""
Remove all #if DBG blocks containing ZwCreateKey/ZwSetValueKey from intel_i226_impl.c.

These calls require IRQL = PASSIVE_LEVEL but are invoked from:
  - AvbTimerDpcRoutine: DPC = DISPATCH_LEVEL  -> 0xC4/0x12001F BSOD
  - i226_set_target_time: IOCTL path, but still wrong pattern

The timer initialization code (KeInitializeTimer / KeInitializeDpc / KeSetTimerEx)
trapped inside the large DBG block is extracted and kept unconditional.

Bug reference: BSOD 030526-12125-01.dmp, Arg1=0x12001f, IntelAvbFilter+0x17568
"""

import re
import sys

TARGET = r"C:\Users\dzarf\source\repos\IntelAvbFilter\devices\intel_i226_impl.c"

def parse_blocks(lines):
    """
    Return list of (start_idx, end_idx) for every #if DBG block
    that contains at least one ZwCreateKey or ZwSetValueKey call.
    start_idx = line index of '#if DBG', end_idx = line index of matching '#endif'.
    Handles nested #if/#endif.
    """
    results = []
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped in ('#if DBG', '#if DBG\n') or stripped.startswith('#if DBG'):
            # Found a candidate; scan to matching #endif
            depth = 1
            j = i + 1
            while j < len(lines) and depth > 0:
                s = lines[j].strip()
                if s.startswith('#if'):
                    depth += 1
                elif s.startswith('#endif'):
                    depth -= 1
                j += 1
            end = j - 1  # index of the '#endif' line
            block_text = ''.join(lines[i:end+1])
            if 'ZwCreateKey' in block_text or 'ZwSetValueKey' in block_text:
                results.append((i, end))
            i = end + 1
        else:
            i += 1
    return results


def extract_timer_init(block_lines):
    """
    From the large #if DBG block that contains timer init, extract the
    timer initialization logic as clean unconditional code.
    Returns a list of lines (already with trailing newlines) or empty list.
    """
    text = ''.join(block_lines)
    if 'KeInitializeTimer' not in text:
        return []

    # We want to extract the context-pointer derivation + conditional timer init
    # Pattern to extract:
    extracted = []
    extracted.append('    /* Bug #1d: REMOVED registry writes from i226_set_target_time */\n')
    extracted.append('    /* Timer initialization (moved out of #if DBG block) */\n')
    extracted.append('    {\n')
    extracted.append('        struct intel_private *priv = (struct intel_private *)dev->private_data;\n')
    extracted.append('        PAVB_DEVICE_CONTEXT avb_context = priv ? (PAVB_DEVICE_CONTEXT)priv->platform_data : NULL;\n')
    extracted.append('\n')
    extracted.append('        if (avb_context) {\n')
    extracted.append('            DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: Context valid (ctx=%p, via priv=%p)\\n", avb_context, priv);\n')
    extracted.append('        } else {\n')
    extracted.append('            DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: Context is NULL! (priv=%p)\\n", priv);\n')
    extracted.append('        }\n')
    extracted.append('\n')
    extracted.append('        if (avb_context) {\n')
    extracted.append('            DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: Flags before init check (ctx=%p, target_active=%d, tx_active=%d)\\n",\n')
    extracted.append('                   avb_context,\n')
    extracted.append('                   avb_context->target_time_poll_active ? 1 : 0,\n')
    extracted.append('                   avb_context->tx_poll_active ? 1 : 0);\n')
    extracted.append('        }\n')
    extracted.append('\n')
    extracted.append('        if (avb_context && !avb_context->target_time_poll_active) {\n')
    extracted.append('            DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: Entering timer init block (flag was FALSE)\\n");\n')
    extracted.append('            KeInitializeTimer(&avb_context->target_time_timer);\n')
    extracted.append('            KeInitializeDpc(&avb_context->target_time_dpc, AvbTimerDpcRoutine, avb_context);\n')
    extracted.append('\n')
    extracted.append('            LARGE_INTEGER dueTime;\n')
    extracted.append('            dueTime.QuadPart = -100000LL;  /* 10ms relative */\n')
    extracted.append('            KeSetTimerEx(&avb_context->target_time_timer, dueTime, 10, &avb_context->target_time_dpc);\n')
    extracted.append('\n')
    extracted.append('            DEBUGP(DL_TRACE, "!!! TARGET_TIMER_INIT: Activating flag (ctx=%p, before=%d)\\n",\n')
    extracted.append('                   avb_context, avb_context->target_time_poll_active ? 1 : 0);\n')
    extracted.append('            avb_context->target_time_poll_active = TRUE;\n')
    extracted.append('            DEBUGP(DL_TRACE, "!!! TARGET_TIMER_INIT: Flag activated (ctx=%p, after=%d)\\n",\n')
    extracted.append('                   avb_context, avb_context->target_time_poll_active ? 1 : 0);\n')
    extracted.append('            avb_context->target_time_poll_interval_ms = 10;\n')
    extracted.append('            avb_context->target_time_dpc_call_count = 0;\n')
    extracted.append('            DEBUGP(DL_TRACE, "!!! TIMER INITIALIZED: 10ms periodic DPC started (context=%p)\\n", avb_context);\n')
    extracted.append('        } else if (avb_context && avb_context->target_time_poll_active) {\n')
    extracted.append('            DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: SKIPPING timer init - already active (ctx=%p)\\n", avb_context);\n')
    extracted.append('        } else {\n')
    extracted.append('            DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: SKIPPING timer init - context is NULL!\\n");\n')
    extracted.append('        }\n')
    extracted.append('    }\n')
    return extracted


def main():
    with open(TARGET, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    print(f"Read {len(lines)} lines from {TARGET}")

    blocks = parse_blocks(lines)
    print(f"Found {len(blocks)} #if DBG blocks containing Zw* registry calls:")
    for s, e in blocks:
        block_text = ''.join(lines[s:e+1])
        has_timer = 'KeInitializeTimer' in block_text
        print(f"  Lines {s+1}-{e+1} {'[CONTAINS TIMER INIT]' if has_timer else ''}")

    # Process blocks in REVERSE order so indices remain valid
    for start, end in reversed(blocks):
        block_lines = lines[start:end+1]
        replacement = extract_timer_init(block_lines)
        lines[start:end+1] = replacement
        print(f"  Replaced lines {start+1}-{end+1} with {len(replacement)} lines"
              f" {'(timer init extracted)' if replacement else '(removed entirely)'}")

    # Final verification: no ZwCreateKey/ZwSetValueKey should remain
    remaining = []
    for i, line in enumerate(lines):
        if ('ZwCreateKey' in line or 'ZwSetValueKey' in line) and not line.strip().startswith('//'):
            remaining.append((i+1, line.rstrip()))

    if remaining:
        print(f"\n!!! WARNING: {len(remaining)} active Zw* calls still remain:")
        for lineno, txt in remaining:
            print(f"  Line {lineno}: {txt}")
        sys.exit(1)
    else:
        print("\nVerification PASSED: zero active Zw* registry calls remain.")

    # Write backup
    backup = TARGET.replace('.c', '_backup_before_fix.c')
    with open(backup, 'w', encoding='utf-8') as f:
        with open(TARGET, 'r', encoding='utf-8') as orig:
            f.write(orig.read())
    print(f"Backup written to: {backup}")

    with open(TARGET, 'w', encoding='utf-8') as f:
        f.writelines(lines)
    print(f"Fixed file written: {TARGET} ({len(lines)} lines)")


if __name__ == '__main__':
    main()
