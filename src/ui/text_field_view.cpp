#include "ui/text_field_view.h"

namespace ui {

bool caret_is_on(uint64_t now_ms, uint64_t last_edit_ms) noexcept
{
    // Clock skew (or a chrome struct that has never seen an edit) must not make
    // the caret vanish; treat "before the last edit" as "just edited".
    const uint64_t since = now_ms >= last_edit_ms ? now_ms - last_edit_ms : 0;
    return (since / CARET_BLINK_MS) % 2 == 0;
}

} // namespace ui
