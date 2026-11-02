#include "long_date.h"
#include <stdio.h>

#define WEEKDAY_COUNT 7
#define MONTH_COUNT 12

void long_date_format(char *out, size_t n, long_date_style_t style,
                      int year, int month0, int mday, int wday,
                      const char *const wdays[7], const char *const months[12]) {
    if (!out || n == 0) return;
    const char *wd = wdays[(wday >= 0 && wday < WEEKDAY_COUNT) ? wday : 0];
    const char *mo = months[(month0 >= 0 && month0 < MONTH_COUNT) ? month0 : 0];
    switch (style) {
        case LONG_DATE_D_DOT_MY:     snprintf(out, n, "%s, %d. %s %d", wd, mday, mo, year); break;
        case LONG_DATE_D_DE_M_DE_Y:  snprintf(out, n, "%s, %d de %s de %d", wd, mday, mo, year); break;
        case LONG_DATE_D_M_Y:        snprintf(out, n, "%s %d %s %d", wd, mday, mo, year); break;
        case LONG_DATE_MDY:
        default:                     snprintf(out, n, "%s, %s %d, %d", wd, mo, mday, year); break;
    }
}
