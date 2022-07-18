#include <ctype.h>
#include <tchar.h>

#include "utils.h"

PTCHAR _tcsistr(PTCHAR haystack, const PTCHAR needle)
{
    do
    {
        PTCHAR h = haystack;
        PTCHAR n = needle;
        while (tolower((TBYTE)*h) == tolower((TBYTE)*n) && *n)
        {
            h++;
            n++;
        }
        if (*n == 0)
        {
            return (PTCHAR)haystack;
        }
    } while (*haystack++);
    return NULL;
}

LPTSTR _guid_to_str(const GUID *guid)
{
    LPTSTR result = malloc(39 * sizeof(TCHAR));
    PTCHAR cur = result;
    cur += _stprintf(cur, TEXT("{%.8lX-%.4hX-%.4hX-"), guid->Data1, guid->Data2, guid->Data3);
    for (int i = 0; i < sizeof(guid->Data4); i++)
    {
        cur += _stprintf(cur, TEXT("%.2hhX"), guid->Data4[i]);
        if (i == 1)
        {
            *(cur++) = TEXT('-');
        }
    }
    *(cur++) = TEXT('}');
    *(cur++) = TEXT('\0');
    return result;
}
