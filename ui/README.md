# Perfetto UI

Quick Start
-----------
Run:

```
$ git clone https://android.googlesource.com/platform/external/perfetto/
$ cd perfetto
$ tools/install-build-deps --no-android --ui
$ tools/gn gen out/debug --args='is_debug=true is_clang=true'
$ tools/ninja -C out/debug ui
```

For more details on `gn` configs see
[Build Instructions](../docs/build-instructions.md).

To run the tests:
```
ui/node ui/node_modules/jest/bin/jest.js --projects=ui/jest.unit.config.js
--projects=ui/jest.jsdom.config.js --roots=../out/l/obj/ui
```

To run the tests in watch mode:
```
ui/node ui/node_modules/jest/bin/jest.js --projects=ui/jest.unit.config.js
--projects=ui/jest.jsdom.config.js --roots=../out/l/obj/ui --watch
```

Finally run:

```
$ ./ui/run-dev-server out/debug
```

and navigate to `localhost:3000`.
