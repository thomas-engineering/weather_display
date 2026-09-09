/*
 * Host-Unit-Tests fuer components/app_logic.
 * Laeuft nativ auf dem Linux-Target, ohne Chip und ohne Emulator.
 *
 * Muster: Zeit und I/O werden als Fakes injiziert, damit die Logik
 * deterministisch und ohne Hardware pruefbar ist.
 */

#include <string.h>
#include "unity.h"
#include "app_logic.h"

/* ---- Fakes ------------------------------------------------------------- */

static uint64_t s_fake_now_us;
static uint8_t  s_out_buf[256];
static size_t   s_out_len;

static uint64_t fake_now_us(void)
{
    return s_fake_now_us;
}

static bool fake_write_out(const uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    if (s_out_len + len > sizeof(s_out_buf)) {
        return false;
    }
    memcpy(s_out_buf + s_out_len, buf, len);
    s_out_len += len;
    return true;
}

static const app_logic_io_t k_fake_io = {
    .now_us    = fake_now_us,
    .write_out = fake_write_out,
    .ctx       = NULL,
};

/* Unity ruft das vor bzw. nach jedem Test auf. */
void setUp(void)
{
    s_fake_now_us = 0;
    s_out_len     = 0;
    memset(s_out_buf, 0, sizeof(s_out_buf));
}

void tearDown(void)
{
}

/* ---- Tests ------------------------------------------------------------- */

static void test_init_setzt_grundzustand(void)
{
    app_logic_t s;
    app_logic_init(&s, &k_fake_io);

    TEST_ASSERT_EQUAL_size_t(0, s_out_len);
}

static void test_leerer_frame_wird_abgelehnt(void)
{
    app_logic_t s;
    app_logic_init(&s, &k_fake_io);

    TEST_ASSERT_LESS_THAN_INT(0, app_logic_feed(&s, NULL, 0));
}

static void test_timeout_nach_ablauf(void)
{
    app_logic_t s;
    app_logic_init(&s, &k_fake_io);

    /* Zeit kontrolliert vorspulen statt zu warten. */
    s_fake_now_us = 5ULL * 1000 * 1000;

    /* TODO: durch die echte Erwartung ersetzen. */
    TEST_ASSERT_TRUE(true);
}

/* ---- Runner ------------------------------------------------------------ */

/*
 * Falls der Linker ueber 'main' stolpert, weil das FreeRTOS-Startup
 * mitgezogen wird: in "void app_main(void)" umbenennen und das return
 * weglassen.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_setzt_grundzustand);
    RUN_TEST(test_leerer_frame_wird_abgelehnt);
    RUN_TEST(test_timeout_nach_ablauf);
    return UNITY_END();  /* Anzahl Fehlschlaege = Exit-Code */
}
