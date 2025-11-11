#!/bin/bash
# Log a change to changes.jsonl
# Usage: log-change.sh --type <type> --component <component> --summary <summary> [--files "file1 file2"] [--tags "tag1 tag2"] [--tested y/n] [--task <task-id>] [--agent <agent-name>]

if [ $# -lt 6 ]; then
    echo "Usage: log-change.sh --type <type> --component <component> --summary <summary> [--files \"file1 file2\"] [--tags \"tag1 tag2\"] [--tested y/n] [--task <task-id>] [--agent <agent-name>]"
    echo ""
    echo "Required:"
    echo "  --type        fix|feature|refactor|doc|test|chore"
    echo "  --component   What changed (e.g., 'parser', 'auth', 'UI')"
    echo "  --summary     One-line description of the change"
    echo ""
    echo "Optional:"
    echo "  --files       Space-separated absolute paths of modified files"
    echo "  --tags        Space-separated tags (e.g., 'bug parser null-safety')"
    echo "  --tested      'y' or 'n' (default: n)"
    echo "  --task        Task ID if applicable (e.g., task-001)"
    echo "  --agent       Agent name (default: claude, overrides AGENT_NAME env var)"
    echo ""
    echo "Example:"
    echo "  log-change.sh --type feature --component parser --summary 'Add null safety checks' --files '/path/file1.js /path/file2.js' --tags 'parser null-safety' --tested y --task task-001"
    exit 1
fi

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHANGES_FILE="$PROJECT_ROOT/changes.jsonl"

# Ensure changes.jsonl exists
touch "$CHANGES_FILE"

# Parse arguments
type=""
component=""
summary=""
files_input=""
tags_input=""
tested_input="n"
task_id=""
agent="${AGENT_NAME:-claude}"

while [[ $# -gt 0 ]]; do
    case $1 in
        --type)
            type="$2"
            shift 2
            ;;
        --component)
            component="$2"
            shift 2
            ;;
        --summary)
            summary="$2"
            shift 2
            ;;
        --files)
            files_input="$2"
            shift 2
            ;;
        --tags)
            tags_input="$2"
            shift 2
            ;;
        --tested)
            tested_input="$2"
            shift 2
            ;;
        --task)
            task_id="$2"
            shift 2
            ;;
        --agent)
            agent="$2"
            shift 2
            ;;
        --help)
            echo "Usage: log-change.sh --type <type> --component <component> --summary <summary> [--files \"file1 file2\"] [--tags \"tag1 tag2\"] [--tested y/n] [--task <task-id>] [--agent <agent-name>]"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Validate required fields
if [ -z "$type" ] || [ -z "$component" ] || [ -z "$summary" ]; then
    echo "Error: --type, --component, and --summary are required"
    exit 1
fi

# Validate type
case "$type" in
    fix|feature|refactor|doc|test|chore) ;;
    *)
        echo "Error: Invalid type '$type'. Must be one of: fix, feature, refactor, doc, test, chore"
        exit 1
        ;;
esac

# Set defaults
tested=$([ "$tested_input" = "y" ] && echo "true" || echo "false")

# Convert space-separated strings to JSON arrays
files_json=$(echo "$files_input" | jq -R 'split(" ") | map(select(length > 0))')
tags_json=$(echo "$tags_input" | jq -R 'split(" ") | map(select(length > 0))')

# Create JSON object
timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)
json=$(jq -n \
  --arg ts "$timestamp" \
  --arg type "$type" \
  --arg comp "$component" \
  --arg summ "$summary" \
  --argjson files "$files_json" \
  --arg agent "$agent" \
  --argjson tags "$tags_json" \
  --arg tested "$tested" \
  --arg task "$task_id" \
  '{
    timestamp: $ts,
    type: $type,
    component: $comp,
    summary: $summ,
    files: $files,
    agent: $agent,
    tags: $tags,
    tested: ($tested == "true"),
    status: "completed"
  } + if $task != "" then {task: $task} else {} end')

# Append to changes.jsonl
echo "$json" >> "$CHANGES_FILE"

echo
echo "✓ Change logged to $CHANGES_FILE"
echo
echo "Summary:"
echo "  Type: $type"
echo "  Component: $component"
echo "  Summary: $summary"
echo "  Files: $(echo "$files_input" | wc -w) files"
echo "  Tested: $tested_input"
