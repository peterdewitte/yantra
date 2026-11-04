#!/bin/sh
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# with_remote_server.sh DIR SERVER COMMAND...: starts the loopback
# SoapySDRServer SERVER (URL file in DIR), runs COMMAND with
# YANTRA_SOAPY_REMOTE set to its URL, stops the server and exits with
# COMMAND's status.
set -u
dir=$1
server=$2
shift 2
mkdir -p "$dir"
rm -f "$dir/url"
"$server" "$dir/url" &
pid=$!
i=0
while [ ! -s "$dir/url" ]; do
    if ! kill -0 "$pid" 2>/dev/null || [ $i -ge 600 ]; then
        echo "with_remote_server.sh: the server did not start" >&2
        kill "$pid" 2>/dev/null
        exit 1
    fi
    i=$((i + 1))
    sleep 0.1
done
YANTRA_SOAPY_REMOTE=$(cat "$dir/url")
export YANTRA_SOAPY_REMOTE
"$@"
status=$?
kill "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
rm -f "$dir/url"
exit $status
