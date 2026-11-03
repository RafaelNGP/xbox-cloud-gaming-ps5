/* SPDX-License-Identifier: GPL-3.0-only */
/* From XCloud4 (https://github.com/asp11edgr/XCloud4, src/video/stream_color_policy.c at 9aab6b1),
 * reused with its author's agreement: https://github.com/asp11edgr/XCloud4/issues/8 */
#include "stream_color_policy.h"

unsigned x4_stream_color_resolve(unsigned matrix, unsigned width,
    unsigned height, unsigned preference)
{
    (void)width;
    if (matrix == 1) return 1;
    if (matrix == 5 || matrix == 6) return 6;
    if (matrix == 2 && preference == 0 && height >= 720) return 1;
    return 6;
}
