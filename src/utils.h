#ifndef UTILS_H
#define UTILS_H

#include <wtypes.h>

PTCHAR _tcsistr(PTCHAR haystack, const PTCHAR needle);
LPTSTR _guid_to_str(const GUID *guid);

#endif /* UTILS_H */
