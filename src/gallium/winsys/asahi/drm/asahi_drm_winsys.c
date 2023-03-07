/*
 * Copyright © 2014 Broadcom
 * Copyright © 208 Alyssa Rosenzweig
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "util/format/u_format.h"
#include "util/os_file.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_screen.h"

#include "asahi/agx_public.h"
#include "drm-uapi/drm.h"
#include "renderonly/renderonly.h"
#include "asahi_drm_public.h"

static struct pipe_screen *
asahi_screen_create(int fd, const struct pipe_screen_config *config,
                    struct renderonly *ro)
{
   return agx_screen_create(fd, ro, NULL);
}

struct pipe_screen *
asahi_drm_screen_create(int fd)
{
   return u_pipe_screen_lookup_or_create(os_dupfd_cloexec(fd), NULL, NULL,
                                         asahi_screen_create);
}

struct pipe_screen *
asahi_drm_screen_create_renderonly(struct renderonly *ro)
{
   return u_pipe_screen_lookup_or_create(os_dupfd_cloexec(ro->gpu_fd), NULL, ro,
                                         asahi_screen_create);
}
