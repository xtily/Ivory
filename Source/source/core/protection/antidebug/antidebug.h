#pragma once

namespace anti_debug {

    bool check();
    bool check_timing_anomaly();
    void start_watcher();
    void patch_remote_breakin();
    void patch_dbg_breakpoint();
    void hide_current_thread();

}
