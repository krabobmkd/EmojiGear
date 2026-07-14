#ifndef FS3ENETWORKHELPER_H
#define FS3ENETWORKHELPER_H

/*
 * fs3enetworkhelper.c - small shared helpers used across friendsh3ep.c,
 * fs3erequests.c, and fs3eaccounts.c. See friendsh3ep.h for struct App.
 */

/* AllocVec'd strdup -- NULL/empty in yields NULL out. */
char *NetStrDup(const char *s);

/* Free login interim OAuth state (loginApiBaseUrl/loginClientId/
 * loginClientSecret) and reset loginPhase to FS3ELOGIN_IDLE. */
void FS3EApp_FreeLoginState(void);

/* Ensure server URL has a scheme and no trailing slash, written into buf.
 * "mastoot.fr" -> "https://mastoot.fr". */
const char *NormalizeServerUrl(const char *server, char *buf, ULONG bufSize);

#endif /* FS3ENETWORKHELPER_H */
