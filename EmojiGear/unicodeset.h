#ifndef UNICODESET_H
#define UNICODESET_H

#include <exec/types.h>

/* Return a short human-readable name for the Unicode block that
 * 'codepoint' belongs to (e.g. "Hangul Syllables (Korean)",
 * "Box Drawing", "Emoticons (Emoji)"), or "Unknown set" if the
 * codepoint falls in an unassigned/unrecognised range.
 *
 * Intended use: when a character cannot be rendered, this tells the
 * user what kind of glyph is missing so they can pick a font that
 * covers it. */
const char *find_unicode_set(ULONG codepoint);

#endif /* UNICODESET_H */
