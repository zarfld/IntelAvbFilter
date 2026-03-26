# Archived Backup/Temporary Files

**Archived Date**: 2025-12-29  
**Reason**: Obsolete, experimental, or temporary files  
**Related Issue**: [#293](https://github.com/zarfld/IntelAvbFilter/issues/293) (REQ-NF-CLEANUP-002)

## Files in This Archive

| File | Reason for Archival | Status |
|------|---------------------|--------|
| temp_filter_7624c57.c | Temporary backup file | Obsolete (replaced by src/filter.c) |
| intel_tsn_enhanced_implementations.c | Experimental TSN implementation | Experimental (not integrated) |
| tsn_hardware_activation_investigation.c | Investigation/diagnostic code | Investigation (not production code) |

## Recovery Instructions

If you need to view or restore these files:

1. **View file content**:
   ```bash
   git show master:archive/backup/temp_filter_7624c57.c
   ```

2. **Restore to working directory** (temporary):
   ```bash
   git show master:archive/backup/temp_filter_7624c57.c > temp_filter_7624c57.c
   ```

3. **View full history**:
   ```bash
   git log --follow archive/backup/temp_filter_7624c57.c
   ```
