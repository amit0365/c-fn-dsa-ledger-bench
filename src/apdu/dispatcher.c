/*****************************************************************************
 *   FN-DSA Ledger bench app.
 *
 *  APDU command dispatcher. Routes incoming commands to the appropriate
 *  handler based on the INS byte.
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
#include "../handler/run_bench.h"

int apdu_dispatcher(const command_t *cmd) {
    LEDGER_ASSERT(cmd != NULL, "NULL cmd");

    if (cmd->cla != CLA) {
        return io_send_sw(SWO_INVALID_CLA);
    }

    switch (cmd->ins) {
        case GET_VERSION:
            if (cmd->p1 != 0 || cmd->p2 != 0) return io_send_sw(SWO_INCORRECT_P1_P2);
            return handler_get_version();

        case GET_APP_NAME:
            if (cmd->p1 != 0 || cmd->p2 != 0) return io_send_sw(SWO_INCORRECT_P1_P2);
            return handler_get_app_name();

        case INS_PROVISION:
            if (cmd->p1 != 0 || cmd->p2 != 0) return io_send_sw(SWO_INCORRECT_P1_P2);
            return handler_provision_basis();

        case INS_RUN_BENCH:
            /* p1 = iteration count (1..255). 0 means "use default 10". */
            if (cmd->p2 != 0) return io_send_sw(SWO_INCORRECT_P1_P2);
            return handler_run_bench(cmd->p1);

        case INS_KAT_CHECK:
            if (cmd->p1 != 0 || cmd->p2 != 0) return io_send_sw(SWO_INCORRECT_P1_P2);
            return handler_kat_check();

        default:
            return io_send_sw(SWO_INVALID_INS);
    }
}
