# Archived Filter Files

**Archived Date**: 2025-12-29  
**Reason**: Obsolete/replaced by active implementation  
**Related Issue**: [#293](https://github.com/zarfld/IntelAvbFilter/issues/293) (REQ-NF-CLEANUP-002)

## Files in This Archive

| File | Reason for Archival | Replacement |
|------|---------------------|-------------|
| filter_august.c | Replaced by current implementation | src/filter.c |
| filter_august.txt | Documentation for obsolete filter | N/A |

## Recovery Instructions

If you need to view or restore these files:

1. **View file content**:
   ```bash
   git show master:archive/filter_old/filter_august.c
   ```

2. **Restore to working directory** (temporary):
   ```bash
   git show master:archive/filter_old/filter_august.c > filter_august.c
   ```

3. **View full history**:
   ```bash
   git log --follow archive/filter_old/filter_august.c
   ```
