# Debugging Strategy for RicControlMessage Crashes

## Overview

This document outlines how to debug and distinguish between successful actions and crashes in the `RicControlMessage::ApplySimpleCommand` function.

## Current Actions Status

### ✅ Actions with Full Error Handling:
1. **set-mcs** - Complete with try-catch, strong references, validation checks
2. **set-enb-txpower** - Complete with try-catch, strong references, validation checks
3. **set-bandwidth** - Complete with try-catch, strong references, validation checks
4. **set-flow-rate** - Complete with try-catch, strong references, validation checks
5. **handover-trigger** - Complete with try-catch, strong references, validation checks

### ⚠️ Actions Needing Review:
1. **stop** - Simple command, but should add try-catch for consistency

## Debugging Techniques

### 1. Enhanced Logging Strategy

Add detailed logging at each critical step to track execution flow:

```cpp
// At the start of each lambda
fprintf(stderr, "[RicControlMessage] [%s] START: nodeId=%u, params=...\n", cmd.c_str(), nodeId);
fflush(stderr);

// After each object acquisition
fprintf(stderr, "[RicControlMessage] [%s] STEP: Got Node (refcount=%ld)\n", cmd.c_str(), n->GetReferenceCount());
fflush(stderr);

// Before critical operations
fprintf(stderr, "[RicControlMessage] [%s] STEP: About to call SetAttribute...\n", cmd.c_str());
fflush(stderr);

// After successful completion
fprintf(stderr, "[RicControlMessage] [%s] SUCCESS: Completed successfully\n", cmd.c_str());
fflush(stderr);
```

### 2. Using GDB for Crash Analysis

#### Compile with Debug Symbols:
```bash
cd ns-3-mmwave-oran
./waf configure --build-profile=debug
./waf build
```

#### Run with GDB:
```bash
gdb --args ./build/debug/scratch/ns3.38.rc1-our-v3-optimized [args]
```

#### GDB Commands for Debugging:
```gdb
# Set breakpoints at critical points
break ric-control-message.cc:311  # Start of set-mcs lambda
break ric-control-message.cc:354  # Before GetCcMap
break ric-control-message.cc:425  # Before SetAttribute

# Run until crash
run

# When crash occurs:
backtrace          # Show call stack
info registers     # Show CPU registers
print *this        # Show object state
print ccMap        # Show map contents
print ccBase       # Show Ptr state
```

### 3. Using Valgrind for Memory Issues

```bash
valgrind --tool=memcheck \
         --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --log-file=valgrind.log \
         ./build/optimized/scratch/ns3.38.rc1-our-v3-optimized [args]
```

Valgrind will detect:
- Use of uninitialized memory
- Reading/writing freed memory
- Memory leaks
- Invalid pointer operations

### 4. Signal Handling for SIGSEGV/SIGABRT

Add signal handlers to get stack traces on crashes:

```cpp
#include <execinfo.h>
#include <signal.h>

void signal_handler(int sig) {
    void *array[20];
    size_t size = backtrace(array, 20);
    
    fprintf(stderr, "[CRASH] Signal %d received:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    
    // Exit or abort based on signal
    if (sig == SIGSEGV) {
        abort();  // Generate core dump
    }
}

// In main or initialization:
signal(SIGSEGV, signal_handler);
signal(SIGABRT, signal_handler);
```

### 5. Comparison: Successful vs. Crashing Execution

#### Successful Execution Pattern:
```
[RicControlMessage] ApplySimpleCommand: Input JSON = '{"cmd":"set-mcs","node":2,"mcs":14}'
[RicControlMessage] Extracted cmd = 'set-mcs'
[RicControlMessage] set-mcs: attempting to set MCS to 14 on node 2
[RicControlMessage] set-mcs: node 2 MCS set to 14 (DL and UL)
```

#### Crashing Execution Pattern (Before Fix):
```
[RicControlMessage] ApplySimpleCommand: Input JSON = '{"cmd":"set-mcs","node":2,"mcs":14}'
[RicControlMessage] Extracted cmd = 'set-mcs'
[RicControlMessage] set-mcs: attempting to set MCS to 14 on node 2
munmap_chunk(): invalid pointer
Command died with <Signals.SIGABRT: 6>
```

#### Crashing Execution Pattern (After Fix - Should Show Exception):
```
[RicControlMessage] ApplySimpleCommand: Input JSON = '{"cmd":"set-mcs","node":2,"mcs":14}'
[RicControlMessage] Extracted cmd = 'set-mcs'
[RicControlMessage] set-mcs: attempting to set MCS to 14 on node 2
[RicControlMessage] set-mcs: exception setting attributes: [error message]
```

### 6. Adding Checkpoints for Debugging

Add checkpoints to identify where execution stops:

```cpp
#define DEBUG_CHECKPOINT(msg) \
    do { \
        fprintf(stderr, "[CHECKPOINT] %s:%d [%s] %s\n", __FILE__, __LINE__, cmd.c_str(), msg); \
        fflush(stderr); \
    } while(0)

// Usage:
DEBUG_CHECKPOINT("Entering lambda");
DEBUG_CHECKPOINT("Got Node");
DEBUG_CHECKPOINT("Got enbDev");
DEBUG_CHECKPOINT("Got ccMap");
DEBUG_CHECKPOINT("Extracted ccBase");
DEBUG_CHECKPOINT("About to SetAttribute");
DEBUG_CHECKPOINT("SetAttribute completed");
```

### 7. Object Lifetime Tracking

Add reference counting logs to track object lifetimes:

```cpp
fprintf(stderr, "[RicControlMessage] [%s] Object refcounts: Node=%ld, enbDev=%ld, cc=%ld, sched=%ld\n",
    cmd.c_str(),
    n ? n->GetReferenceCount() : 0,
    enbDev ? enbDev->GetReferenceCount() : 0,
    cc ? cc->GetReferenceCount() : 0,
    flexSched ? flexSched->GetReferenceCount() : 0);
fflush(stderr);
```

### 8. Memory Sanitizer (if available)

```bash
# Compile with AddressSanitizer
./waf configure --build-profile=debug CXXFLAGS="-fsanitize=address -g"
./waf build

# Run - will show detailed memory error information
./build/debug/scratch/ns3.38.rc1-our-v3-optimized [args]
```

## Common Crash Patterns and Solutions

### Pattern 1: "munmap_chunk(): invalid pointer"
**Cause**: Double free or use after free
**Solution**: Ensure strong references are kept, extract Ptr<> immediately from maps

### Pattern 2: SIGSEGV (Segmentation Fault)
**Cause**: Null pointer dereference or invalid memory access
**Solution**: Add null checks before all pointer/object access

### Pattern 3: SIGABRT (Abort Signal)
**Cause**: Assertion failure or memory corruption detected by allocator
**Solution**: Check for memory corruption, validate all inputs

## Testing Strategy

### 1. Stress Testing
Run the same command multiple times to catch intermittent issues:
```bash
for i in {1..100}; do
    echo "Test $i"
    # Send command
    # Check for crash
done
```

### 2. Boundary Testing
Test with edge cases:
- Invalid node IDs
- Out of range values
- Missing parameters
- Null/empty maps

### 3. Timing Testing
Test commands at different simulation times to catch timing-related issues

## Log Analysis Script

Create a script to analyze logs:

```bash
#!/bin/bash
# analyze_crashes.sh

LOG_FILE="$1"

echo "=== Crash Analysis ==="
echo ""

# Find crash points
echo "Crash points:"
grep -B 5 "SIGSEGV\|SIGABRT\|munmap\|invalid pointer" "$LOG_FILE"

echo ""
echo "Last successful action before crash:"
grep -B 10 "SIGSEGV\|SIGABRT" "$LOG_FILE" | grep "SUCCESS\|attempting" | tail -1

echo ""
echo "Exception messages:"
grep "exception" "$LOG_FILE"
```

## Next Steps

1. Add enhanced logging to all actions
2. Implement signal handlers for better crash reporting
3. Add reference count logging
4. Create automated test suite
5. Document all crash patterns encountered




