#include "tooltypepref.h"

#include <proto/exec.h>
#include <proto/icon.h>
#include <workbench/workbench.h>
#include <string.h>

#include <stdio.h>

//only use exec AllocVec() and FreeVec() for allocations, the Amiga way, in C99 !

struct DiskObject *AppDiskObject = NULL;

// this is the table of string
// managed to have same shape as DiskObject.do_ToolTypes
// settings are either "WORD" or "WORD=VALUE"
static char **CToolType = NULL;
static char *s_exename = NULL;

int ToolTypePrefs_Init(const char *exename)
{
    int count = 0;
    int i;
    char **src;

    // Clone exename with "PROGDIR:" prefix
    {
        int len = strlen(exename);
        s_exename = (char *)AllocVec(len + 9, MEMF_CLEAR); // "PROGDIR:" = 8 chars + \0
        if(!s_exename)
        {
            // Cleanup on failure
            for(i = 0; i < count; i++)
                FreeVec(CToolType[i]);
            FreeVec(CToolType);
            CToolType = NULL;
            FreeDiskObject(AppDiskObject);
            AppDiskObject = NULL;
            return 0;
        }
        strcpy(s_exename, "PROGDIR:");
        strcat(s_exename, exename);
    }

    AppDiskObject = GetDiskObjectNew(s_exename);
    if(!AppDiskObject) return 0;


    // Count number of tooltypes
    if(AppDiskObject->do_ToolTypes)
    {
        src = AppDiskObject->do_ToolTypes;
        while(src[count] != NULL)
            count++;
    }

    // Allocate table for count+1 entries (NULL terminated)
    CToolType = (char **)AllocVec((count + 1) * sizeof(char *), MEMF_CLEAR);
    if(!CToolType)
    {
        FreeDiskObject(AppDiskObject);
        AppDiskObject = NULL;
        return 0;
    }

    // Clone each string
    for(i = 0; i < count; i++)
    {

        int len = strlen(AppDiskObject->do_ToolTypes[i]);
        CToolType[i] = (char *)AllocVec(len + 1, MEMF_CLEAR);
        if(!CToolType[i])
        {
            int j;
            // Cleanup on failure
            for(j = 0; j < i; j++)
            {
               FreeVec(CToolType[j]);
            }
            FreeVec(CToolType);
            CToolType = NULL;
            FreeDiskObject(AppDiskObject);
            AppDiskObject = NULL;
            return 0;
        }
        strcpy(CToolType[i], AppDiskObject->do_ToolTypes[i]);
    }
    CToolType[count] = NULL;

    return 1;
}
const char *ToolTypePrefs_Get(const char *prefid)
{
    int i;
    int prefid_len;
    static char empty_string = '\0';

    if(!AppDiskObject || !CToolType) return NULL;

    prefid_len = strlen(prefid);

    for(i = 0; CToolType[i] != NULL; i++)
    {
        // Check if it starts with prefid
        if(strncmp(CToolType[i], prefid, prefid_len) == 0)
        {
            // Case 1: "NAME=VALUE" or "NAME=" - return pointer after '='
            if(CToolType[i][prefid_len] == '=')
            {
                return CToolType[i] + prefid_len + 1;
            }
            // Case 2: Just "NAME" - return empty string
            else if(CToolType[i][prefid_len] == '\0')
            {
                return &empty_string;
            }
        }
    }

    // Case 3: Not found
    return NULL;
}


void ToolTypePrefs_Set(const char *prefid,const char *value)
{
    int i;
    int count = 0;
    int prefid_len;
    int found_index = -1;
    char *new_entry;
    int new_entry_len;
    char **new_table;

    if(!AppDiskObject || !CToolType) return;

    prefid_len = strlen(prefid);

    // Count entries and search for existing prefid
    for(i = 0; CToolType[i] != NULL; i++)
    {
        count++;
        if(found_index == -1 && strncmp(CToolType[i], prefid, prefid_len) == 0)
        {
            if(CToolType[i][prefid_len] == '=' || CToolType[i][prefid_len] == '\0')
            {
                found_index = i;
            }
        }
    }

    // Create new entry "NAME=VALUE"
    new_entry_len = prefid_len + 1 ; // NAME + = + VALUE + \0
    if(value) new_entry_len +=  strlen(value)+1;
    new_entry = (char *)AllocVec(new_entry_len, MEMF_CLEAR);
    if(!new_entry) return;

    strcpy(new_entry, prefid);
    if(value)
    {
        strcat(new_entry, "=");
        strcat(new_entry, value);
    }
    if(found_index != -1)
    {
        // Replace existing entry
        FreeVec(CToolType[found_index]);
        CToolType[found_index] = new_entry;
    }
    else
    {
        // Insert new entry - need to reallocate table
        new_table = (char **)AllocVec((count + 2) * sizeof(char *), MEMF_CLEAR);
        if(!new_table)
        {
            FreeVec(new_entry);
            return;
        }

        // Copy existing entries
        for(i = 0; i < count; i++)
        {
            new_table[i] = CToolType[i];
        }

        // Add new entry
        new_table[count] = new_entry;
        new_table[count + 1] = NULL;

        // Free old table (but not the strings, we reused them)
        FreeVec(CToolType);
        CToolType = new_table;
    }
}
void ToolTypePrefs_Remove(const char *prefid)
{
    int i;
    int count = 0;
    int prefid_len;
    int found_index = -1;
    char **new_table;
    int new_count;

    if(!AppDiskObject || !CToolType) return;

    prefid_len = strlen(prefid);

    // Count entries and search for existing prefid
    for(i = 0; CToolType[i] != NULL; i++)
    {
        count++;
        if(found_index == -1 && strncmp(CToolType[i], prefid, prefid_len) == 0)
        {
            if(CToolType[i][prefid_len] == '=' || CToolType[i][prefid_len] == '\0')
            {
                found_index = i;
            }
        }
    }

    // If not found, nothing to do
    if(found_index == -1) return;

    // Free the found entry
    FreeVec(CToolType[found_index]);

    // If this was the only entry, just set to NULL
    if(count == 1)
    {
        FreeVec(CToolType);
        CToolType = (char **)AllocVec(sizeof(char *), MEMF_CLEAR);
        if(CToolType) CToolType[0] = NULL;
        return;
    }

    // Allocate new table with one less entry
    new_count = count - 1;
    new_table = (char **)AllocVec((new_count + 1) * sizeof(char *), MEMF_CLEAR);
    if(!new_table) return;

    // Copy entries, skipping the removed one
    new_count = 0;
    for(i = 0; i < count; i++)
    {
        if(i != found_index)
        {
            new_table[new_count++] = CToolType[i];
        }
    }
    new_table[new_count] = NULL;

    // Free old table
    FreeVec(CToolType);
    CToolType = new_table;
}

void ToolTypePrefs_Save()
{
    char **previousValues;
    if(!AppDiskObject || !CToolType) return;

    // that's how we save tooltypes prefs in icons..
    previousValues = AppDiskObject->do_ToolTypes;
        AppDiskObject->do_ToolTypes = CToolType;
        AppDiskObject->do_Type = WBTOOL;
        AppDiskObject->do_StackSize = 32768;
        //  dobj->do_DefaultTool = olddeftool;
         PutDiskObject(s_exename, AppDiskObject);
    AppDiskObject->do_ToolTypes = previousValues; // so it is freed by FreeDiskObject()

}
void ToolTypePrefs_Close()
{
    int i;

    if(CToolType)
    {
        // Free all strings
        for(i = 0; CToolType[i] != NULL; i++)
        {
            FreeVec(CToolType[i]);
        }
        // Free the table itself
        FreeVec(CToolType);
        CToolType = NULL;
    }

    if(AppDiskObject)
    {
        FreeDiskObject(AppDiskObject);
        AppDiskObject = NULL;
    }

    if(s_exename)
    {
        FreeVec(s_exename);
        s_exename = NULL;
    }
}
