#!/bin/bash
# tools/headless.sh - drives the installed game (with the mod) in a nested,
# headless KWin with its own Xwayland and D-Bus session bus, so nothing appears
# on the desktop and nothing of the user's is touched: saves, Unity's prefs,
# KWin's config and Player.log live in the run directory, and the game runs with
# -nosteam.
#
#   tools/headless.sh start [W H]     KWin + the game; returns once the mod's test
#                                     harness answers (see src/testkit.h)
#   tools/headless.sh do 'cmd' ...    one harness command per argument; waits for
#                                     them to finish
#   tools/headless.sh shot NAME       a screenshot, as $RUN/NAME.png
#   tools/headless.sh log             the mod's log
#   tools/headless.sh stop
#
# The saves start as a copy of the user's (~/.local/share/3909/PapersPlease),
# taken at `start` when the run has none yet; the mod's log and ini are the
# run's own (PPCC_TEST_DIR). RUN defaults to $XDG_RUNTIME_DIR/ppcc-headless.
#
# WIN=1 runs the Windows version instead (WIN_GAME_DIR in config.mk, with the
# Windows build installed there by make install) under Wine, in a prefix of the
# run's own ($RUN/pfx, made with no display at the first start; Wine's HOME and
# XDG folders are $RUN/home): the saves are copied into the prefix
# (%APPDATA%\3909\PapersPlease), RUN defaults to $XDG_RUNTIME_DIR/ppcc-headless-win,
# KWin's socket is ppcc-wayland-win, and stop ends that prefix's processes
# (wineserver -k) and nothing else. With DXVK=<folder of DXVK's x64 dxgi.dll
# and d3d11.dll> (Proton's: .../Proton - Experimental/files/lib/wine/dxvk/
# x86_64-windows) the game draws through DXVK, as under Proton, in a run of its
# own ($XDG_RUNTIME_DIR/ppcc-headless-win-dxvk, socket ppcc-wayland-win-dxvk).
# PPCC_GAME_DIR runs another copy of the game than config.mk's (one with a mod
# loader beside the mod: Wine takes MelonLoader's version.dll and BepInEx's
# winhttp.dll from the game's folder too). PPCC_TEST_LOADER=1 has the Windows
# build put a stand-in mod loader in UnityPlayer's GetProcAddress slot after its
# own DllMain, as MelonLoader or Doorstop would if theirs ran later.
set -e
cd "$(dirname "$0")/.."
if [ -n "$WIN" ]; then
  GAME_DIR=${PPCC_GAME_DIR:-$(sed -n 's/^WIN_GAME_DIR *:= *//p' config.mk)}
  RUN=${RUN:-${XDG_RUNTIME_DIR:-/tmp}/ppcc-headless-win${DXVK:+-dxvk}}
  SOCKET=ppcc-wayland-win${DXVK:+-dxvk}
else
  GAME_DIR=${PPCC_GAME_DIR:-$(sed -n 's/^GAME_DIR *:= *//p' config.mk)}
  RUN=${RUN:-${XDG_RUNTIME_DIR:-/tmp}/ppcc-headless}
  SOCKET=ppcc-wayland  # in $XDG_RUNTIME_DIR, beside (not inside) the run directory
fi

# Wine's environment for a Windows run: the run's prefix and home, no menu
# entries or browser (a steam:// link must not reach the desktop's Steam), and
# hid.dll from the game's folder - the mod - before Wine's own (and the mod
# loaders' when they are there: MelonLoader's version.dll, BepInEx's winhttp.dll).
# mscoree stays: .NET (a mod loader's) loads
# assemblies through it; only the prefix's creation goes without it, so that it
# installs no Wine Mono.
wine_env() {
  export WINEPREFIX="$RUN/pfx" HOME="$RUN/home" WINEDEBUG=-all \
    XDG_CONFIG_HOME="$RUN/home/.config" XDG_DATA_HOME="$RUN/home/.local/share" XDG_CACHE_HOME="$RUN/home/.cache" \
    WINEDLLOVERRIDES="mshtml=;winemenubuilder.exe=d;winebrowser.exe=d;hid,version,winhttp=n,b${DXVK:+;dxgi,d3d11=n}"
  # DXVK would write its logs and shader cache beside the exe, in the game's folder.
  if [ -n "$DXVK" ]; then export DXVK_LOG_LEVEL=none DXVK_STATE_CACHE=0; fi
}

wait_ack() {
  local want=$1 i
  for i in $(seq 1 ${2:-600}); do
    if [ -f "$RUN/ack" ] && [ "$(cat "$RUN/ack" 2>/dev/null)" = "$want" ]; then return 0; fi
    sleep 0.1
  done
  echo "headless: no answer from the harness (want ack $want)" >&2
  return 1
}

next_seq() {
  local cur=0
  [ -f "$RUN/seq" ] && cur=$(cat "$RUN/seq")
  echo $((cur + 1)) > "$RUN/seq"
  echo $((cur + 1))
}

case "$1" in
  start)
    W=${2:-1280}; H=${3:-720}
    rm -f "$RUN/ack" "$RUN/cmd" "$RUN/seq"
    if [ -n "$WIN" ]; then
      USER_SAVES="$HOME/.local/share/3909/PapersPlease"
      mkdir -p "$RUN/home"
      (
        wine_env
        if [ ! -f "$WINEPREFIX/system.reg" ]; then
          env -u DISPLAY -u WAYLAND_DISPLAY WINEDLLOVERRIDES="mscoree=;$WINEDLLOVERRIDES" wineboot -i > "$RUN/wineboot.log" 2>&1
          env -u DISPLAY -u WAYLAND_DISPLAY wineserver -w
        fi
        if [ -n "$DXVK" ]; then cp -f "$DXVK/dxgi.dll" "$DXVK/d3d11.dll" "$WINEPREFIX/drive_c/windows/system32/"; fi
        # The Windows version keeps its saves in %APPDATA%\3909\PapersPlease.
        SAVES="$WINEPREFIX/drive_c/users/$(id -un)/AppData/Roaming/3909/PapersPlease"
        mkdir -p "$SAVES"
        if [ -z "$(ls -A "$SAVES")" ] && [ -d "$USER_SAVES" ]; then cp -a "$USER_SAVES/." "$SAVES/"; fi
      )
      WRUN="Z:${RUN//\//\\}"  # the run directory as Wine names it
      cat > "$RUN/game.sh" <<GAME
#!/bin/bash
$(declare -f wine_env)
RUN="$RUN" DXVK="$DXVK"
wine_env
cd "$GAME_DIR"
export PPCC_TEST_DIR='$WRUN'${PPCC_TEST_LOADER:+ PPCC_TEST_LOADER=1}
echo \$\$ > "$RUN/game.pid"   # exec keeps the pid: Wine runs the game in it
exec wine PapersPlease.exe -logFile '$WRUN\\Player.log' \
  -screen-fullscreen 0 -screen-width $W -screen-height $H > "$RUN/game.out" 2>&1
GAME
    else
      # The game keeps its saves in $XDG_DATA_HOME/3909/PapersPlease.
      SAVES="$RUN/data/3909/PapersPlease"
      mkdir -p "$RUN/config" "$SAVES"
      if [ -z "$(ls -A "$SAVES")" ] && [ -d "$HOME/.local/share/3909/PapersPlease" ]; then
        cp -a "$HOME/.local/share/3909/PapersPlease/." "$SAVES/"
      fi
      cat > "$RUN/game.sh" <<GAME
#!/bin/bash
cd "$GAME_DIR"
export PPCC_TEST_DIR="$RUN" XDG_CONFIG_HOME="$RUN/config" XDG_DATA_HOME="$RUN/data"
echo \$\$ > "$RUN/game.pid"   # exec keeps the pid: the game's own
exec ./PapersPlease -logFile "$RUN/Player.log" \
  -screen-fullscreen 0 -screen-width $W -screen-height $H > "$RUN/game.out" 2>&1
GAME
    fi
    chmod +x "$RUN/game.sh"
    # KWin gets a session bus and a config of its own. On the user's bus it was a
    # second "kwin" in their kglobalaccel, and its exit switched off every KWin
    # shortcut on their desktop (Alt+Tab, Alt+F4, Meta+D...) for the rest of the
    # session; with their config it wrote its output into kwinoutputconfig.json.
    # The bus has no service files: nothing is D-Bus-activated beside KWin.
    cat > "$RUN/bus.conf" <<'BUS'
<busconfig>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
BUS
    mkdir -p "$RUN/kwin-config"
    # The sh writes KWin's own pid and execs it: stop kills KWin, and
    # dbus-run-session then takes the bus down.
    XDG_CONFIG_HOME="$RUN/kwin-config" setsid dbus-run-session --config-file="$RUN/bus.conf" -- \
      sh -c 'echo $$ > "$0"; exec "$@"' "$RUN/kwin.pid" \
      kwin_wayland --virtual --xwayland --socket $SOCKET --width $W --height $H -- "$RUN/game.sh" \
      > "$RUN/kwin.log" 2>&1 < /dev/null &
    echo 1 > "$RUN/seq"
    printf '#seq 1\nwait 1\n' > "$RUN/cmd"
    wait_ack 1 900 && echo "headless: game up (run dir $RUN)"
    ;;
  do)
    shift
    seq=$(next_seq)
    { echo "#seq $seq"; printf '%s\n' "$@"; } > "$RUN/cmd.$seq" && mv "$RUN/cmd.$seq" "$RUN/cmd"
    wait_ack "$seq" ${WAIT:-1200}
    ;;
  shot)
    "$0" do "shot $2"
    magick "$RUN/$2.ppm" "$RUN/$2.png" && rm -f "$RUN/$2.ppm" && echo "$RUN/$2.png"
    ;;
  log)
    cat "$RUN/PPCabbyCodes.log"
    ;;
  stop)
    # Only the game this script started: the player's own copy may be running
    # (it is the same executable), so nothing is killed by name. A Windows run's
    # game is its prefix's: wineserver -k ends that prefix's processes alone.
    if [ -n "$WIN" ] && [ -d "$RUN/pfx" ]; then
      (wine_env; wineserver -k 2>/dev/null || true; wineserver -w 2>/dev/null || true)
      rm -f "$RUN/game.pid"
    elif [ -f "$RUN/game.pid" ]; then
      pid=$(cat "$RUN/game.pid")
      if [ -r "/proc/$pid/environ" ] && tr '\0' '\n' < "/proc/$pid/environ" | grep -qx "PPCC_TEST_DIR=$RUN"; then
        kill "$pid" 2>/dev/null || true
        # Unity may sit on a SIGTERM once its display is gone: then it is killed.
        for i in $(seq 1 30); do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
        kill -9 "$pid" 2>/dev/null || true
      fi
      rm -f "$RUN/game.pid"
    fi
    if [ -f "$RUN/kwin.pid" ]; then
      kpid=$(cat "$RUN/kwin.pid")
      kill "$kpid" 2>/dev/null || true
      # The next start needs its socket and lock free: wait for it to be gone.
      for i in $(seq 1 50); do kill -0 "$kpid" 2>/dev/null || break; sleep 0.1; done
      kill -9 "$kpid" 2>/dev/null || true
      rm -f "$RUN/kwin.pid"
    fi
    ;;
  *)
    sed -n '2,34p' "$0"
    exit 1
    ;;
esac
