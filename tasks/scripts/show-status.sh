#!/bin/bash
# Show task summary and status

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TASKS_ROOT="$PROJECT_ROOT/tasks"

echo "=== Hydromatic Task Status ==="
echo

# Count tasks by status
open_count=$(ls "$TASKS_ROOT/open"/*.json 2>/dev/null | wc -l)
in_progress_count=$(grep -l '"status": "in-progress"' "$TASKS_ROOT/open"/*.json 2>/dev/null | wc -l)
completed_count=$(ls "$TASKS_ROOT/completed"/*.json 2>/dev/null | wc -l)
deferred_count=$(ls "$TASKS_ROOT/deferred"/*.json 2>/dev/null | wc -l)

echo "Summary:"
echo "  Open: $open_count"
echo "  In Progress: $in_progress_count"
echo "  Completed: $completed_count"
echo "  Deferred: $deferred_count"
echo

# Show open tasks by priority
echo "Open Tasks by Priority:"
for priority in "critical" "high-urgent" "high" "medium" "low" "backlog"; do
    tasks=$(grep -l "\"priority\": \"$priority\"" "$TASKS_ROOT/open"/*.json 2>/dev/null)
    count=$(echo "$tasks" | grep -c . || echo 0)
    if [ "$count" -gt 0 ]; then
        echo "  [$priority] - $count task(s)"
        for task in $tasks; do
            task_id=$(basename "$task" .json)
            task_title=$(jq -r '.title' "$task" 2>/dev/null)
            task_assigned=$(jq -r '.assigned_to // "unassigned"' "$task" 2>/dev/null)
            task_blockers=$(jq '.blockers | length' "$task" 2>/dev/null)

            if [ "$task_blockers" -gt 0 ]; then
                echo "    ⚠️  $task_id: $task_title (assigned: $task_assigned, $task_blockers blocker(s))"
            else
                echo "    ✓ $task_id: $task_title (assigned: $task_assigned)"
            fi
        done
    fi
done

echo
echo "Recent Changes:"
tail -3 "$PROJECT_ROOT/changes.jsonl" 2>/dev/null | jq '.timestamp, .component, .summary' 2>/dev/null || echo "(no changes logged yet)"

echo
echo "Recommended: ./tasks/scripts/recommend-task.sh"
