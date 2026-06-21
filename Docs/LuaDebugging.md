# Lua debugging

ITGmania includes a Lua debugger server. Use it to set breakpoints and
step through theme code and Lua mods in songs.

Pass `--lua-debugger` to the ITGmania executable on the command line to
enable the server and then connect from your IDE. Optionally, set a host
and a port to listen on, for example `--lua-debugger=0.0.0.0:8173`. The
default is `localhost:8173`.

To debug Lua code that runs early, such as the Scripts directory or the
title screen, pass the `--lua-debugger-paused` command line option in
addition to `--lua-debugger`. This causes the debugger to pause at the
first Lua execution so that you have time to connect and set
breakpoints.

Make sure to open the Lua file in your IDE from the same path where
ITGmania loads it! Otherwise, breakpoints will not be set correctly.
Symlinks or drive mappings in the file path can cause problems.

## Client configuration

The server implements the
[Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/),
so any IDE that implements it should work as a frontend. See
configuration examples for specific clients below.

### Neovim

1. Install the [nvim-dap](https://github.com/mfussenegger/nvim-dap) plugin.
2. Add the following to your Neovim configuration:
```lua
require('dap').adapters.itgmania_lua = {
    type = 'server',
    host = '127.0.0.1',
    port = 8173,
}

require('dap').configurations.lua = {
    {
        type = 'itgmania_lua',
        request = 'attach',
        name = 'Debug ITGmania Lua code',
    }
}
```
3. Launch ITGmania with the `--lua-debugger` parameter.
4. Use the `:DapContinue` command to connect.

### VSCode

1. Install the
   [vscode-lua-debugger-for-itgmania](https://github.com/aryla/vscode-lua-debugger-for-itgmania)
   extension.
2. Add the following to `.vscode/launch.json` (or choose "Debug: Add
   configuration..." &rarr; "ITGmania Lua: Attach" from the command
   palette):
```jsonc
{
  "version": "0.2.0",
  "configurations": [
    // ...
    {
      "name": "ITGmania Lua: Attach",
      "type": "itgmania-lua",
      "request": "attach",
      "host": "localhost",
      "port": 8173
    }
  ]
}
```
3. Launch ITGmania with the `--lua-debugger` parameter.
4. Pick "Debug: Select and Start Debugging" &rarr; "ITGmania Lua:
   Attach" from the command palette to connect.

### Zed

1. Install the
   [zed-lua-debugger-for-itgmania](https://github.com/aryla/zed-lua-debugger-for-itgmania)
   extension.
2. Add the following to `.zed/debug.json`:
```jsonc
[
  // ...
  {
    "label": "ITGmania Lua: Attach",
    "adapter": "itgmania-lua"
  }
]
```
3. Launch ITGmania with the `--lua-debugger` parameter.
4. Pick "debugger: start" &rarr; "ITGmania Lua: Attach" from the command
   palette to connect.

## Tips

- The debugger will silently skip over breakpoints at locations like
  empty lines, comment lines, and multi-line statements, so try to avoid
  those.
- Use a conditional breakpoint to pause only when a Lua expression
  evaluates to `true` (or anything else than `false` or `nil`).
- Use an always-false breakpoint condition to execute some code without
  pausing. For example, `SM('Hello, World!'); return false`.
- Set a log message for a breakpoint to print something without pausing.
  Include variables and Lua expressions in the message by wrapping them
  in `{}`.
