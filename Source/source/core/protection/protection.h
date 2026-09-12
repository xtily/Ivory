#pragma once

#include <core/protection/antidebug/antidebug.h>
#include <core/protection/antiattach/antiattach.h>
#include <core/protection/antidump/antidump.h>
#include <core/protection/encryption/xorstr.h>

namespace paw_guard {

    inline void init() {
        anti_debug::patch_remote_breakin();
        anti_debug::patch_dbg_breakpoint();
        anti_debug::hide_current_thread();
        anti_debug::check();
        anti_attach::check();
        anti_debug::start_watcher();
    }

}