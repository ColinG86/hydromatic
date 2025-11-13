#!/usr/bin/env bash
# Activity logger for Claude crash investigation
# Usage: log-activity.sh "message"
# Returns: 👍 on success, 👎 on failure

LOG_FILE="/tmp/claude_activity_log.txt"

# Check if message provided
if [ -z "$1" ]; then
    echo "👎"
    exit 1
fi

# Attempt to log
if echo "[$(date -Iseconds)] $1" >> "$LOG_FILE" 2>/dev/null; then
    echo "👍"
    exit 0
else
    echo "👎"
    exit 1
fi
