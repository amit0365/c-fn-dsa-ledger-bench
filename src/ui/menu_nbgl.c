/*****************************************************************************
 *   FN-DSA Ledger bench app — minimal menu UI for Stax / Flex.
 *
 *  Strips the boilerplate's transaction-signing menu down to a no-frills
 *  "ready screen + quit" pair. This app has no user-driven flows (it only
 *  responds to APDU commands), so the UI is intentionally trivial.
 *****************************************************************************/

#include "os.h"
#include "ux.h"
#include "glyphs.h"
#include "nbgl_use_case.h"

#include "globals.h"
#include "menu.h"

static void on_quit(void) {
    os_sched_exit(-1);
}

void ui_menu_main(void) {
    nbgl_useCaseHomeAndSettings("FN-DSA Bench",
                                &C_app_boilerplate_64px,
                                "FN-DSA-512 / FNDSA_LOW_RAM\nbench harness — APDU-driven.",
                                INIT_HOME_PAGE,
                                NULL,
                                NULL,
                                NULL,
                                on_quit);
}

void ui_menu_about(void) {
    /* No about page; shimmed for callers expecting the symbol. */
    ui_menu_main();
}
