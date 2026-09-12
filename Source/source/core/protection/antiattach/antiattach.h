#pragma once

namespace anti_attach {

    void set_critical_process();
    bool check_hardware_breakpoints();
    bool check_debug_object();
    bool check_api_hooks();
    bool check_untrusted_modules();
    void check();

}
