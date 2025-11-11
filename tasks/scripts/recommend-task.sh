#!/bin/bash
# Recommend the best task for an agent to work on
# Analyzes priority, dependencies, and blockers

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TASKS_DIR="$PROJECT_ROOT/tasks/open"

if [ ! -d "$TASKS_DIR" ]; then
    echo "Error: $TASKS_DIR not found"
    exit 1
fi

# Count tasks
task_count=$(ls "$TASKS_DIR"/task-*.json 2>/dev/null | wc -l)

if [ "$task_count" -eq 0 ]; then
    echo "No open tasks. Create one with: ./tasks/scripts/new-task.sh"
    exit 0
fi

# Find best task: critical > high-urgent > high > medium > low > backlog
# Within same priority: no blockers > no dependencies
for priority in "critical" "high-urgent" "high" "medium" "low" "backlog"; do
    for task_file in "$TASKS_DIR"/task-*.json; do
        [ -f "$task_file" ] || continue

        task_priority=$(jq -r '.priority // "medium"' "$task_file" 2>/dev/null)
        task_status=$(jq -r '.status // "open"' "$task_file" 2>/dev/null)
        task_blockers=$(jq '.blockers | length' "$task_file" 2>/dev/null)

        # Skip if not the priority we're looking for or if already in progress
        if [ "$task_priority" != "$priority" ] || [ "$task_status" != "open" ]; then
            continue
        fi

        # Skip if it has blockers
        if [ "$task_blockers" -gt 0 ]; then
            continue
        fi

        # Found a good candidate
        task_id=$(jq -r '.id' "$task_file")
        echo "=== RECOMMENDED TASK ==="
        echo
        jq '.' "$task_file"
        echo
        echo "=== NEXT STEP ==="
        echo "claim-task.sh $task_id YOUR-AGENT-NAME"
        exit 0
    done
done

# If we get here, all open tasks have blockers or issues
echo "⚠️  All open tasks are either blocked or in-progress."
echo
echo "Open tasks:"
for task_file in "$TASKS_DIR"/task-*.json; do
    [ -f "$task_file" ] || continue
    task_id=$(jq -r '.id' "$task_file")
    task_title=$(jq -r '.title' "$task_file")
    task_status=$(jq -r '.status' "$task_file")
    task_blockers=$(jq '.blockers | length' "$task_file")

    if [ "$task_blockers" -gt 0 ]; then
        echo "$task_id ($task_status, $task_blockers blockers): $task_title"
    else
        echo "$task_id ($task_status): $task_title"
    fi
done
