`Mesa <https://mesa3d.org>`_ - The 3D Graphics Library
======================================================

======================================================

Tips
---------------

How to improve stability with Turnip + Zink in Exagear? 

If your game in Exagear freezes or crashes with errors **"GL_OUT_OF_MEMORY"**, **"ZINK: vkMapMemory failed"** etc, try these solutions.

- Use the most stable version of **Mesa Zink from 11.06.22** (https://github.com/alexvorxx/mesa-zink-11.06.22).

- Use environment variables **ZINK_DESCRIPTORS=lazy** and **ZINK_DEBUG=compact**. Zink has a problem with descriptor sets, these commands partially fix it. **ZINK_DEBUG=compact** forces to use a maximum of 4 descriptor sets. **ZINK_DESCRIPTORS=lazy** disables caching and attempt to use the least amount of CPU. It improves performance and stability.

- For some Direct3D games open **winecfg** and change version of Windows to Windows 98. Then open **regedit**, go to the **[HKEY_CURRENT_USER/Software/Wine/Direct3D]** and add parameter **"strict_shader_math"=dword:00000003**. Turnip has problems with rendering of some textures or compiling shaders, but Windows 98 skips some textures and shaders. Thanks for this method to GIORGI (aka Alien), OnlyFuns and Luis Gaming Test.

- For Direct3D games use **WineD3D 7.2** - the most stable version.

- For some Direct3D games open **regedit**, go to the **[HKEY_CURRENT_USER/Software/Wine/Direct3D]** and add parameter **"csmt"=dword:00000000**. A value **0** may improve stability but decrease performance. A values **1** or **3** may decrease stability but improve performance.

https://www.phoronix.com/review/zink-sub-alloc/6

https://docs.mesa3d.org/drivers/zink.html

https://wiki.winehq.org/Useful_Registry_Keys

https://4pda.to/forum/index.php?showtopic=992239&view=findpost&p=119392856

Build deps
---------------

It's recommended to use Ubuntu 18.04.

  $ sudo apt update
  
  $ sudo apt install -y software-properties-common dirmngr apt-transport-https wget git unzip libxcb-shm0 mesa-utils python3-pip software-properties-common dirmngr apt-transport-https python3-mako libxcb-shm0-dev libpciaccess-dev make build-essential libssl-dev zlib1g-dev libbz2-dev libreadline-dev libsqlite3-dev wget curl llvm libncurses5-dev libncursesw5-dev xz-utils tk-dev libxml2-dev graphviz doxygen xsltproc xmlto xutils-dev libxkbcommon-dev libvulkan1 mesa-vulkan-drivers vulkan-utils 
  
  $ sudo apt-get build-dep mesa -y

  $ pip3 install meson ninja

  $ pip3 install dataclasses
  
It's recommended to use meson-0.61.5 or newer.

It's recommended to use GCC-11 and G++-11 or newer.
  
After that, you need to build from source and install the following libraries.

- https://gitlab.freedesktop.org/glvnd/libglvnd

- https://dri.freedesktop.org/libdrm

- https://gitlab.freedesktop.org/wayland/wayland

- https://gitlab.freedesktop.org/wayland/wayland-protocols

For details, see README of the respective repositories.

Build Turnip
---------------

Build Mesa Turnip using Mesa repository (https://gitlab.freedesktop.org/mesa/mesa).

Go to the folder with Mesa code and run the commands:

  $ meson build -D platforms=x11,wayland -D gallium-drivers=swrast,virgl,zink,freedreno -D vulkan-drivers=freedreno -D dri3=enabled -D egl=enabled -D gles2=enabled -D glvnd=true -D glx=dri -D libunwind=disabled -D osmesa=true -D shared-glapi=enabled -D microsoft-clc=disabled -D valgrind=disabled --prefix /usr -D gles1=disabled -D freedreno-kmds=kgsl -Dbuildtype=release
  
  $ ninja -C build

Build Mesa Zink
---------------

Build Mesa Zink using this repository (https://github.com/alexvorxx/Zink-Mesa-Xlib).

Go to the folder with Mesa code and run the commands:

  $ meson . build -Dgallium-va=disabled -Dgallium-drivers=virgl,zink,swrast -Ddri3=disabled -Dvulkan-drivers= -Dglx=xlib -Dplatforms=x11 -Dbuildtype=release
  
  $ ninja -C build
  
======================================================

Source
------

This repository lives at https://gitlab.freedesktop.org/mesa/mesa.
Other repositories are likely forks, and code found there is not supported.  

Support
-------

Many Mesa devs hang on IRC; if you're not sure which channel is
appropriate, you should ask your question on `OFTC's #dri-devel
<irc://irc.oftc.net/dri-devel>`_, someone will redirect you if
necessary.
Remember that not everyone is in the same timezone as you, so it might
take a while before someone qualified sees your question.
To figure out who you're talking to, or which nick to ping for your
question, check out `Who's Who on IRC
<https://dri.freedesktop.org/wiki/WhosWho/>`_.

The next best option is to ask your question in an email to the
mailing lists: `mesa-dev\@lists.freedesktop.org
<https://lists.freedesktop.org/mailman/listinfo/mesa-dev>`_


Bug reports
-----------

If you think something isn't working properly, please file a bug report
(`docs/bugs.rst <https://mesa3d.org/bugs.html>`_).


Contributing
------------

Contributions are welcome, and step-by-step instructions can be found in our
documentation (`docs/submittingpatches.rst
<https://mesa3d.org/submittingpatches.html>`_).

Note that Mesa uses gitlab for patches submission, review and discussions.
