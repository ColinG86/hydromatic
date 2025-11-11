#!/bin/bash
# Test hook to verify hook execution
echo "HOOK EXECUTED AT: $(date)" >> /tmp/claude-hook-test.log
exit 0
