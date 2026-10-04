# Queued read validation

## DM-era integration validation

Run `python3 tests/era.py` after building the binary and `io-probe.so` as below.
The fixtures compare selectively updated digest bodies with independent full
digests, parse/replay delta records independently, and also apply deltas through
the CLI before checking source/target bytes. They cover buffered/mmap paths,
gaps, unsorted/overlapping ranges, partial source tails, differing era/hash block
sizes, empty lists, stdin XML, malformed/overflowing/out-of-bounds XML,
incompatible/missing baselines, output aliases, dry-run digest checks and failure
markers. Read instrumentation verifies that an empty list reads no source bytes
and a selected tail skips the source prefix. Streamed delta tests inject short
writes and EINTR into stdout. Actual device-mapper mappings and discard tracking
were not exercised; the change list must be complete and match the baseline.

The existing 579-invocation correctness matrix and failure, live-recovery and
target-resizing suites were rerun after integration. A compile with
`-DPAGE_SIZE=4096` verifies Dan Nelson's portability fix. The formatting memory
leak fix retains our existing thread-local buffers.

Baseline snapshot: `4b554d7`, committed before implementation as
`dagelf <coenraad@wish.org.za>`. The snapshot includes the pre-existing generated
build files and executable. Neither 400 GiB VM image was used or modified.

## Inspection and implementation

The original synchronous path reads source/destination buffers in
`globals.c:map_buffer`, hashes through `globals.c:hash_buffer`, compares digest
entries in `blocksync-fast.c:blocksync`, and coalesces target/digest writes in
`globals.c:blocksync_dev_wri_flush` and `globals.c:digest_wri_flush`. Initialization
in `init.c` formerly resized files and rewrote the digest header even during a
dry run. Cleanup is `globals.c:cleanup`/`freedev`; fsync was previously optional.
The shared `dev` offsets, `oper` hash state, flush state and `prog` counters cannot
be used concurrently. Existing buffers are addressed by explicit pread/pwrite,
but their bookkeeping assumes ordered traversal.

`queued.c:read_worker` owns a native Linux AIO context, reusable slots, completion
array, comparison buffers and hash context. Completions carry the originating
slot/offset. Native AIO has the same kernel ABI as libaio; there was no existing
AIO dependency to reuse. Requests are submitted individually, so a successful
submission always owns exactly one request; zero/EAGAIN submissions are retried
only after outstanding requests progress, or fail immediately if none exist.
Short completions resume the unread suffix. Exclusive contiguous ranges keep
all target and digest pwrite offsets disjoint. Completed buffers are processed
while other reads remain outstanding, with no completion backlog outside the
fixed-depth slot array. Workers cancel outstanding requests, destroy the context
before freeing buffers, and propagate the first error through atomic state.

Safety changes apply to the synchronous path too: alias rejection, dry-run
initialization, complete pread/pwrite loops, malformed/truncated digest validation,
destination comparison after creation/resizing, source-change detection and a
durable incomplete marker. Tests exposed an existing final-flush offset bug when
a pending changed run ended before a matching last block; it is now corrected.
Formatting allocations were replaced with a bounded per-thread string ring to
avoid existing leaks observed during sanitizer preparation.

## Reproduce

Build a test interposer on an executable filesystem (this environment's `/tmp`
is mounted `noexec`, including shared-library mappings):

```sh
mkdir -p .test-build
cc -shared -fPIC -o .test-build/io-probe.so tests/io_probe.c -ldl
python3 tests/queued_reads.py src/blocksync-fast "$PWD/.test-build/io-probe.so" "$PWD/.test-build/blocksync-fast-baseline"
python3 tests/queued_failures.py src/blocksync-fast "$PWD/.test-build/io-probe.so"
python3 tests/live_recovery.py
python3 tests/target_resize.py
python3 tests/read_performance.py .test-build/blocksync-fast-baseline 1024 3
```

Keep the original executable from the baseline snapshot in `.test-build` for the
optional compatibility and performance comparisons. The scripts use disposable
fixtures and `cmp -- source target` after successful copies. Digest bodies are
compared independently, excluding the 512-byte timestamp-bearing header.
`io_probe.c` counts actual successful target pwrite bytes and injects short I/O,
EINTR, ENOSPC, failed/zero submissions, short and reversed completions, read
failures, unsupported direct completions and delayed operations. Instrumentation
failure is an explicit test failure. Mmap cases use byte comparisons and unchanged
target modification times; the pwrite interposer does not count mmap stores.

## Results

- 579-invocation correctness matrix passed, including the baseline executable's
  compatible digest entries. Sizes: 0, 1, 4095, 4096, 4097, 262143, 262144,
  262145, 32767, 32768, 32769 and 64 MiB + 4097. Paths: synchronous, mmap,
  queued jobs 1/2/4/8, depths 1/2/16, buffered and direct reads. Exact write byte
  counts cover changed blocks, read-buffer and actual worker boundaries, adjacent
  and scattered changes, partial tails, unchanged copies and missing digests.
- Sparse fixtures with leading/trailing/interior holes, explicit zero extents and
  stale nonzero targets passed. Sparse reads are ordinary zero-returning reads;
  no extent-discovery optimization was implemented. Extent-discovery fallback
  testing is therefore not applicable.
- Missing/new target and digest, shorter/longer targets, forced sizing, dry runs
  (including missing files and mismatched sizes), malformed/truncated headers and
  bodies, wrong size/block/algorithm, excessive and malformed read options,
  hard links, symlinks, incompatible modes and no-digest copies passed.
- SIGINT and SIGKILL during delayed source reads, target writes and digest writes,
  source truncation, permission failures, injected read errors and target/digest
  ENOSPC passed. Failed runs retained the marker; retry ignored the incomplete digest,
  compared destination bytes, rebuilt entries and cleared the marker after
  successful synchronization. Active-marker locks rejected overlapping runs. Small fixtures exposed nine Linux tasks with eight reader workers.
  Twenty repeated queued/direct invocations passed.
- CRC32, MD5, SHA256, XXH32, XXH64, XXH3LOW, XXH3 and XXH128 entries matched the
  synchronous encoding. A separate build without xxhash passed default CRC32
  synchronous/queued comparison.
- AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer and ThreadSanitizer
  passed a 64 MiB + 4097 fixture with eight direct readers. Independent cmp passed
  for both targets. These checks ran outside sandbox tracing; LeakSanitizer does
  not work under ptrace, and ThreadSanitizer required `setarch x86_64 -R` to reserve
  its shadow address space. `queued.c` compiles with `-Wall -Wextra -Werror`.

Full-device ENOSPC was simulated at the write interface rather than filling a
filesystem. Unsupported direct I/O was fault-injected as EINVAL; the actual fixture
filesystem supports direct reads. Unsupported native AIO/filesystems and physical
block devices were not available as separate hardware fixtures. A separate live-content/recovery suite passed changes during reads, next-pass
convergence, active-sync exclusion, recovery from apparently valid stale digest
entries and partial headers, and non-mutating dry-run recovery. Running-image
sync is supported as repeated best-effort passes. A stopped VM/snapshot is needed
for a point-in-time copy and reproducible correctness/performance measurements. Kill tests are process termination tests, not physical
power-loss tests. The durability protocol uses fsync on marker/target/digest and
parent directories.

## Performance

The benchmark uses a fully allocated stable 1 GiB fixture and restores identical
target/digest states before every timed run. The scattered workload changes 256
4 KiB blocks (1 MiB). It records wall/user/system time, process peak RSS, kernel
block I/O counters and per-drive diskstats; see `performance-results.json` for raw
results. Cache eviction is requested using POSIX_FADV_DONTNEED, without globally
flushing caches. Direct readers bypass the page cache. Three repetitions are run
per workload/mode. `cmp` verification and setup are outside elapsed sync time.
Diskstats reflect whole-host traffic and filesystem metadata, not exclusively
this process. Peak RSS includes the subprocess's pre-exec memory high-water mark.
The original executable did not synchronize final writes; the new executable
includes the required durability steps, so its write counters include the marker
and filesystem metadata. This is not a durability-equivalent original benchmark.

| Workload | Readers | Elapsed s | User s | System s | Peak RSS MiB | Process reads MiB | Process writes MiB |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| unchanged | original | 0.387 | 0.064 | 0.296 | 14.7 | 1024.0 | 0.035 |
| unchanged | new-sequential | 0.439 | 0.093 | 0.287 | 14.7 | 1024.0 | 0.242 |
| unchanged | queued-1 | 0.363 | 0.071 | 0.120 | 14.7 | 1031.5 | 0.195 |
| unchanged | queued-4 | 0.254 | 0.074 | 0.120 | 18.0 | 1040.8 | 0.211 |
| unchanged | queued-8 | 0.251 | 0.100 | 0.160 | 34.2 | 1057.8 | 0.367 |
| scattered | original | 0.397 | 0.075 | 0.304 | 14.7 | 1024.0 | 2.000 |
| scattered | new-sequential | 0.482 | 0.088 | 0.318 | 14.7 | 1024.0 | 2.348 |
| scattered | queued-1 | 0.231 | 0.072 | 0.093 | 14.7 | 1031.5 | 2.379 |
| scattered | queued-4 | 0.257 | 0.075 | 0.137 | 18.0 | 1040.8 | 2.426 |
| scattered | queued-8 | 0.258 | 0.089 | 0.159 | 34.1 | 1058.0 | 2.379 |

Values are medians of three isolated repetitions. Eight readers improve both
workloads over the original implementation on this fixture. Four readers are
similar in elapsed time and use less system CPU than eight. Eight is therefore
not an established optimum. Increased system CPU and physical read counters
with eight readers suggest kernel/filesystem queue overhead and drive read
amplification are limiting further scaling on this small fixture; this is an
inference from counters, not a traced full-image bottleneck diagnosis. Four
readers provide similar throughput to eight in these runs. The one-reader
scattered median is lower still, illustrating filesystem/cache placement
variability and why this short fixture cannot select full-image concurrency.
The unchanged eight-reader fixture throughput is calculated from complete sync
elapsed time, including comparison and durability, rather than fio throughput.

Full-image acceptance remains unverified: the user explicitly declined use of the
400 GiB images. fio's 22.5 GB/s is read-path throughput and has not been used as a
sync performance result or promise. No sparse optimization was measured.

## Automatic resize and preallocation follow-up

`target_resize.py` passed automatic growth with compatible old digests, partial
old EOF blocks and zero/nonzero tails on synchronous, mmap, queued buffered and
queued direct paths. Allocation instrumentation verified that default growth makes no allocation
calls, --preallocate reserves precisely the added range with KEEP_SIZE, and dry
runs make no allocation calls even when the flag is specified. Recovery without
--preallocate succeeds when allocation calls are fault-injected to fail.
Shrink rejection, EOF/default-no, explicit yes, -y/--yes and --force not bypassing
confirmation passed with unchanged data/size/mtime on rejection. Injected ENOSPC,
EDQUOT, EOPNOTSUPP and EFBIG during preallocation failed before target data copying
or digest changes; retry recovered automatically. The zero-tail destination
truncation reproducer now fails, both on destination EOF and on final size checks
when a valid digest suppresses target reads. Simulated smaller block-device
capacity was rejected, and two syncs with different digests contended on the
same destination inode lock. These are disposable fixture tests, not VM-image
runs. Existing performance measurements predate this resize follow-up.
