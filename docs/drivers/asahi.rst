Asahi
=====

The Asahi driver aims to provide an OpenGL implementation for the Apple M1.

Testing on macOS
-----------------

On macOS, the experimental Asahi driver may built with options:

    -Dosmesa=true -Dglx=xlib -Dgallium-drivers=asahi,swrast

To use, set the ``DYLD_LIBRARY_PATH`` environment variable:

   DYLD_LIBRARY_PATH=/Users/nobody/mesa/build/src/gallium/targets/libgl-xlib/ glmark2 --reuse-context

Only X11 apps are supported. XQuartz must be setup separately.

Wrap (macOS only)
-----------------

Mesa includes a library that wraps the key IOKit entrypoints used in the macOS
UABI for AGX. The wrapped routines print information about the kernel calls made
and dump work submitted to the GPU using agxdecode.

This library allows debugging Mesa, particularly around the undocumented macOS
user-kernel interface. Logs from Mesa may compared to Metal to check that the
UABI is being used correcrly.

Furthermore, it allows reverse-engineering the hardware, as glue to get at the
"interesting" GPU memory.

The library is only built if ``-Dtools=asahi`` is passed. It builds a single
``wrap.dylib`` file, which should be inserted into a process with the
``DYLD_INSERT_LIBRARIES`` environment variable.

For example, to trace an app ``./app``, run:

    DYLD_INSERT_LIBRARIES=~/mesa/build/src/asahi/lib/libwrap.dylib ./app

Hardware varyings
-----------------

At an API level, vertex shader outputs need to be interpolated to become
fragment shader inputs. This process is logically pipelined in AGX, with a value
travelling from a vertex shader to remapping hardware to coefficient register
setup to the fragment shader to the iterator hardware. Each stage is described
below.

Vertex shader
`````````````

A vertex shader (running on the Unified Shader Cores) outputs varyings with the
``st_var`` instruction. ``st_var`` takes a *vertex output index* and a 32-bit
value. The maximum number of *vertex outputs* is specified as the "output count"
of the shader in the "Bind Vertex Pipeline" packet. The value may be interpreted
consist of a single 32-bit value or an aligned 16-bit register pair, depending
on whether interpolation should happen at 32-bit or 16-bit. Vertex outputs are
indexed starting from 0, with the *vertex position* always coming first, the
32-bit user varyings coming next, then 16-bit user varyings, and finally *point
size* at the end if present.

.. list-table:: Ordering of vertex outputs with all outputs used
   :widths: 25 75
   :header-rows: 1

   * - Index
     - Value
   * - 0
     - Vertex position
   * - 4
     - 32-bit varying 0
   * -
     - ...
   * - 4 + m
     - 32-bit varying m
   * - 4 + m + 1
     - Packed pair of 16-bit varyings 0
   * -
     - ...
   * - 4 + m + 1 + n
     - Packed pair of 16-bit varyings n
   * - 4 + m + 1 + n + 1
     - Point size

Remapping
`````````

Vertex outputs are remapped to varying slots to be interpolated.
The output of remapping consists of the following items: the *W* fragment
coordinate, the *Z* fragment coordinate, user varyings in the vertex
output order. *Z* may be omitted, but *W* may not be. This remapping is
configured by the "Linkage" packet.

.. list-table:: Ordering of remapped slots
   :widths: 25 75
   :header-rows: 1

   * - Index
     - Value
   * - 0
     - Fragment coord W
   * - 1
     - Fragment coord Z
   * - 2
     - 32-bit varying 0
   * -
     - ...
   * - 2 + m
     - 32-bit varying m
   * - 2 + m + 1
     - Packed pair of 16-bit varyings 0
   * -
     - ...
   * - 2 + m + n + 1
     - Packed pair of 16-bit varyings n

Coefficient registers
`````````````````````

The fragment shader does not see the physical slots.
Instead, it references varyings through *coefficient registers*. A coefficient
register is a register allocated constant for all fragment shader invocations in
a given polygon. Physically, it contains the values output by the vertex shader
for each vertex of the polygon. Coefficient registers are preloaded with values
from varying slots. This preloading appears to occur in fixed function hardware,
a simplifcation from PowerVR which requires a specialized program for the
programmable data sequencer to do the preload.

The "Bind fragment pipeline" packet points to coefficient register bindings,
preceded by a header. The header contains the number of 32-bit varying slots. As
the *W* slot is always present, this field is always nonzero. Slots whose index
is below this count are treated as 32-bit. The remaining slots are treated as
16-bits.

The header also contains the total number of coefficient registers bound.

Each binding that follows maps a (vector of) varying slots to a (consecutive)
coefficient registers. Some details about the varying (perspective
interpolation, flat shading, point sprites) are configured here.

Coefficient registers may be ordered the same as the internal varying slots.
However, this may be inconvenient for some APIs that require a separable shader
model. For these APIs, the flexibility to mix-and-match slots and coefficient
registers allows mixing shaders without shader variants. In that case, the
bindings should be generated outside of the compiler. For simple APIs where the
bindings are fixed and known at compile-time, the bindings could be generated
within the compiler.

Fragment shader
```````````````

In the fragment shader, coefficient registers, identified by the prefix `cf`
followed by a decimal index, act as opaque handles to varyings. For flat
shading, coefficient registers may be loaded into general registers with the
`ldcf` instruction. For smooth shading, the coefficient register corresponding
to the desired varying is passed as an argument to the "iterate" instruction
`iter` in order to "iterate" (interpolate) a varying. As perspective correct
interpolation also requires the W component of the fragment coordinate, the
coefficient register for W is passed as a second argument. As an example, if
there's a single varying to interpolate, an instruction like `iter r0, cf1, cf0`
is used.

Iterator
````````

To actually interpolate varyings, AGX provides fixed-function iteration hardware
to multiply the specified coefficient registers with the required barycentrics,
producing an interpolated value, hence the name "coefficient register". This
operation is purely mathematical and does not require any memory access, as
the required coefficients are preloaded before the shader begins execution.
That means the iterate instruction executes in constant time, does not signal
a data fence, and does not require the shader to wait on a data fence before
using the value.
