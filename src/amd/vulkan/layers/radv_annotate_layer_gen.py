COPYRIGHT=u"""
/* Copyright © 2021 Intel Corporation
 * Copyright © 2024 Valve Corporation
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
"""

import argparse
import os
import sys
import xml.etree.ElementTree as et

import mako
from mako.template import Template

sys.path.append(os.path.join(sys.path[0], '../../../vulkan/util/'))

from vk_entrypoints import get_entrypoints_from_xml

EXCLUDED_COMMANDS = [
    'CmdBeginRenderPass',
    'CmdEndRenderPass',
    'CmdDispatch',
]

TEMPLATE = Template(COPYRIGHT + """
/* This file generated from ${filename}, don't edit directly. */

#include "radv_private.h"

#define ANNOTATE(command, ...) \
   radv_cmd_buffer_annotate(radv_cmd_buffer_from_handle(commandBuffer), #command); \
   radv_cmd_buffer_from_handle(commandBuffer)->device->layer_dispatch.annotate.command(__VA_ARGS__)

% for c in commands:
% if c.guard is not None:
#ifdef ${c.guard}
% endif
VKAPI_ATTR ${c.return_type} VKAPI_CALL
annotate_${c.name}(${c.decl_params()})
{
   ANNOTATE(${c.name}, ${c.call_params()});
}

% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor
""")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-c", required=True, help="Output C file.")
    parser.add_argument("--beta", required=True, help="Enable beta extensions.")
    parser.add_argument("--xml",
                        help="Vulkan API XML file.",
                        required=True, action="append", dest="xml_files")
    args = parser.parse_args()

    commands = []
    commands_names = []
    for e in get_entrypoints_from_xml(args.xml_files, args.beta):
        if not e.name.startswith('Cmd') or e.alias or e.return_type != "void":
            continue

        stripped_name = e.name.removesuffix('EXT').removesuffix('KHR').removesuffix('2')
        if stripped_name in commands_names or stripped_name in EXCLUDED_COMMANDS:
            continue

        commands.append(e)
        commands_names.append(stripped_name)

    environment = {
        "filename": os.path.basename(__file__),
        "commands": commands,
    }

    try:
        with open(args.out_c, "w", encoding='utf-8') as f:
            f.write(TEMPLATE.render(**environment))
    except Exception:
        # In the event there"s an error, this uses some helpers from mako
        # to print a useful stack trace and prints it, then exits with
        # status 1, if python is run with debug; otherwise it just raises
        # the exception
        print(mako.exceptions.text_error_template().render(), file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
