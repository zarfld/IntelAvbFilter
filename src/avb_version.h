#pragma once

/*
 * Driver version definitions
 * 
 * Version format: Major.Minor.Build.Revision
 * 
 * - Major: API version, breaking changes (manually set)
 * - Minor: API version, compatible additions (manually set)
 * - Build: Incrementing build counter (set by CI/CD or build script)
 * - Revision: Source control commit count or patch number (set by CI/CD or build script)
 * 
 * Build and Revision should be set by build infrastructure via:
 *   - Environment variables (BUILD_NUMBER, GIT_COMMIT_COUNT)
 *   - Command-line defines: /DAVB_VERSION_BUILD=12345
 *   - Build script that reads/increments counter file
 * 
 * If not set by infrastructure, defaults to 0.
 */

/* API version (manually set when API changes) */
#define AVB_VERSION_MAJOR 1
#define AVB_VERSION_MINOR 0

/* Build number (should be set by CI/CD or build script) */
#ifndef AVB_VERSION_BUILD
  #define AVB_VERSION_BUILD 0  /* Default: Not set by build infrastructure */
#endif

/* Revision/commit count (should be set by CI/CD or build script) */
#ifndef AVB_VERSION_REVISION
  #define AVB_VERSION_REVISION 0  /* Default: Not set by build infrastructure */
#endif

/*
 * Build infrastructure integration examples:
 * 
 * 1. From environment variables (PowerShell build script):
 *    $env:AVB_VERSION_BUILD = <counter>
 *    $env:AVB_VERSION_REVISION = <git commit count>
 *    MSBuild with: /p:DefineConstants="AVB_VERSION_BUILD=$($env:AVB_VERSION_BUILD)"
 * 
 * 2. From Git (if available):
 *    Build = $(git rev-list --count HEAD)
 *    Revision = First 4 hex digits of $(git rev-parse --short HEAD)
 * 
 * 3. From CI/CD (GitHub Actions, Azure Pipelines, etc.):
 *    Build = ${{ github.run_number }}
 *    Revision = ${{ github.run_attempt }}
 */
