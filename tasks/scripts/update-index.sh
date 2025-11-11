#!/bin/bash
# Update task index (tasks/INDEX.json) by editing in place
# Machine-readable JSON format for easy parsing and UI building

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TASKS_DIR="$PROJECT_ROOT/tasks"
INDEX_FILE="$TASKS_DIR/INDEX.json"

# Build open tasks array
open_array=$(jq -n '[]')
for task_file in $(find "$TASKS_DIR/open" -name "task-*.json" -type f | sort); do
    task=$(jq -c '{
        id: .id,
        title: .title,
        priority: .priority,
        status: .status,
        keywords: .tags,
        created: .created,
        updated: .updated
    }' "$task_file")
    open_array=$(echo "$open_array" | jq ". += [$task]")
done

# Build completed tasks array
completed_array=$(jq -n '[]')
for task_file in $(find "$TASKS_DIR/completed" -name "task-*.json" -type f | sort); do
    task=$(jq -c '{
        id: .id,
        title: .title,
        priority: .priority,
        status: .status,
        keywords: .tags,
        created: .created,
        updated: .updated,
        completed_date: (.verification.test_date // .updated)
    }' "$task_file")
    completed_array=$(echo "$completed_array" | jq ". += [$task]")
done

# Update or create index file
timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)
if [ -f "$INDEX_FILE" ]; then
    # Edit existing file
    jq --arg ts "$timestamp" \
       --argjson open "$open_array" \
       --argjson completed "$completed_array" \
       '.generated = $ts | .open_count = ($open | length) | .completed_count = ($completed | length) | .open = $open | .completed = $completed' \
       "$INDEX_FILE" > "$INDEX_FILE.tmp" && mv "$INDEX_FILE.tmp" "$INDEX_FILE"
else
    # Create new file
    jq -n --arg ts "$timestamp" \
          --argjson open "$open_array" \
          --argjson completed "$completed_array" \
          '{generated: $ts, open_count: ($open | length), completed_count: ($completed | length), open: $open, completed: $completed}' \
       > "$INDEX_FILE"
fi

echo "✓ Index updated: $INDEX_FILE"
