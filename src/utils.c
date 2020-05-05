#include <ctype.h>

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
