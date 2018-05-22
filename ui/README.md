# Perfetto UI

Quick Start
-----------
Run:

```
$ git clone https://android.googlesource.com/platform/external/perfetto/
$ cd perfetto
$ tools/install-build-deps --no-android --ui
$ tools/build_all_configs.py
```

Then on Linux:

```
$ tools/ninja -C out/linux_clang_debug ui
```

Or on MacOS:

```
$ ninja -C out/mac_debug ui
```

Finally run:

```
$ ./ui/run-dev-server
```

and navigate to `localhost:3000`.


