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
that was shipped with the application. Refer [to the ini file](https://github.com/MonkeyModdingTroop/hookfxr/blob/master/hookfxr/hookfxr.ini) for more options.

## Limitations
- Only supports .NET Core global framework-dependent deployments. Self-contained deployments are currently not supported.
- Currently only supports Windows. On other platforms, just run your code directly.
- Requires a custom version of libnethost [built from this branch of runtime](https://github.com/dotnet/runtime/compare/v9.0.6...MonkeyModdingTroop:runtime:v9.0.6-hookfxr) that exposes additional functionality and is built against a static CRT.[hookfxr.ini](hookfxr/hookfxr.ini)