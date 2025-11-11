#!/bin/bash
# Search tasks and changes by keyword

if [ $# -lt 1 ]; then
    echo "Usage: search-tasks.sh <keyword>"
    echo "Example: search-tasks.sh parser"
    exit 1
fi

KEYWORD="$1"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TASKS_DIR="$PROJECT_ROOT/tasks"
CHANGES_FILE="$PROJECT_ROOT/changes.jsonl"

echo "=== Searching for: $KEYWORD ==="
echo

echo "--- Tasks ---"
grep -r "$KEYWORD" "$TASKS_DIR" --include="*.json" 2>/dev/null | jq -r '.id, .title' 2>/dev/null || echo "(no matches in tasks)"

echo
echo "--- Changes ---"
grep "$KEYWORD" "$CHANGES_FILE" 2>/dev/null | jq '.timestamp, .component, .summary' 2>/dev/null || echo "(no matches in changes)"
