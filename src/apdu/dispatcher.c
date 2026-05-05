/*****************************************************************************
 *   FN-DSA Ledger bench app.
 *
 *  APDU command dispatcher. Routes incoming commands to the appropriate
 *  handler based on the INS byte. Phase-2 stub: real handlers TBD.
 *****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

#include "buffer.h"
#include "io.h"
#include "ledger_assert.h"

#include "dispatcher.h"
#include "constants.h"
#include "globals.h"
#include "types.h"
#include "sw.h"
#include "get_version.h"
#include "get_app_name.h"

int apdu_dispatcher(const command_t *cmd) {
    LEDGER_ASSERT(cmd != NULL, "NULL cmd");

    if (cmd->cla != CLA) {
        return io_send_sw(SWO_INVALID_CLA);
    }

    switch (cmd->ins) {
        case GET_VERSION:
            if (cmd->p1 != 0 || cmd->p2 != 0) {
                return io_send_sw(SWO_INCORRECT_P1_P2);
            }
            return handler_get_version();

        case GET_APP_NAME:
            if (cmd->p1 != 0 || cmd->p2 != 0) {
                return io_send_sw(SWO_INCORRECT_P1_P2);
            }
            return handler_get_app_name();

        case INS_RUN_BENCH:
        case INS_KAT_CHECK:
        case INS_PROVISION:
            /* Phase-2 placeholder: real handlers in Phase 3. Return
               a stub success response so the dispatcher links. */
            return io_send_sw(0x9000);

        default:
            return io_send_sw(SWO_INVALID_INS);
    }
}
