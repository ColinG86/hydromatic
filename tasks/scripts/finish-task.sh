#!/bin/bash
# Mark a task as completed and move it to tasks/completed/
# Usage: finish-task.sh <task-id> <test-summary> [--skip-criteria-check]

if [ $# -lt 2 ]; then
    echo "Usage: finish-task.sh <task-id> <test-summary> [--skip-criteria-check]"
    echo "Example: finish-task.sh task-001 \"Tested feature on device, all tests pass\""
    echo ""
    echo "Options:"
    echo "  --skip-criteria-check  Skip validation that all criteria are defined"
    exit 1
fi

TASK_ID="$1"
TEST_SUMMARY="$2"
SKIP_CHECK=0

# Parse options
shift 2
while [ $# -gt 0 ]; do
    case "$1" in
        --skip-criteria-check)
            SKIP_CHECK=1
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TASK_FILE="$PROJECT_ROOT/tasks/open/$TASK_ID.json"
COMPLETED_FILE="$PROJECT_ROOT/tasks/completed/$TASK_ID.json"

if [ ! -f "$TASK_FILE" ]; then
    echo "Error: Task file not found: $TASK_FILE"
    exit 1
fi

# Check if acceptance criteria are documented
if [ "$SKIP_CHECK" -eq 0 ]; then
    criteria_count=$(jq '.acceptance_criteria | length' "$TASK_FILE")
    if [ "$criteria_count" -eq 0 ]; then
        echo "⚠️  Warning: Task has no acceptance criteria defined"
        echo "  These should have been defined before starting"
        echo "  Use --skip-criteria-check to bypass this warning"
        exit 1
    fi
fi

# Update task file with completed status and verification
timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)
temp_file=$(mktemp)

jq --arg status "completed" \
   --arg updated "$timestamp" \
   --arg test_summary "$TEST_SUMMARY" \
   '.status = $status | .updated = $updated | .verification = {tested: true, test_date: $updated, test_results: $test_summary, status: "success"}' \
   "$TASK_FILE" > "$temp_file"

# Move to completed directory
mv "$temp_file" "$COMPLETED_FILE"
rm -f "$TASK_FILE"

echo "✓ Task completed: $TASK_ID"
echo "  Status: completed"
echo "  Moved to: tasks/completed/$TASK_ID.json"

# Update the task index
"$PROJECT_ROOT/tasks/scripts/update-index.sh" > /dev/null 2>&1
