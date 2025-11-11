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

## Documentation

- **[TASK_MANAGEMENT.md](./TASK_MANAGEMENT.md)** - How the task system works
- **[README.md](./README.md)** - Project overview
- **[docs/](./docs/)** - Detailed project documentation

## Next Step

Run: `./tasks/scripts/recommend-task.sh`

---

**Last Updated**: 2025-11-11
