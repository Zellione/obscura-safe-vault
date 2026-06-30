#include "ui/natural_sort.h"

#include <cctype>

namespace ui {
namespace {

bool is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

// Length of the maximal digit run starting at index `i` in `s`.
size_t digit_run(std::string_view s, size_t i)
{
    size_t n = 0;
    while (i + n < s.size() && is_digit(static_cast<unsigned char>(s[i + n]))) ++n;
    return n;
}

// Compare two digit runs by numeric value, then by leading-zero count (fewer
// first). Returns < 0, 0, or > 0.
int compare_numeric(std::string_view ra, std::string_view rb)
{
    std::string_view sa = ra, sb = rb;   // significant digits (leading zeros stripped)
    while (sa.size() > 1 && sa.front() == '0') sa.remove_prefix(1);
    while (sb.size() > 1 && sb.front() == '0') sb.remove_prefix(1);

    if (sa.size() != sb.size()) return sa.size() < sb.size() ? -1 : 1;
    if (sa != sb)               return sa < sb ? -1 : 1;
    // Equal value: the run with fewer leading zeros (shorter raw run) sorts first.
    if (ra.size() != rb.size()) return ra.size() < rb.size() ? -1 : 1;
    return 0;
}

} // namespace

int natural_compare(std::string_view a, std::string_view b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[j]);

        if (is_digit(ca) && is_digit(cb)) {
            const size_t la = digit_run(a, i);
            const size_t lb = digit_run(b, j);
            if (const int c = compare_numeric(a.substr(i, la), b.substr(j, lb)); c != 0)
                return c;
            i += la;
            j += lb;
            continue;
        }

        const unsigned char la = static_cast<unsigned char>(std::tolower(ca));
        const unsigned char lb = static_cast<unsigned char>(std::tolower(cb));
        if (la != lb) return la < lb ? -1 : 1;
        ++i;
        ++j;
    }
    // A prefix sorts before the longer string.
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

} // namespace ui
