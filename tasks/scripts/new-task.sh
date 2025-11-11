#!/bin/bash
# Create a new task file
# Usage: ./new-task.sh --title "Title" --priority "high" --description "Desc" [--criteria "Criterion 1" --criteria "Criterion 2"] [--tags "tag1 tag2"]

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TASKS_DIR="$PROJECT_ROOT/tasks/open"

# Parse arguments
title=""
priority=""
description=""
criteria_list=()
tags_input=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --title)
            title="$2"
            shift 2
            ;;
        --priority)
            priority="$2"
            # Validate priority
            case "$priority" in
                critical|high-urgent|high|medium|low|backlog) ;;
                *)
                    echo "Error: Invalid priority. Must be one of: critical, high-urgent, high, medium, low, backlog"
                    exit 1
                    ;;
            esac
            shift 2
            ;;
        --description)
            description="$2"
            shift 2
            ;;
        --criteria)
            criteria_list+=("$2")
            shift 2
            ;;
        --tags)
            tags_input="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 --title \"Title\" --priority \"LEVEL\" --description \"Desc\" [--criteria \"Criterion\"] [--tags \"tag1 tag2\"]"
            echo ""
            echo "Required:"
            echo "  --title       Brief task description"
            echo "  --priority    Priority level (critical, high-urgent, high, medium, low, backlog)"
            echo "  --description What needs to be done"
            echo ""
            echo "Optional:"
            echo "  --criteria    Acceptance criterion (can be specified multiple times)"
            echo "  --tags        Space-separated tags"
            echo "  --help        Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Validate required fields
if [ -z "$title" ] || [ -z "$priority" ] || [ -z "$description" ]; then
    echo "Error: --title, --priority, and --description are required"
    echo "Use --help for usage information"
    exit 1
fi

# Find next task number (check BOTH open and completed folders)
# Use 10# prefix to force decimal interpretation and avoid octal conversion of numbers with leading zeros
last_num=$(find "$PROJECT_ROOT/tasks"/{open,completed} -name "task-*.json" 2>/dev/null | sed 's/.*task-\([0-9]*\)\.json/\1/' | sort -n | tail -1)
next_num=$(printf "%03d" $((10#${last_num:-0} + 1)))

task_file="$TASKS_DIR/task-$next_num.json"

# Convert tags to JSON array
tags_json=$(echo "$tags_input" | jq -R 'split(" ") | map(select(length > 0))')

# Convert criteria to JSON array
criteria_json=$(printf '%s\n' "${criteria_list[@]}" | jq -R . | jq -s .)

# Create task JSON
timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)
json=$(jq -n \
  --arg id "task-$next_num" \
  --arg ts "$timestamp" \
  --arg title "$title" \
  --arg priority "$priority" \
  --arg desc "$description" \
  --argjson criteria "$criteria_json" \
  --argjson tags "$tags_json" \
  '{
    id: $id,
    created: $ts,
    updated: $ts,
    title: $title,
    status: "open",
    priority: $priority,
    description: $desc,
    assigned_to: null,
    acceptance_criteria: $criteria,
    investigation: [],
    solution: null,
    verification: null,
    blockers: [],
    related_tasks: [],
    tags: $tags
  }')

# Write task file using jq to ensure valid JSON
echo "$json" | jq . > "$task_file"

# Update index file by adding to open array
INDEX_FILE="$PROJECT_ROOT/tasks/INDEX.json"
if [ -f "$INDEX_FILE" ]; then
    timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    new_task=$(jq -c '{
        id: .id,
        title: .title,
        priority: .priority,
        status: .status,
        keywords: .tags,
        created: .created,
        updated: .updated
    }' "$task_file")
    jq --arg ts "$timestamp" --argjson task "$new_task" \
       '.generated = $ts | .open_count += 1 | .open += [$task]' \
       "$INDEX_FILE" > "$INDEX_FILE.tmp" && mv "$INDEX_FILE.tmp" "$INDEX_FILE"
fi

echo "task-$next_num"
