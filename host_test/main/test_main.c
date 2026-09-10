/*
 * Einziger app_main() im host_test-Binary — ESP-IDF's Linux-Target ruft
 * app_main() auf (siehe FreeRTOS-Kernel/portable/linux/port_idf.c:
 * main_task() ruft app_main(), nicht app_main() selbst ist der Prozess-
 * Einstiegspunkt), also darf nur eine Datei app_main()/setUp()/tearDown()
 * definieren. Jede getestete Komponente bekommt ihre eigene test_<name>.c
 * mit test_<name>_run(), aufgerufen hier.
 *
 * app_main() ist void — nach der Rueckkehr loescht main_task() nur die
 * eigene Task und der Scheduler laeuft weiter, ohne den Prozess zu beenden.
 * exit(UNITY_END()) beendet ihn selbst, mit der Anzahl fehlgeschlagener
 * Tests als Exit-Code — genau das, was host-test.sh als Erfolg/Fehlschlag
 * auswertet. */
#include "unity.h"
#include <stdlib.h>

void test_light_policy_run(void);
void test_storage_record_run(void);
void test_weather_forecast_parse_run(void);

void app_main(void) {
    UNITY_BEGIN();
    test_light_policy_run();
    test_storage_record_run();
    test_weather_forecast_parse_run();
    exit(UNITY_END());
}
