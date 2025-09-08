# hookfxr
Reroutes entrypoint DLLs for CoreCLR apphosts by proxying through hostfxr.dll,
allowing .NET Core applications to be modified without overwriting any files.

## Usage
Place `hostfxr.dll` and the accompanying `hookfxr.ini` into the same directory as your .NET Core application. In the ini
file, specify the DLL you want to reroute the entrypoint to, for example:

```ini
[hookfxr]
target_assembly=MyApp.dll
```

When you run your application, `hostfxr.dll` will intercept the entrypoint and redirect it to `MyApp.dll` instead of the original
that was shipped with the application. Refer [to the ini file](hookfxr/hookfxr.ini) for more options.

## Other features
* .deps.json of target assembly can be merged into the one of the origin assembly, by setting `merge_deps_json=true` in `hookfxr.ini`. This allows the runtime to resolve native assemblies of the origin assembly and the target assembly.
* The path of the origin assembly is now written into the `HOOKFXR_ORIGINAL_APP_PATH` environment variable before the runtime is loaded. Your loader can access this variable to know what target assembly it needs to load.

## Limitations
- Only supports .NET Core global framework-dependent deployments. Self-contained deployments are currently not supported.
- Currently only supports Windows. On other platforms, just run your code directly.
- Requires a custom version of libnethost [built from this branch of runtime](https://github.com/dotnet/runtime/compare/v9.0.6...MonkeyModdingTroop:runtime:v9.0.6-hookfxr) that exposes additional functionality and is built against a static CRT.
