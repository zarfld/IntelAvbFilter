# Archived Integration Files

**Archived Date**: 2025-12-29  
**Reason**: Obsolete/replaced by active implementation  
**Related Issue**: [#293](https://github.com/zarfld/IntelAvbFilter/issues/293) (REQ-NF-CLEANUP-002)

## Files in This Archive

| File | Reason for Archival | Replacement |
|------|---------------------|-------------|
| avb_integration.c | Replaced by refactored implementation | avb_integration_fixed.c |
| avb_context_management.c | Functionality merged | avb_integration_fixed.c |
| avb_integration_fixed_backup.c | Pre-refactor backup | avb_integration_fixed.c |
| avb_integration_fixed_complete.c | Intermediate backup | avb_integration_fixed.c |
| avb_integration_fixed_new.c | Post-refactor backup | avb_integration_fixed.c |
| avb_integration_hardware_only.c | Experimental approach abandoned | avb_integration_fixed.c |

## Recovery Instructions

If you need to view or restore these files:

1. **View file content**:
   ```bash
   git show master:archive/avb_integration_old/avb_integration.c
   ```

2. **Restore to working directory** (temporary):
   ```bash
   git show master:archive/avb_integration_old/avb_integration.c > avb_integration.c
   ```

3. **View full history**:
   ```bash
   git log --follow archive/avb_integration_old/avb_integration.c
   ```
