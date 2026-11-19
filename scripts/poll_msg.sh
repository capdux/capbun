#!/bin/bash
while true; do
    python3 /data/dev/bun/scripts/watch_task.py --journal /root/.agy/comm/cp-bun-capsurf.jsonl
    echo "[POLLER] Bun Poller exited, restarting in 5s..."
    sleep 5
done
