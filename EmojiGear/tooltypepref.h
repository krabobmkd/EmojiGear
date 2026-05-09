#ifndef BWTTPREFS_H_
#define BWTTPREFS_H_

/**
 * ToolType Preferences API
 *
 * Manages application preferences using Amiga icon.library tooltypes.
 * Tooltypes are stored in .info files and can be either standalone
 * keywords ("NAME") or key=value pairs ("NAME=VALUE").
 */

/**
 * Initialize the tooltype preferences system.
 *
 * @param exename Base name of the executable (without path).
 *                Will be prefixed with "PROGDIR:" to locate the .info file.
 * @return 1 on success, 0 on failure (e.g., no .info file or allocation error).
 */
int ToolTypePrefs_Init(const char *exename);

/**
 * Get the value of a tooltype preference.
 *
 * @param prefid The preference name to look up.
 * @return Pointer to the value string if "NAME=VALUE" format found,
 *         pointer to empty string if standalone "NAME" found,
 *         NULL if not found.
 */
const char *ToolTypePrefs_Get(const char *prefid);

/**
 * Set or update a tooltype preference.
 *
 * @param prefid The preference name.
 * @param value The value to set. Will be stored as "NAME=VALUE".
 *              If prefid already exists, it will be replaced.
 *              If not found, it will be added to the tooltype array.
 */
void ToolTypePrefs_Set(const char *prefid, const char *value);

/**
 * Remove a tooltype preference.
 *
 * @param prefid The preference name to remove.
 *               Removes entries matching "NAME" or "NAME=*".
 */
void ToolTypePrefs_Remove(const char *prefid);

/**
 * Save all tooltype preferences back to the .info file.
 *
 * Writes the modified tooltypes to disk using PutDiskObject().
 */
void ToolTypePrefs_Save();

/**
 * Clean up and free all allocated resources.
 *
 * Must be called to properly release memory allocated by ToolTypePrefs_Init().
 */
void ToolTypePrefs_Close();

#endif

