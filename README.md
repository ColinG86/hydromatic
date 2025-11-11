# Hydromatic

[Project description goes here]

## 🤖 For AI Agents

**START HERE**: [`AGENTS.md`](./AGENTS.md) - Single entry point for all AI agents

Quick workflow:
```bash
./tasks/scripts/recommend-task.sh      # What should I work on?
./tasks/scripts/claim-task.sh task-NNN your-name  # Start the task
./tasks/scripts/log-change.sh --type fix --component parser --summary "..." --tested y  # Log changes
./tasks/scripts/finish-task.sh task-NNN "Testing summary"           # Mark complete
```

See [`TASK_MANAGEMENT.md`](./TASK_MANAGEMENT.md) for system overview.

## 👤 For Humans

This project uses an AI-friendly task management system:
- **`AGENTS.md`** - Instructions for AI agents
- **`tasks/`** - Task files (open, completed, deferred)
- **`changes.jsonl`** - Append-only change log
- **`tasks/scripts/`** - Automated task management helpers

### Key Files

- **`AGENTS.md`** - AI agent entry point (quick commands)
- **`TASK_MANAGEMENT.md`** - How the task system works
- **`tasks.json`** - Task index (priority-sorted)
- **`changes.jsonl`** - Complete change history
- **`docs/`** - Project documentation

### Quick Commands

View task status:
```bash
./tasks/scripts/show-status.sh
```

Search tasks and changes:
```bash
./tasks/scripts/search-tasks.sh keyword
```

View recent changes:
```bash
tail -20 changes.jsonl | jq
```

## Project Structure

```
.
├── AGENTS.md              # AI agent entry point
├── README.md              # This file
├── TASK_MANAGEMENT.md     # Task system explanation
├── tasks.json             # Task index
├── changes.jsonl          # Change log
├── tasks/
│   ├── scripts/           # Task management automation
│   ├── open/              # Active tasks
│   ├── completed/         # Finished tasks
│   └── deferred/          # Paused tasks
├── scripts/               # Project scripts
├── docs/                  # Documentation
└── src/                   # Source code (TBD)
```

---

**Setup Instructions**: See [`docs/setup.md`](./docs/setup.md) (when created)

**For AI agents**: Read [`AGENTS.md`](./AGENTS.md)
