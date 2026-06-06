#include "lsp_watchdog.h"
#include "lsp_client.h"
#include "command_execution.h"
#include "logger.h"
#include "screen_ui.h"
#include "editor_utils.h"
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

void lsp_watchdog_reset_counters(EditorState *state) {
    if (!state) return;
    state->lsp.watchdog_restart_attempts = 0;
    state->lsp.watchdog_disabled = false;
    state->lsp.watchdog_last_restart = 0;
}

void lsp_watchdog_check(EditorState *state) {
    if (!state || !state->lsp.enabled || state->lsp.watchdog_disabled) return;
    if (!state->lsp.client || state->lsp.client->server_pid <= 0) return;

    // Check if process is still alive by sending signal 0
    if (kill(state->lsp.client->server_pid, 0) == -1 && errno == ESRCH) {
        A2_LOG(LOG_ERROR, TAG_LSP, "LSP Server (PID %d) died unexpectedly.", state->lsp.client->server_pid);
        
        time_t now = time(NULL);
        if (now - state->lsp.watchdog_last_restart < 60) {
            state->lsp.watchdog_restart_attempts++;
        } else {
            // Reset if it was running fine for a while (> 60s)
            state->lsp.watchdog_restart_attempts = 1; 
        }
        
        state->lsp.watchdog_last_restart = now;
        
        if (state->lsp.watchdog_restart_attempts >= 3) {
            A2_LOG(LOG_FATAL, TAG_LSP, "LSP crashed 3 times in quick succession. Disabling LSP for this session.");
            state->lsp.watchdog_disabled = true;
            editor_set_status_msg(state, "LSP crashed too many times. Disabled.");
            lsp_shutdown(state); // Clean up remaining fds
            return;
        }
        
        A2_LOG(LOG_WARN, TAG_LSP, "Attempting LSP restart %d/3...", state->lsp.watchdog_restart_attempts);
        editor_set_status_msg(state, "LSP crashed. Restarting (%d/3)...", state->lsp.watchdog_restart_attempts);
        
        // Let process_lsp_restart handle the tear down and start
        process_lsp_restart(state);
    }
}