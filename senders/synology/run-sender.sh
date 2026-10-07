#!/bin/sh
set -eu
sender_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
for python_candidate in /usr/local/bin/python3 /usr/bin/python3 /bin/python3; do
  if [ -x "$python_candidate" ]; then
    exec "$python_candidate" "$sender_root/synology_sync.py" "$@"
  fi
done
if command -v python3 >/dev/null 2>&1; then
  exec python3 "$sender_root/synology_sync.py" "$@"
fi
echo 'Python 3 is not available. Install Python3 from Synology Package Center if available for your DSM, then run this task again.' >&2
exit 1
