#include "favorites.h"
#include <stdio.h>

static int find_by_coords(const app_favorite_t favs[APP_FAVORITES_MAX], float lat, float lon) {
    for (int i = 0; i < APP_FAVORITES_MAX; i++) {
        if (favs[i].used && favs[i].lat == lat && favs[i].lon == lon) return i;
    }
    return -1;
}

bool app_favorites_toggle(app_favorite_t favs[APP_FAVORITES_MAX], const char *name,
                           const char *country, float lat, float lon) {
    int idx = find_by_coords(favs, lat, lon);
    if (idx != -1) {
        app_favorite_t empty = {0};
        favs[idx] = empty;
        return true;
    }

    int slot = -1;
    for (int i = 0; i < APP_FAVORITES_MAX; i++) {
        if (!favs[i].used) { slot = i; break; }
    }
    if (slot == -1) return false;

    snprintf(favs[slot].name, sizeof favs[slot].name, "%s", name ? name : "");
    snprintf(favs[slot].country, sizeof favs[slot].country, "%s", country ? country : "");
    favs[slot].lat = lat;
    favs[slot].lon = lon;
    favs[slot].used = true;
    return true;
}

void app_favorites_remove(app_favorite_t favs[APP_FAVORITES_MAX], int idx) {
    if (idx < 0 || idx >= APP_FAVORITES_MAX) return;
    app_favorite_t empty = {0};
    favs[idx] = empty;
}

bool app_favorites_contains(const app_favorite_t favs[APP_FAVORITES_MAX], float lat, float lon) {
    return find_by_coords(favs, lat, lon) != -1;
}
