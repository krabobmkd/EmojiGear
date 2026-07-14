/*
 * fs3enetworkhelper.c - small shared helpers used across friendsh3ep.c,
 * fs3erequests.c, and fs3eaccounts.c. See fs3enetworkhelper.h for the
 * public entry points and friendsh3ep.h for struct App.
 */

#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>

#include <proto/exec.h>

#include "compilers.h"
#include "bdbprintf.h"

#include "friendsh3ep.h"
#include "fs3enetworkhelper.h"

/* - - - - - - - - - - - - - - - - NETWORK HELPERS - - - - - - - - - - - - - */

/* Small AllocVec'd strdup -- NULL/empty in yields NULL out. Used all over
 * (friendsh3ep.c, fs3erequests.c, fs3eaccounts.c) for copying strings out
 * of network replies/tags into AllocVec'd, app-owned storage. */
char *NetStrDup(const char *s)
{
    ULONG len;
    char *copy;
    if (!s || !s[0]) return NULL;
    len  = (ULONG)strlen(s) + 1;
    copy = (char *)AllocVec(len, MEMF_ANY);
    if (copy) CopyMem(s, copy, len);
    return copy;
}

/* Free login interim state (called on login error or completion). Used by
 * friendsh3ep.c's main() (Cancel button, exitclose), fs3erequests.c's
 * FS3EApp_HandleNetReply, and fs3eaccounts.c's FS3EApp_SwitchAccount. */
void FS3EApp_FreeLoginState(void)
{
    if (app->loginApiBaseUrl)   { FreeVec(app->loginApiBaseUrl);   app->loginApiBaseUrl   = NULL; }
    if (app->loginClientId)     { FreeVec(app->loginClientId);     app->loginClientId     = NULL; }
    if (app->loginClientSecret) { FreeVec(app->loginClientSecret); app->loginClientSecret = NULL; }
    app->loginPhase = FS3ELOGIN_IDLE;
}

/* Ensure server URL has a scheme and no trailing slash.
 * "mastoot.fr" → "https://mastoot.fr"
 * "https://mastoot.fr/" → "https://mastoot.fr"
 * Not static: fs3erequests.c's FS3EApp_LoginStart calls this too -- see the
 * extern declaration there. */
const char *NormalizeServerUrl(const char *server, char *buf, ULONG bufSize)
{
    ULONG len;
    if (!server || !server[0]) return server;
    if (strncmp(server, "http://", 7) == 0 || strncmp(server, "https://", 8) == 0)
        strncpy(buf, server, bufSize - 1);
    else
        snprintf(buf, bufSize, "https://%s", server);
    buf[bufSize - 1] = '\0';
    len = (ULONG)strlen(buf);
    while (len > 0 && buf[len - 1] == '/') buf[--len] = '\0';
    return buf;
}
