#!/bin/sh
# Start the Animal Crossing dedicated server on Linux.
# Reads server_config.ini in the current directory (or next to this script) and
# execs AnimalCrossing --host --dedicated. Does not download updates.
#
# Usage (from the directory that contains the binary, rom/ and this config):
#   ./start-server.sh
#   ./start-server.sh /path/to/AnimalCrossing
#
# The town is created under servers/default/ on first launch and loaded again
# on every later start. Ctrl+C or the console command "stop" saves and exits.
# A USA GAFE01 disc image must be in rom/ next to the binary. This script does
# not ship or download one.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
CONFIG=""
if [ -f "./server_config.ini" ]; then
    CONFIG="./server_config.ini"
elif [ -f "$SCRIPT_DIR/server_config.ini" ]; then
    CONFIG="$SCRIPT_DIR/server_config.ini"
else
    echo "start-server.sh: server_config.ini not found in . or $SCRIPT_DIR" >&2
    exit 1
fi

value() {
    key=$1
    default=$2
    line=$(grep -E "^[[:space:]]*${key}[[:space:]]*=" "$CONFIG" | tail -n 1 || true)
    if [ -z "$line" ]; then
        printf '%s' "$default"
        return
    fi
    printf '%s' "$line" | sed -E "s/^[[:space:]]*${key}[[:space:]]*=[[:space:]]*//" | tr -d '\r'
}

port=$(value port 7777)
ws_port=$(value ws_port 7778)
max_peers=$(value max_peers 8)
max_guests=$(value max_guests 4)
allow_new=$(value allow_new_guests 1)
tokens=$(value resident_tokens off)
town_serve=$(value town_serve auto)
personal=$(value personal_sync auto)

case "$port" in ''|*[!0-9]*) echo "port must be a number" >&2; exit 1 ;; esac
case "$ws_port" in ''|*[!0-9]*) echo "ws_port must be a number" >&2; exit 1 ;; esac
case "$max_peers" in ''|*[!0-9]*) echo "max_peers must be a number" >&2; exit 1 ;; esac
case "$max_guests" in ''|*[!0-9]*) echo "max_guests must be a number" >&2; exit 1 ;; esac
if [ "$max_guests" -gt "$max_peers" ]; then
    echo "max_guests ($max_guests) cannot exceed max_peers ($max_peers)" >&2
    exit 1
fi
case "$tokens" in off|tofu|required) ;; *) echo "resident_tokens must be off, tofu or required" >&2; exit 1 ;; esac
case "$allow_new" in 0|1) ;; *) echo "allow_new_guests must be 0 or 1" >&2; exit 1 ;; esac

if [ "$town_serve" = "auto" ]; then town_serve=on; fi
if [ "$personal" = "auto" ]; then
    if [ "$town_serve" = "off" ]; then personal=off; else personal=on; fi
fi

BIN=${1:-}
if [ -z "$BIN" ]; then
    if [ -x "./AnimalCrossing" ]; then
        BIN="./AnimalCrossing"
    elif [ -x "$SCRIPT_DIR/AnimalCrossing" ]; then
        BIN="$SCRIPT_DIR/AnimalCrossing"
    else
        echo "start-server.sh: pass the AnimalCrossing binary, or run this from its directory" >&2
        exit 1
    fi
fi

echo "UDP $port  WebSocket TCP $ws_port  peers $max_peers  guests $max_guests  tokens $tokens"
echo "Town files: ./servers/default/   (created on first launch, reloaded after that)"
exec "$BIN" --host "$port" --dedicated --ws-port "$ws_port" \
    --max-peers "$max_peers" --max-guests "$max_guests" \
    --allow-new-guests "$allow_new" --resident-tokens "$tokens" \
    --town-serve "$town_serve" --personal-sync "$personal"
