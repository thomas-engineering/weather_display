#ifndef LONG_DATE_H
#define LONG_DATE_H

#include <stddef.h>

/* Hardware-free assembly of the "long" date line (screen saver). The name
 * tables stay with the other translated strings in main/weather_i18n.c and come
 * in as arguments; what lives here is the part that differs per language: the
 * order of the pieces and the separators, mirroring what
 * Intl.DateTimeFormat(locale, {weekday, day, month, year: 'long'|'numeric'})
 * gives the design. */
typedef enum {
    LONG_DATE_MDY,            /* "Monday, November 2, 2026" */
    LONG_DATE_D_DOT_MY,       /* "Montag, 2. November 2026" */
    LONG_DATE_D_DE_M_DE_Y,    /* "lunes, 2 de noviembre de 2026" */
    LONG_DATE_D_M_Y,          /* "lundi 2 novembre 2026" */
} long_date_style_t;

/* `month0` is 0-11 and `wday` 0-6 (Sunday first), like struct tm; out-of-range
 * values fall back to index 0. `year` is the full year (2026). The result is
 * always NUL-terminated and cut at `n` if it does not fit. */
void long_date_format(char *out, size_t n, long_date_style_t style,
                      int year, int month0, int mday, int wday,
                      const char *const wdays[7], const char *const months[12]);

#endif
