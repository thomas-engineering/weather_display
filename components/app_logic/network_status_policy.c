#include "network_status_policy.h"

void network_status_policy_reset(network_status_policy_t *p) {
    p->error_active = false;
    p->toast = NSP_TOAST_HIDDEN;
}

void network_status_policy_on_fetch(network_status_policy_t *p, bool success, bool is_manual) {
    if (success) {
        p->error_active = false;
        if (is_manual) {
            p->toast = NSP_TOAST_SUCCESS;
        } else if (p->toast == NSP_TOAST_ERROR) {
            /* A silent refresh resolves a toast a manual one left stuck on
             * error — see this header's file comment. A toast already
             * hidden or mid-auto-dismiss is left alone. */
            p->toast = NSP_TOAST_HIDDEN;
        }
    } else {
        p->error_active = true;
        if (is_manual) p->toast = NSP_TOAST_ERROR;
        /* A silent failure never touches the toast — matches Claude Design's
         * refresh() only ever being wired to the manual retry paths. */
    }
}
