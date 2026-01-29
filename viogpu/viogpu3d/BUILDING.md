# Building viogpu3d driver

This repository contains kernel-mode part of viogpu3d driver, and full-driver will be build only if there is user-mode driver dll's available in directory pointed by environment variable `MESA_PATH`.

## Build mesa
Now inside virtual machine with build tools installed create working directory, then inside it (this assumes use of Powershell):
1. Create mesa prefix dir `mkdir mesa_prefix`  and set env `MESA_PREFIX` to its path: `$env:MESA_PREFIX="$PWD\mesa_prefix"`
2. Acquire [mesa source code](https://gitlab.freedesktop.org/mesa/mesa) and then cd into it `cd mesa`
3. Create build directory `mkdir build && cd build`
4. Configure build `meson .. --prefix=$env:MESA_PREFIX  -Dgallium-drivers=virgl -Dgallium-d3d10umd=true -Dgallium-wgl-dll-name=viogpu_wgl -Dgallium-d3d10-dll-name=viogpu_d3d10 -Db_vscrt=mt`, build options explained:
  * `--prefix=$env:MESA_PREFIX` set installation path to dir created in step 1
  * `-Dgallium-drivers=virgl` build only virgl driver
  * `-Dgallium-d3d10umd=true` build DirectX 10 user-mode driver (opengl one is build by default)
  * `-Dgallium-d3d10-dll-name=viogpu_d3d10` name of generated d3d10 dll to `viogpu_d3d10.dll`
  * `-Dgallium-wgl-dll-name=viogpu_wgl` name of generated wgl dll to `viogpu_wgl.dll`
  * `-Db_vscrt=mt` use static c runtime
5. Build and install (to mesa prefix): `ninja install`

## Build driver
Now that mesa is build and installed into `%MESA_PREFIX%` viogpu3d will be built (in case `%MESA_PREFIX` is not set viogpu3d inf generation is skipped)
1. Acquire [drivers source code](https://github.com/virtio-win/kvm-guest-drivers-windows) and cd into it `cd kvm-guest-drivers-windows`
2. Go to viogpu `cd viogpu`
3. (optional, but very useful) setup test code signning from visual studio
4. Call build `.\build_AllNoSdv.bat`

Built driver will be available at `kvm-guest-drivers-windows\viogpu\viogpu3d\objfre_win10_amd64\amd64\viogpu3d`.
