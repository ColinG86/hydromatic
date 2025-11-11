# Hydromatic Project Configuration

---

# Hydromatic - AI Agent Entry Point

**START HERE** - Single entry point for all AI agents.

## Quick Commands

```bash
# What should I work on?
./tasks/scripts/recommend-task.sh

# I'm starting work on a task
./tasks/scripts/claim-task.sh task-NNN your-name

# I made changes
./tasks/scripts/log-change.sh \
  --type fix \
  --component parser \
  --summary "Description of change" \
  --tested y

# I finished the task
./tasks/scripts/finish-task.sh task-NNN "Testing summary"

# Create a new task
./tasks/scripts/new-task.sh \
  --title "Task title" \
  --priority high \
  --description "Description"
```

## One-Minute Workflow

```bash
# 1. Get recommended task
./tasks/scripts/recommend-task.sh

# 2. Claim it
./tasks/scripts/claim-task.sh task-XXX my-agent-name

# 3. Do the work...

# 4. Log changes as you go
./tasks/scripts/log-change.sh \
  --type feature \
  --component parser \
  --summary "Added null safety checks" \
  --tested y \
  --task task-XXX

# 5. When done, finish it
./tasks/scripts/finish-task.sh task-XXX "Tested on device, all criteria met"
```

---

# Task Management System

How the task system works.

## Architecture

Two-part system for efficiency:

1. **Task files** - `tasks/{open|completed|deferred}/task-NNN.json` - Full context for detailed work
2. **Change log** - `changes.jsonl` - Record of all changes (append-only, JSON Lines format)
3. **Scripts** - `tasks/scripts/` - Automate all operations (agents don't manually edit JSON)

## Task File Structure

Each task is a JSON file with:
- `id`, `title`, `description` - What needs to be done
- `status` - open | in-progress | ready-for-review | blocked | completed | deferred
- `priority` - critical | high | medium | low
- `acceptance_criteria` - What "done" means
- `investigation` - Notes from exploring the problem
- `solution` - How it was solved
- `verification` - Test results
- `blockers`, `related_tasks`, `tags` - Metadata

**Files are created/updated by scripts, not manually.**

## Change Log Format

`changes.jsonl` - One JSON object per line:
```json
{"timestamp":"2025-11-11T14:30:00Z","type":"fix","component":"parser","summary":"Fixed null reference in JSON parser","files":["/home/colin/projects/hydromatic/src/parser.rs"],"agent":"claude","tags":["bug","parser"],"tested":true,"task":"task-001"}
```

**Important**: Append-only, never modify existing lines.

## Scripts Do The Work

All scripts in `tasks/scripts/`:

- `recommend-task.sh` - Analyzes open tasks, recommends best one to work on
- `claim-task.sh` - Claim a task (updates status + assigned_to automatically)
- `log-change.sh` - Log a change (minimal prompting, auto-timestamps + formatting)
- `finish-task.sh` - Mark task complete (updates status + verification)
- `new-task.sh` - Create new task with arguments (auto-generates ID + JSON structure)

**Agents use scripts, not manual JSON editing.**

### new-task.sh Usage

Non-interactive task creation via command-line arguments:

```bash
./tasks/scripts/new-task.sh \
  --title "Task title" \
  --priority "critical|high-urgent|high|medium|low|backlog" \
  --description "What needs to be done" \
  [--criteria "Criterion 1"] \
  [--criteria "Criterion 2"] \
  [--tags "tag1 tag2"]
```

**Arguments:**
- `--title` (required) - Brief task description
- `--priority` (required) - Priority level (critical, high-urgent, high, medium, low, backlog)
- `--description` (required) - Detailed what/why for the task
- `--criteria` (optional) - Acceptance criterion (can specify multiple times)
- `--tags` (optional) - Space-separated tags for organization

**Returns:** Task ID (e.g., `task-042`) on stdout for use in subsequent commands

**Example:**
```bash
task_id=$(./tasks/scripts/new-task.sh \
  --title "Fix null pointer in parser" \
  --priority "high-urgent" \
  --description "Parser crashes on nested arrays" \
  --criteria "Nested arrays parse correctly" \
  --criteria "No crashes on edge cases" \
  --tags "parser bugfix")

./tasks/scripts/claim-task.sh "$task_id" my-agent
```

### claim-task.sh Usage

Claim a task to begin working on it:

```bash
./tasks/scripts/claim-task.sh <task-id> <agent-name>
```

**Arguments:**
- `<task-id>` (required) - Task ID to claim (e.g., `task-001`)
- `<agent-name>` (required) - Name of agent claiming the task (e.g., `claude`)

**Example:**
```bash
./tasks/scripts/claim-task.sh task-001 claude
```

### log-change.sh Usage

Log a change to the change log as you work:

```bash
./tasks/scripts/log-change.sh \
  --type <type> \
  --component <component> \
  --summary <summary> \
  [--files "file1 file2"] \
  [--tags "tag1 tag2"] \
  [--tested y/n] \
  [--task <task-id>] \
  [--agent <agent-name>]
```

**Required Arguments:**
- `--type` - Change type: `fix` | `feature` | `refactor` | `doc` | `test` | `chore`
- `--component` - What changed (e.g., `parser`, `auth`, `UI`)
- `--summary` - One-line description of the change

**Optional Arguments:**
- `--files` - Space-separated absolute paths of modified files
- `--tags` - Space-separated tags (e.g., `bug parser null-safety`)
- `--tested` - `y` or `n` (default: `n`)
- `--task` - Task ID if applicable (e.g., `task-001`)
- `--agent` - Agent name (default: `claude`, overrides `AGENT_NAME` env var)

**Example:**
```bash
./tasks/scripts/log-change.sh \
  --type feature \
  --component parser \
  --summary "Add null safety checks" \
  --files "/home/colin/projects/hydromatic/src/parser.rs" \
  --tags "parser null-safety" \
  --tested y \
  --task task-001 \
  --agent claude
```

### finish-task.sh Usage

Mark a task as completed and move it to `tasks/completed/`:

```bash
./tasks/scripts/finish-task.sh <task-id> <test-summary> [--skip-criteria-check]
```

**Arguments:**
- `<task-id>` (required) - Task ID to finish (e.g., `task-001`)
- `<test-summary>` (required) - Brief summary of testing performed

**Options:**
- `--skip-criteria-check` - Skip validation that acceptance criteria are defined

**What it does:**
1. Updates task status to `completed`
2. Records verification details (test summary, timestamp)
3. Moves task file from `tasks/open/` to `tasks/completed/`

**Example:**
```bash
./tasks/scripts/finish-task.sh task-001 "Tested feature on device, all tests pass"
```

## Status Values

- `open` - Available, not started
- `in-progress` - Actively being worked on
- `completed` - Finished by agent, file moved to `tasks/completed/`
- `blocked` - Cannot proceed, see `blockers`
- `deferred` - Paused intentionally

## Priority Values

- `critical` - Must do immediately, blocks other work
- `high-urgent` - High priority AND time-sensitive
- `high` - Should do soon, important
- `medium` - Normal priority
- `low` - Nice to have, low impact
- `backlog` - Deferred, consider later

## Quick Reference

```bash
# What should I work on?
./tasks/scripts/recommend-task.sh

# Start working on a task
./tasks/scripts/claim-task.sh task-001 my-agent-name

# Log a change as you work
./tasks/scripts/log-change.sh \
  --type feature \
  --component parser \
  --summary "Fixed null pointer issue" \
  --tested y \
  --task task-001

# Finish the task
./tasks/scripts/finish-task.sh task-001 "Tested on device, all tests pass"

# Create a new task
./tasks/scripts/new-task.sh \
  --title "Brief description" \
  --priority "high-urgent" \
  --description "What needs to be done" \
  --criteria "Acceptance criterion 1" \
  --criteria "Acceptance criterion 2" \
  --tags "tag1 tag2"
```

## Workflow

### Working on Existing Tasks

1. **Run `recommend-task.sh`** - Get task recommendation with full context
2. **Run `claim-task.sh task-001 agent-name`** - Claim the task, set status to `in-progress`
3. **Do the work** - Read task file, make changes, build, test
4. **Run `log-change.sh --type fix --component X --summary "..."` --tested y --task task-001`** - Log each significant change
5. **Run `finish-task.sh task-001 "test summary"`** - Mark as `completed` and move file to `tasks/completed/`

### Creating New Tasks

Use `new-task.sh` with arguments (non-interactive):

```bash
task_id=$(./tasks/scripts/new-task.sh \
  --title "What needs to be done" \
  --priority "high-urgent" \
  --description "Why and what" \
  --criteria "How to verify it's done" \
  --tags "relevant tags")

# Then claim and work on it
./tasks/scripts/claim-task.sh "$task_id" my-agent
```

Scripts handle all file updates. **Agents only call scripts.**

---

**Last Updated**: 2025-11-11
