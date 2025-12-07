/**
 * @file file_logger.h
 * @brief Simple file-based logging for kernel driver debugging
 * 
 * This provides a fallback logging mechanism when DebugView doesn't work.
 * Logs are written to C:\IntelAvb_Debug.log
 */

#ifndef FILE_LOGGER_H
#define FILE_LOGGER_H

#include <ntddk.h>

/**
 * @brief Write a log message to file
 * @param format Printf-style format string
 * @param ... Variable arguments
 */
VOID FileLog(const char* format, ...);

/**
 * @brief Initialize file logging system
 * @return TRUE if successful
 */
BOOLEAN FileLogInit(VOID);

/**
 * @brief Cleanup file logging system
 */
VOID FileLogCleanup(VOID);

#endif // FILE_LOGGER_H
