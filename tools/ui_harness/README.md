# UI harness

Look at the overlay without the game: a tiny Direct3D 11 host shows a still picture as the "game",
the harness build of the DLL (`/DSHERBET_UI_HARNESS=1`, never set in release builds) reads commands
from `uih_cmd.txt` next to the DLL, and screenshots land in `<run>\shots`.

    powershell -ExecutionPolicy Bypass -File tools\ui_harness\build.ps1 -Dll -Rebuild -Content
    powershell -ExecutionPolicy Bypass -File tools\ui_harness\drive.ps1 -Start -W 2560 -H 1440 -Cmds "wait 150;open;wait 40;tab 0;wait 30;shot home"
    python -I tools\ui_harness\crop.py %TEMP%\sherbet_uih_run out_dir
    powershell -ExecutionPolicy Bypass -File tools\ui_harness\drive.ps1 -Stop

Commands: `open` `close` `tab N` `mseg N` `sseg N` `scroll Y` `mouse X Y` `mouse off` `down` `up`
`wheel D` `theme ID` `shot NAME` `wait FRAMES` `trace FRAMES` (content scroll per frame -> uih_trace.txt).

The run folder is `%TEMP%\sherbet_uih_run`; `-Content` copies shaders, presets, config and the login
token from the installed game folder. The window is created off-screen and never takes focus.

After harness builds, rebuild the project once without the define (`/t:ReShade:Rebuild`) before any
release build - the object files still contain the hook otherwise.
