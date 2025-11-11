#!/bin/bash
# Claim a task (set status to in-progress and assign to agent)

if [ $# -lt 2 ]; then
    echo "Usage: claim-task.sh <task-id> <agent-name>"
    echo "Example: claim-task.sh task-001 claude"
    exit 1
fi

TASK_ID="$1"
AGENT_NAME="$2"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TASK_FILE="$PROJECT_ROOT/tasks/open/$TASK_ID.json"

if [ ! -f "$TASK_FILE" ]; then
    echo "Error: Task file not found: $TASK_FILE"
    exit 1
fi

# Update task file
timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)
temp_file=$(mktemp)

jq --arg status "in-progress" \
   --arg agent "$AGENT_NAME" \
   --arg updated "$timestamp" \
   '.status = $status | .assigned_to = $agent | .updated = $updated' \
   "$TASK_FILE" > "$temp_file"

mv "$temp_file" "$TASK_FILE"

echo "✓ Task claimed: $TASK_ID"
echo "  Assigned to: $AGENT_NAME"
echo "  Status: in-progress"
echo
echo "Next steps:"
echo "1. Read the task: cat $TASK_FILE | jq"
echo "2. Do the work (make changes, build, test)"
echo "3. Log changes: ./tasks/scripts/log-change.sh"
echo "4. When done: ./tasks/scripts/finish-task.sh $TASK_ID"
