/*
 * Host-Simulator fuer das Weather-Display-UI.
 *
 * Zeigt exakt die UI aus main/weather_ui.c in einem 1024x600-SDL-Fenster auf dem
 * Entwicklungsrechner. Die Maus ersetzt den Touchscreen, die PC-Tastatur schreibt
 * zusaetzlich in die Textfelder (Suche, WLAN-Passwort) — die On-Screen-Tastatur
 * der UI funktioniert daneben unveraendert.
 *
 * Die Rolle, die auf dem Geraet main.c + app_weather.c spielen, uebernimmt hier
 * dieses File: Es setzt Fixture-Daten, beantwortet die UI-Callbacks und haelt die
 * Einstellungen. Es wird kein Netz angefasst und nichts persistiert.
 *
 * Wichtig fuer die Treue zum Geraet: die UI-Callbacks laufen im LVGL-Kontext und
 * duerfen dort nicht sofort zurueckschreiben. Auf dem P4 posten sie in die Queue
 * des Weather-Workers, der die Setter spaeter unter dem Display-Lock aufruft.
 * Hier uebernimmt ein einmaliger lv_timer dieselbe Rolle — mit einer kleinen
 * kuenstlichen Latenz, damit Lade- und Suchzustaende sichtbar werden statt in
 * einem Frame durchzulaufen.
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "lvgl.h"

#include "app_format.h"
#include "weather_i18n.h"
#include "weather_ui.h"

#define SIM_HOR_RES 1024
#define SIM_VER_RES 600

/* Antwortzeit der gefakten "Netzwerkaufrufe". Lang genug, um Spinner und
 * Ladezustaende tatsaechlich zu sehen, kurz genug, um nicht zu nerven. */
#define SIM_LATENCY_MS      700
#define SIM_SCAN_LATENCY_MS 1500

/* ---- Zustand, den auf dem Geraet app_prefs haelt ------------------------- */
/* Startwerte wie in app_prefs_load(): LANG_EN, Celsius, km/h, 24h, Trier. */

static weather_lang_t s_lang = LANG_EN;
static wx_temp_unit_t s_temp_unit = WX_UNIT_C;
static wx_wind_unit_t s_wind_unit = WX_WIND_KMH;
static wx_time_fmt_t  s_time_fmt  = WX_TIME_24;

static char s_city[64]    = "Trier";
static char s_country[64] = "Germany";

/* ---- Fixtures ----------------------------------------------------------- */

/* Eine Woche mit Absicht gemischtem Wetter: jede Icon-Kategorie und jeder
 * Zustand der Tageskarten kommt mindestens einmal vor. WMO-Codes wie von
 * Open-Meteo: 0 klar, 2 teils bewoelkt, 3 bedeckt, 45 Nebel, 61 Regen,
 * 71 Schnee, 80 Schauer, 95 Gewitter. */
static const int   k_code[WEATHER_UI_DAYS]      = {  3,   61,   80,    2,    0,   71,   95 };
static const float k_tmax[WEATHER_UI_DAYS]      = { 18.4f, 16.1f, 19.7f, 22.3f, 24.8f,  1.2f, 17.0f };
static const float k_tmin[WEATHER_UI_DAYS]      = {  11.2f, 10.4f, 12.0f, 13.6f, 15.1f, -3.4f, 11.8f };
static const float k_wind[WEATHER_UI_DAYS]      = { 12.0f, 24.5f, 31.2f,  9.0f,  6.4f, 18.9f, 44.0f };
static const int   k_precip[WEATHER_UI_DAYS]    = {  35,    80,    65,    15,     0,    70,    90 };
/* Tag 0 bekommt bewusst etwas Regen: sonst bleibt das Balken-Panel des Charts
 * beim Start leer und man sieht die Haelfte der Darstellung nie. */
static const float k_precip_mm[WEATHER_UI_DAYS] = { 0.8f,  4.2f,  2.6f,  0.0f,  0.0f,  1.8f,  7.5f };

static const int s_humidity = 68;

/* Tagesgang: Minimum kurz vor Sonnenaufgang, Maximum am spaeten Nachmittag.
 * Reicht fuer den Chart, ist bewusst keine Wettersimulation. */
static void fill_hourly(weather_hourly_t *h, int day)
{
    const float tmax = k_tmax[day], tmin = k_tmin[day];
    const float mid = (tmax + tmin) / 2.0f, amp = (tmax - tmin) / 2.0f;

    h->count = WEATHER_UI_CHART_POINTS;
    for (int i = 0; i < WEATHER_UI_CHART_POINTS; i++) {
        h->hour[i] = i;
        h->temp_c[i] = mid - amp * cosf((float)(i - 5) / 24.0f * 2.0f * (float)M_PI);
        /* Nachtregen, trockener Vormittag, Schauer am Nachmittag. Die Randstunden
         * sind mit Absicht nicht null: nur so bekommt der Chart Balken direkt an
         * der linken und rechten Panelkante, und nur dort faellt auf, wenn etwas
         * ueber den Rand haengt und abgeschnitten wird. */
        const float share = (i <= 3 || i >= 22) ? 0.10f
                          : (i >= 12 && i <= 19) ? 0.25f
                          : 0.0f;
        h->precip_mm[i] = k_precip_mm[day] * share;
    }
}

/* Baut aus den Fixtures oben den kompletten Datensatz und schiebt ihn in die UI.
 * Wird auch nach jeder Einstellungsaenderung erneut aufgerufen, weil Datums-,
 * Wochentags- und "Real feel"-Texte von der App vorformatiert kommen — genau wie
 * in main.c auf dem Geraet. */
static void publish_weather(void)
{
    const time_t now = time(NULL);
    struct tm lt = *localtime(&now);

    weather_current_t cur = {0};
    snprintf(cur.location_name, sizeof(cur.location_name), "%s", s_city);
    snprintf(cur.location_country, sizeof(cur.location_country), "%s", s_country);
    fmt_time(cur.time_str, sizeof(cur.time_str), &lt, s_time_fmt);
    fmt_date(cur.date_str, sizeof(cur.date_str), &lt, s_lang);

    cur.weather_code = k_code[0];
    /* Aktuelle Temperatur aus dem Tagesgang der laufenden Stunde. */
    weather_hourly_t today;
    fill_hourly(&today, 0);
    cur.temp_c = today.temp_c[lt.tm_hour];
    cur.feels_like_c = cur.temp_c - 1.8f;
    cur.humidity_pct = s_humidity;
    cur.wind_kmh = k_wind[0];
    cur.precip_pct = k_precip[0];
    fmt_real_feel(cur.real_feel_text, sizeof(cur.real_feel_text),
                  cur.feels_like_c, cur.temp_c, cur.wind_kmh, cur.precip_pct, s_lang);

    weather_day_t days[WEATHER_UI_DAYS] = {0};
    weather_hourly_t hourly[WEATHER_UI_DAYS];
    const weather_hourly_t *hourly_ptr[WEATHER_UI_DAYS];

    for (int i = 0; i < WEATHER_UI_DAYS; i++) {
        time_t t = now + (time_t)i * 86400;
        struct tm d = *localtime(&t);

        fmt_day_label(days[i].day_label, sizeof(days[i].day_label), &d, i, s_lang);
        fmt_day_date(days[i].date_label, sizeof(days[i].date_label), &d, s_lang);
        days[i].weather_code = k_code[i];
        days[i].temp_max_c = k_tmax[i];
        days[i].temp_min_c = k_tmin[i];
        days[i].feels_max_c = k_tmax[i] - 1.5f;
        days[i].feels_min_c = k_tmin[i] - 2.0f;
        days[i].wind_max_kmh = k_wind[i];
        days[i].precip_pct = k_precip[i];

        fill_hourly(&hourly[i], i);
        hourly_ptr[i] = &hourly[i];
    }

    weather_ui_set_current(&cur);
    weather_ui_set_days(days);
    weather_ui_set_hourly(&today, hourly_ptr);
    weather_ui_set_loading(false);
    weather_ui_set_error(NULL);
    weather_ui_set_network_status(WX_NET_ONLINE);
    weather_ui_set_data_stale(false);

    /* Sample data for the Settings > Device information dialog (--screen
     * device-info) — the real values come from app_weather.c on the device,
     * which the simulator has no equivalent of. */
    weather_device_info_t dev_info = {
        .device_name = "Weather Display",
        .hardware_version = "ESP32-P4 Rev 1.3",
        .firmware_version = "1.0.0-sim",
        .online = true,
        .ip = "192.168.1.42",
        .dns = "1.1.1.1",
        .gateway = "192.168.1.1",
        .note = "Created by M. Thomas using Claude Design and Claude Code.",
    };
    weather_ui_set_device_info(&dev_info);
}

/* ---- Fake-Geocoding ----------------------------------------------------- */

typedef struct { const char *name, *sub; } sim_city_t;

static const sim_city_t k_cities[] = {
    { "Trier",     "Rheinland-Pfalz, Germany" },
    { "Trieste",   "Friuli Venezia Giulia, Italy" },
    { "Nürnberg",  "Bavaria, Germany" },
    { "Zürich",    "Switzerland" },
    { "Berlin",    "Germany" },
    { "Hamburg",   "Germany" },
    { "Paris",     "Ile-de-France, France" },
    { "Madrid",    "Community of Madrid, Spain" },
    { "London",    "England, United Kingdom" },
    { "New York",  "New York, United States" },
    { "Tokyo",     "Japan" },
};
#define SIM_CITY_COUNT ((int)(sizeof(k_cities) / sizeof(k_cities[0])))

static char s_query[64];
static int  s_result_idx[SIM_CITY_COUNT];
static int  s_result_count;

/* Teilstring-Suche, damit eine Eingabe ohne Treffer auch wirklich zu null
 * Ergebnissen fuehrt — der "no cities"-Zustand der UI ist sonst nie zu sehen. */
static void run_search(void)
{
    s_result_count = 0;
    if (s_query[0] == '\0') return;

    for (int i = 0; i < SIM_CITY_COUNT; i++) {
        if (strcasestr(k_cities[i].name, s_query) != NULL) {
            s_result_idx[s_result_count++] = i;
        }
    }
}

/* ---- Fake-WLAN ---------------------------------------------------------- */

static const wx_wifi_network_t k_networks[] = {
    { "FRITZ!Box 7590 XY", 3, true  },
    { "Vodafone-2E4A",     2, true  },
    { "Telekom_FON",       2, false },
    { "eduroam",           1, true  },
    { "Gast-WLAN",         1, false },
};
#define SIM_NETWORK_COUNT ((int)(sizeof(k_networks) / sizeof(k_networks[0])))

static char s_wifi_pass[64];

/* ---- Verzoegerte Antworten (Ersatz fuer die Worker-Queue) ---------------- */

typedef enum {
    ACT_SEARCH_RESULTS = 1,
    ACT_CITY_SELECTED,
    ACT_REFRESH_DONE,
    ACT_MANUAL_REFRESH_DONE,
    ACT_WIFI_SCAN_RESULTS,
    ACT_WIFI_CONNECT_RESULT,
    ACT_OPEN_SCREEN,
} sim_action_t;

static void open_screen(void);

static void deferred_cb(lv_timer_t *timer)
{
    const sim_action_t action = (sim_action_t)(uintptr_t)lv_timer_get_user_data(timer);

    switch (action) {
    case ACT_SEARCH_RESULTS: {
        const char *names[SIM_CITY_COUNT];
        const char *subs[SIM_CITY_COUNT];
        for (int i = 0; i < s_result_count; i++) {
            names[i] = k_cities[s_result_idx[i]].name;
            subs[i]  = k_cities[s_result_idx[i]].sub;
        }
        weather_ui_set_searching(false);
        weather_ui_set_search_results(names, subs, s_result_count);
        break;
    }
    case ACT_CITY_SELECTED:
        publish_weather();
        break;

    case ACT_REFRESH_DONE:
        publish_weather();
        break;

    /* Distinct from ACT_REFRESH_DONE (used for the silent initial load): only
     * a user-triggered refresh shows the toast, matching real firmware's
     * is_manual distinction in app_weather.c's do_refresh(). */
    case ACT_MANUAL_REFRESH_DONE:
        publish_weather();
        weather_ui_show_refresh_toast(true);
        break;

    case ACT_WIFI_SCAN_RESULTS:
        weather_ui_set_wifi_scan_results(k_networks, SIM_NETWORK_COUNT);
        break;

    case ACT_WIFI_CONNECT_RESULT:
        /* Ein Fehlschlagspfad muss erreichbar sein, sonst laesst sich der
         * Fehlerzustand des Setup-Screens nie ansehen: Passwort "falsch"
         * scheitert, alles andere klappt. */
        weather_ui_set_wifi_connect_result(strcmp(s_wifi_pass, "falsch") != 0);
        break;

    case ACT_OPEN_SCREEN:
        /* Erst nach publish_weather(): ein Detailpanel ohne Daten zeigt leere
         * Felder, und der Chart darin misst sich auf Hoehe null. */
        open_screen();
        break;
    }
}

static void defer(sim_action_t action, uint32_t delay_ms)
{
    lv_timer_t *t = lv_timer_create(deferred_cb, delay_ms, (void *)(uintptr_t)action);
    lv_timer_set_repeat_count(t, 1); /* einmalig, LVGL raeumt danach selbst auf */
}

/* ---- UI-Callbacks (Gegenstueck zu denen in main/main.c) ------------------ */

static void on_search(const char *query)
{
    snprintf(s_query, sizeof(s_query), "%s", query ? query : "");
    run_search();
    weather_ui_set_searching(true);
    defer(ACT_SEARCH_RESULTS, SIM_LATENCY_MS);
}

static void on_select_city(int idx)
{
    if (idx < 0 || idx >= s_result_count) return;
    const sim_city_t *c = &k_cities[s_result_idx[idx]];
    snprintf(s_city, sizeof(s_city), "%s", c->name);
    /* Die UI zeigt unter dem Ortsnamen das Land, nicht die volle Region — vom
     * letzten Komma an ist genau das der Rest. */
    const char *comma = strrchr(c->sub, ',');
    snprintf(s_country, sizeof(s_country), "%s", comma ? comma + 2 : c->sub);
    weather_ui_set_loading(true);
    defer(ACT_CITY_SELECTED, SIM_LATENCY_MS);
}

static void on_refresh(void)
{
    weather_ui_set_loading(true);
    defer(ACT_MANUAL_REFRESH_DONE, SIM_LATENCY_MS);
}

static void on_settings_changed(weather_lang_t lang, wx_temp_unit_t t, wx_wind_unit_t w, wx_time_fmt_t tf)
{
    s_lang = lang;
    s_temp_unit = t;
    s_wind_unit = w;
    s_time_fmt = tf;
    /* Einheiten rechnet weather_ui.c selbst um; Datum, Wochentag und der
     * "Real feel"-Satz kommen vorformatiert von hier und muessen neu. */
    publish_weather();
}

static void on_wifi_scan(void)
{
    defer(ACT_WIFI_SCAN_RESULTS, SIM_SCAN_LATENCY_MS);
}

static void on_wifi_connect(const char *ssid, const char *password)
{
    (void)ssid;
    snprintf(s_wifi_pass, sizeof(s_wifi_pass), "%s", password ? password : "");
    defer(ACT_WIFI_CONNECT_RESULT, SIM_SCAN_LATENCY_MS);
}

/* The simulator has no real backlight to drive; this only needs to exist so
 * weather_ui_set_brightness_callback() has a target. */
static void on_brightness(int percent, bool final)
{
    (void)percent;
    (void)final;
}

/* The simulator has no real NVS-backed credential store to clear; this only
 * needs to exist so weather_ui_set_wifi_callbacks() has all three callbacks. */
static void on_wifi_forget(void)
{
    weather_ui_set_network_status(WX_NET_OFFLINE);
}

/* ---- Screen-Auswahl ----------------------------------------------------- */

/* Die Screens hinter dem Hauptbild oeffnet auf dem Geraet ein Fingertipp. Fuer
 * die Aufnahme muss der Simulator denselben Weg gehen, ohne Finger.
 *
 * Gesucht wird ueber den Objektbaum statt ueber feste Koordinaten: ein Klick auf
 * "990,32" trifft das Zahnrad nur so lange, wie das Design es dort laesst — und
 * die Aufnahmen sind gerade dazu da, Designaenderungen sichtbar zu machen. Ein
 * Label mit bekanntem Text zu suchen und dessen anklickbares Elternteil zu
 * feuern, ueberlebt eine Umgruppierung. */

typedef enum {
    SCREEN_MAIN,
    SCREEN_SEARCH,
    SCREEN_SETTINGS,
    SCREEN_SETTINGS_ADAPTIVE_ON,
    SCREEN_DEVICE_INFO,
    SCREEN_REFRESH_TOAST,
    SCREEN_DETAIL,
    SCREEN_WIFI,
} sim_screen_t;

static sim_screen_t s_screen = SCREEN_MAIN;

static lv_obj_t *find_label(lv_obj_t *parent, const char *text)
{
    uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(child, &lv_label_class)) {
            const char *t = lv_label_get_text(child);
            if (t != NULL && strcmp(t, text) == 0) return child;
        }
        lv_obj_t *found = find_label(child, text);
        if (found != NULL) return found;
    }
    return NULL;
}

static lv_obj_t *find_by_class(lv_obj_t *parent, const lv_obj_class_t *cls)
{
    uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(child, cls)) return child;
        lv_obj_t *found = find_by_class(child, cls);
        if (found != NULL) return found;
    }
    return NULL;
}

/* Die Symbole sitzen als Label in einem Button, die Tagesbeschriftung in der
 * Tageskarte — geklickt wird also nicht das Label, sondern sein naechster
 * anklickbarer Vorfahr. LV_EVENT_CLICKED ist genau das, worauf die
 * Callbacks in weather_ui.c registriert sind. */
static bool click_owner_of(lv_obj_t *label)
{
    for (lv_obj_t *o = label; o != NULL; o = lv_obj_get_parent(o)) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
            lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
            return true;
        }
    }
    return false;
}

static void warn_missing(const char *what)
{
    fprintf(stderr, "WARNUNG: %s im Objektbaum nicht gefunden — Screen bleibt zu.\n"
                    "Hat sich die UI geaendert, muss die Suche in sim/main.c nachziehen.\n", what);
}

static void open_screen(void)
{
    lv_obj_t *root = lv_screen_active();

    switch (s_screen) {
    case SCREEN_MAIN:
        break;

    case SCREEN_SEARCH: {
        lv_obj_t *pin = find_label(root, LV_SYMBOL_GPS);
        if (pin == NULL || !click_owner_of(pin)) { warn_missing("Ortsmarke im Header"); break; }
        /* Eine leere Suchmaske zeigt vom Design fast nichts. Der Text geht ueber
         * LV_EVENT_VALUE_CHANGED durch denselben Pfad wie echtes Tippen, also
         * laeuft auch die Trefferliste echt durch. */
        lv_obj_t *ta = find_by_class(root, &lv_textarea_class);
        if (ta != NULL) lv_textarea_set_text(ta, "Tri");
        break;
    }

    case SCREEN_SETTINGS: {
        lv_obj_t *gear = find_label(root, LV_SYMBOL_SETTINGS);
        if (gear == NULL || !click_owner_of(gear)) warn_missing("Zahnrad im Header");
        break;
    }

    case SCREEN_DEVICE_INFO: {
        /* Settings > the new info button opens Device information on top of
         * it (see the "device info dialog" comment in weather_ui.c) — open
         * both in sequence like a real tap would. */
        lv_obj_t *gear = find_label(root, LV_SYMBOL_SETTINGS);
        if (gear == NULL || !click_owner_of(gear)) { warn_missing("Zahnrad im Header"); break; }
        lv_obj_t *info = find_label(root, "i");
        if (info == NULL || !click_owner_of(info)) warn_missing("Info-Knopf im Einstellungsdialog");
        break;
    }

    case SCREEN_REFRESH_TOAST: {
        /* Clicking the header's refresh icon runs the real on_refresh() path
         * (ACT_MANUAL_REFRESH_DONE, SIM_LATENCY_MS later), so the toast this
         * captures is the same one a real tap produces — use
         * --screenshot-after somewhere between SIM_LATENCY_MS+200+
         * SIM_LATENCY_MS (when it appears) and +1000 more (when success
         * auto-dismisses it), e.g. 2000. */
        lv_obj_t *refresh = find_label(root, LV_SYMBOL_REFRESH);
        if (refresh == NULL || !click_owner_of(refresh)) warn_missing("Refresh-Icon im Header");
        break;
    }

    case SCREEN_SETTINGS_ADAPTIVE_ON: {
        /* Renders the "Adapt to ambient light" switch in its checked state —
         * the simulator otherwise only ever shows it unavailable (no camera),
         * so the Nocturne checked-state styling (accent track/border, light
         * knob) had no screenshot coverage at all. Setting the state here,
         * after the screen has already gone through at least one render
         * pass, matters: lv_switch's own animation code
         * (lv_switch_trigger_anim -> `if (!obj->rendered) return;`) silently
         * skips positioning the knob if the state is set before the very
         * first draw. */
        lv_obj_t *gear = find_label(root, LV_SYMBOL_SETTINGS);
        if (gear == NULL || !click_owner_of(gear)) { warn_missing("Zahnrad im Header"); break; }
        weather_ui_set_brightness_adaptive_available(true);
        weather_ui_set_brightness_adaptive(true);
        break;
    }

    case SCREEN_DETAIL: {
        /* Die erste Tageskarte traegt als einzige diesen Text. */
        lv_obj_t *today = find_label(root, weather_strings[s_lang].today);
        if (today == NULL || !click_owner_of(today)) warn_missing("Tageskarte \"heute\"");
        break;
    }

    case SCREEN_WIFI:
        weather_ui_open_wifi_setup();
        break;
    }
}

/* ---- Screenshot --------------------------------------------------------- */

/* Liest den fertig gezeichneten Frame aus dem SDL-Renderer und legt ihn als BMP
 * ab. Praktisch fuer Design-Vergleiche und fuer einen Blick auf die UI ohne
 * Grafiksitzung — mit SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software laeuft
 * das auch ueber eine reine Terminalverbindung. */
static bool save_screenshot(lv_display_t *disp, const char *path)
{
    SDL_Renderer *renderer = lv_sdl_window_get_renderer(disp);
    if (renderer == NULL) return false;

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, SIM_HOR_RES, SIM_VER_RES, 32,
                                                          SDL_PIXELFORMAT_ARGB8888);
    if (surface == NULL) return false;

    bool ok = SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                   surface->pixels, surface->pitch) == 0
              && SDL_SaveBMP(surface, path) == 0;
    if (!ok) fprintf(stderr, "FEHLER: Screenshot fehlgeschlagen: %s\n", SDL_GetError());

    SDL_FreeSurface(surface);
    return ok;
}

/* ---- Einstieg ----------------------------------------------------------- */

static uint32_t tick_get_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int main(int argc, char **argv)
{
    bool open_wifi_setup = false;
    const char *screenshot_path = NULL;
    /* 3 s: der WLAN-Scan im Simulator antwortet nach 1,5 s, und geoeffnet wird
     * erst nach den Wetterdaten. Kuerzer nimmt den Screen halb leer auf. */
    uint32_t screenshot_after_ms = 3000;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wifi-setup") == 0) {
            open_wifi_setup = true;
        } else if (strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
            const char *name = argv[++i];
            if      (strcmp(name, "main")     == 0) s_screen = SCREEN_MAIN;
            else if (strcmp(name, "search")   == 0) s_screen = SCREEN_SEARCH;
            else if (strcmp(name, "settings") == 0) s_screen = SCREEN_SETTINGS;
            else if (strcmp(name, "settings-adaptive-on") == 0) s_screen = SCREEN_SETTINGS_ADAPTIVE_ON;
            else if (strcmp(name, "device-info") == 0) s_screen = SCREEN_DEVICE_INFO;
            else if (strcmp(name, "refresh-toast") == 0) s_screen = SCREEN_REFRESH_TOAST;
            else if (strcmp(name, "detail")   == 0) s_screen = SCREEN_DETAIL;
            else if (strcmp(name, "wifi")     == 0) s_screen = SCREEN_WIFI;
            else {
                fprintf(stderr, "Unbekannter Screen '%s'. Moeglich: main, search, "
                                "settings, settings-adaptive-on, device-info, refresh-toast, detail, wifi\n", name);
                return 2;
            }
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if (strcmp(argv[i], "--screenshot-after") == 0 && i + 1 < argc) {
            screenshot_after_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else {
            const bool asked = strcmp(argv[i], "--help") == 0;
            fprintf(asked ? stdout : stderr,
                    "Aufruf: %s [--screen NAME | --wifi-setup]\n"
                    "        [--screenshot DATEI.bmp [--screenshot-after MS]]\n"
                    "  --screen            oeffnet den Screen nach dem Laden der Daten:\n"
                    "                      main (Vorgabe), search, settings,\n"
                    "                      settings-adaptive-on, device-info, refresh-toast, detail, wifi\n"
                    "                      refresh-toast braucht ein kurzes --screenshot-after\n"
                    "                      (z.B. 2000) — der Toast blendet sich nach 1s\n"
                    "                      wieder aus, der Standard-Wert (3000) verpasst ihn.\n"
                    "  --wifi-setup        WLAN-Setup im Erstboot-Zustand: offline, ohne\n"
                    "                      Wetterdaten. Nicht dasselbe wie --screen wifi.\n"
                    "  --screenshot        schreibt den Frame als BMP und beendet sich\n"
                    "  --screenshot-after  Wartezeit davor in ms (Vorgabe 3000, damit\n"
                    "                      Daten, Scan und Einblendanimationen durch sind)\n",
                    argv[0]);
            return asked ? 0 : 2;
        }
    }

    lv_init();
    lv_tick_set_cb(tick_get_cb);

    lv_display_t *disp = lv_sdl_window_create(SIM_HOR_RES, SIM_VER_RES);
    if (disp == NULL) {
        /* Kein Fenster: keine Grafiksitzung, oder SDL findet keinen passenden
         * Renderer. Headless geht es mit dem Software-Renderer:
         *   SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software ./weather_sim */
        fprintf(stderr, "FEHLER: SDL konnte kein Fenster oeffnen. Laeuft eine Grafiksitzung?\n");
        return 1;
    }
    lv_sdl_window_set_title(disp, "weather_display — Simulator (1024x600)");

    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();

    /* Die PC-Tastatur schreibt in das fokussierte Textfeld. Nur Widgets, die
     * LVGL selbst als gruppierbar markiert (Textarea, Keyboard), landen in der
     * Default-Gruppe — die Karten und Labels der UI bleiben aussen vor. */
    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_set_group(lv_sdl_keyboard_create(), group);

    weather_ui_create(lv_screen_active());
    weather_ui_set_callbacks(on_search, on_select_city, on_refresh, on_settings_changed);
    weather_ui_set_wifi_callbacks(on_wifi_scan, on_wifi_connect, on_wifi_forget);
    weather_ui_set_brightness_callback(on_brightness);
    /* The simulator has no camera; render the adaptive switch the way a real
     * board with nothing on the MIPI-CSI connector would. */
    weather_ui_set_brightness_adaptive_available(false);
    weather_ui_set_language(s_lang);
    weather_ui_set_units(s_temp_unit, s_wind_unit, s_time_fmt);

    if (open_wifi_setup) {
        weather_ui_set_network_status(WX_NET_OFFLINE);
        weather_ui_set_loading(false);
        weather_ui_open_wifi_setup();
    } else {
        weather_ui_set_loading(true);
        defer(ACT_REFRESH_DONE, SIM_LATENCY_MS);
        if (s_screen != SCREEN_MAIN) {
            defer(ACT_OPEN_SCREEN, SIM_LATENCY_MS + 200);
        }
    }

    /* LV_SDL_DIRECT_EXIT ist an: Fenster zu -> exit(0) aus dem Treiber heraus. */
    const uint32_t started_ms = tick_get_cb();
    while (true) {
        uint32_t idle_ms = lv_timer_handler();
        if (idle_ms > 16) idle_ms = 16; /* ~60 Hz, damit die Maus fluessig bleibt */
        if (idle_ms < 1) idle_ms = 1;   /* nie ohne Pause drehen */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = (long)idle_ms * 1000000L };
        nanosleep(&ts, NULL);

        if (screenshot_path != NULL && tick_get_cb() - started_ms >= screenshot_after_ms) {
            return save_screenshot(disp, screenshot_path) ? 0 : 1;
        }
    }
}
