# Blocksync-fast
![blocksync-fast](https://raw.githubusercontent.com/nethappen/blocksync-fast/main/assets/images/blocksync-fast.png)

[![Build Status](https://github.com/nethappen/blocksync-fast/actions/workflows/build-and-release.yml/badge.svg)](https://github.com/nethappen/blocksync-fast/actions)

Blocksync-fast is a program written in C that clones and synchronizes any block devices (entire disks, partitions) or files (disk images) using fast and efficient methods. It uses buffered reads and writes to combine adjacent blocks together reducing the number of I/O operations. At synchronization process program overwrites only changed blocks which reduces data transfer and maintains blocks deduplication in Copy-on-write file systems.

The digest file can be used to store checksums of blocks from a previous sync to avoid read operations from the target disk. This optimization is especially desirable when synchronizing fast NVM drives with slower HDD drives or when transferring data over the network. The program can also creates delta file that stores only differing blocks which can be applied to the destination.

Blocksync-fast uses the Libgcrypt library and supports many hashing algorithms, both cryptographic and non-cryptographic. Optionally, it can also use hashing algorithms from the xxHash family, which offers the very high speed of processed data on processors with SSE2, AVX2 instructions. [see list of hashing algos](#hashing-algos)

## Requirements

- Linux with a C compiler, Make, Autoconf >= 2.71, Automake, pkg-config,
  and Linux native AIO development headers (`linux/aio_abi.h`)
- Required library: [Libgcrypt](https://github.com/gpg/libgcrypt) >= 1.9.0
- Optional library: [xxHash](https://github.com/Cyan4973/xxHash) >= 0.8.0

## Installation

Install the development headers for Libgcrypt and, if desired, xxHash.
Generated Autotools files are not tracked in Git, so regenerate them before
configuring a fresh checkout:

```console
 $ autoreconf --install
 $ ./configure
 $ make
 $ make install
```

## Build artifacts and releases

The GitHub Actions workflow builds a Linux binary and Debian package on pushes.
Workflow artifacts are retained for five days; artifact-upload failures are
nonfatal, so a successful build does not guarantee an artifact is available.
Successful release uploads on pushes of tags beginning with `v` attach the binary
and package to a GitHub release. Release assets are separate from the temporary
workflow artifacts.

The release name and tag use `git describe --tags --long --dirty`, rather than
the pushed tag verbatim. For example, pushing `v1.0.8` can produce a release tag
such as `v1.0.8-0-gabcdef0`. Creating releases requires the workflow's GitHub token
to have permission to write repository contents.

## Usage

```console
 blocksync-fast [options]
 blocksync-fast -s <src_device> -d <dst_device> [-f <digest_file>] [options]
 blocksync-fast -s <src_device> [-f <digest_file>] --make-digest [options]
 blocksync-fast -s <src_device> [-f <digest_file>] --make-delta -D <delta_file> [options]
 blocksync-fast -d <dst_device> --apply-delta -D <delta_file> [options]
```

## Basic examples

#### Clone devices

```console
 $ blocksync-fast -s /dev/vg1/vol1-snap -d /mnt/backups/vol1
```

#### Synchronizing devices with digest

```console
 $ blocksync-fast -s /dev/vg1/vol1-snap -d /mnt/backups/vol1 -f /var/cache/backups/vol1.digest
```

#### Synchronizing devices with digest sending delta over SSH

```console
 $ blocksync-fast --make-delta -s /dev/vg1/vol1-snap -f /var/cache/backups/vol1.digest | ssh 192.168.1.115 'blocksync-fast --apply-delta -d /mnt/backups/vol1'
```

## Queued source reads

```console
src/blocksync-fast -s ~/vms/400G-SQL.img -d /tmp/vms/400G-SQL.img -f /tmp/vms/400G-SQL.img.digest -b 4K --read-jobs=8 --read-depth=16 --read-size=256K --direct-read
```

A running VM image can be synchronized repeatedly: each digest entry represents
bytes read and copied for that block, and changes after its read are picked up
on a later pass. A live pass can combine blocks from different moments; it is not
a point-in-time or guaranteed recoverable VM backup. Use a stopped VM or stable
snapshot for a consistent copy and reproducible correctness/performance tests.
Snapshots still benefit from faster transfers and shorter retention times.
The options enable bounded Linux native AIO
reads, using the same kernel interface as libaio. Each reader has its own Linux
task ID, queue, hash context, aligned buffers and contiguous range. Eight readers
with sixteen 256 KiB slots hold at most 32 MiB of source data in their queues,
plus bounded comparison, digest and existing device buffers. Completed reads
are hashed and compared in 4 KiB blocks; adjacent changed blocks within a read
are written together. The digest indexing, hash algorithm and hash encoding are
unchanged. Matching truncated hashes retain the existing collision risk.

Without these options the existing synchronous reader remains the default.
Specifying any read option enables the queued path; omitted values are one reader,
depth sixteen and 256 KiB reads. `--buffer-size` retains its existing per-device
buffer meaning. Limits are 64 readers, depth 256, read sizes from 4 KiB to 64 MiB
in 4 KiB multiples, and 256 MiB total queued source buffers. Queued reads initially
support block-sync with seekable files/devices and 4 KiB blocks. They reject mmap,
stdin/stdout data streams, delta modes and detailed block progress. Basic progress
prints a final summary when queued readers finish.

`--direct-read` opens only the source with `O_DIRECT`. Alignment comes from
`STATX_DIOALIGN`, the block device sector size when applicable, or a conservative
4 KiB alignment on older regular-file systems. The final unaligned read uses the
buffered source descriptor. Unsupported direct I/O produces an error and fails
cleanly; omit `--direct-read` to use buffered reads. Direct writes are never enabled
implicitly. Linux buffered AIO may perform reads synchronously during submission.

Sparse handling is unchanged: ordinary reads return zeros for holes, which enter
comparisons and digest generation and overwrite stale destination data. This
change does not skip host extents or parse NTFS metadata.

Regular-file targets grow automatically when the source is larger; `--force` is
not needed for resizing. A warning reports the old and new sizes. By default,
the target is extended without reserving disk space in advance. Specify
`--preallocate` to reserve the added range before copying: native Linux `fallocate(FALLOC_FL_KEEP_SIZE)` reserves the added range,
then the target is extended. With this flag, new files reserve their entire size. Allocation,
quota, size-limit or unsupported-preallocation errors fail early; there is no
zero-writing fallback. Reservation includes zero/hole ranges, and covers the
added range rather than extra space needed for overwriting existing CoW extents
or filesystem metadata. Preallocation failure leaves existing target bytes,
size and digest contents unchanged; its incomplete marker enables safe retry.

When the source is smaller, block-sync warns and asks before truncating the target
(default: no). Pass `-y` or `--yes` to accept shrinking in advance. `--force` does
not bypass this confirmation. Noninteractive runs must pass `-y` to allow
shrinking; stdin sources also require `-y` when shrinking would consume input
intended as source data. Declining leaves target, digest and marker unchanged;
dry runs report the proposed change without prompting or reserving space. Old
digests covering a different source size are rebuilt by destination comparison.
Smaller block devices are rejected even with `--force`; larger devices retain the
existing `--force` behavior of syncing only the source-sized prefix.

The destination inode is locked for the entire sync, even when callers use
different digest paths. Unexpected destination EOF is an error; zeros beyond
EOF are synthesized only for a dry run's proposed extension. Before success,
block-sync verifies the final regular-file size, so outside truncation cannot be
mistaken for matching zero-filled data. Other applications must leave the target
alone during the sync; inode locks are advisory.

Block-sync persists `<digest>.incomplete` before target creation, resizing or
updates. It removes the marker only after target and digest synchronization and
directory synchronization succeed. After failure or interruption, the next block-sync automatically ignores the
incomplete digest's header and entries, compares destination bytes, and rebuilds
the digest using the current block-size/algorithm options. The marker stays in
place until recovery succeeds. An exclusive lock on the marker prevents another
sync using the same digest from starting while one is active. Dry-run recovery
compares destination bytes while leaving target, digest and marker unchanged.
Digest-only and delta modes refuse incomplete digests until block-sync recovers
them. Keep the marker with the digest when moving files. Compatible old digests without a marker remain
supported. A digest assumes its destination has not been changed externally;
discard it when that assumption is false. Resizing/recreating a destination forces
comparison instead of trusting its old digest. Source/destination inode and block
device aliases are rejected before resizing.

`--dont-write` does not create, truncate or update the target or digest, including
when either is missing. For block-sync, `--dont-write-target` also suppresses digest
updates so it cannot claim that uncopied data reached the target. A run with
`--dont-write-digest` retains the incomplete marker; the next block-sync
compares destination bytes and rebuilds it. Dry runs with mmap use buffered comparisons.

fio can be used to find the optimal settings and throughput can get close to theoretical maximum.
Digest access, comparison, target writes, filesystem behavior and synchronization
all contribute to elapsed sync time. See [fixture validation and measurements](tests/VALIDATION.md).

## Options

|                                  Argument | Description                                                                                                 |
| ----------------------------------------: | ----------------------------------------------------------------------------------------------------------- |
|                            -s, --src=PATH | Source block device or disk image                                                                           |
|                            -d, --dst=PATH | Destination block device or disk image                                                                      |
|                         -S, --size=N[KMG] | Data size in N bytes for STDIN data or override disk image size                                             |
|                             --make-digest | Creates only digest file or write digest to stdout                                                          |
|                         -f, --digest=PATH | Digest file stores checksums of the blocks from sync                                                        |
|                              --make-delta | Creates a delta file from src                                                                               |
|                             --apply-delta | Applies a delta file to dst                                                                                 |
|                          -D, --delta=PATH | Delta file path. If none, data write to stdout or read from stdin                                           |
|                   -b, --block-size=N[KMG] | Block size in N bytes for writing and checksum calculations (default:4K)                                    |
|                           -a, --algo=ALGO | Cryptographic hash algorithm which is used to compute checksum to compare blocks (default:CRC32 or XXH3LOW) |
|                          -l, --list-algos | It prints all supported hash algorithms                                                                     |
|                         --benchmark-algos | Benchmark all supported hash algorithms                                                                     |
|                             --digest-info | Checks digest file, prints info and exit                                                                    |
|                              --delta-info | Checks delta file, prints info and exit                                                                     |
|                           -e, --era=PATH | DM-era XML change list for make-digest/make-delta; requires a compatible baseline digest                    |
|                        -E, --era-sectors=N | Era tracking block size in 512-byte sectors (default:4096)                                                   |
|                           --read-jobs=N | Queued source reader threads (opt-in)                                                                       |
|                          --read-depth=N | Maximum outstanding reads per reader (default:16)                                                          |
|                     --read-size=N[KMG] | Bytes per queued source read (default:256K); independent of 4K hash blocks                                  |
|                         --direct-read | Direct source reads; fail cleanly if unsupported                                                           |
|                      --buffer-size=N[KMG] | Size of the buffer in N bytes for processing data per device (default:2M)                                   |
|               --progress, --show-progress | Show current progress while syncing                                                                         |
| --progress-detail, --show-progress-detail | Show more detailed progress which generates a lot of writes on the console                                  |
|                                    --mmap | Use a system mmap instead of direct read and write method                                                   |
|                              --no-compare | Copy all data from src to dst without comparing differences                                                 |
|                             --sync-writes | Immediately flushes and writes data to the disk specified at --buffer-size                                  |
|                              --dont-write | Perform dry run with no updates to target and digest file                                                   |
|                       --dont-write-target | Suppress target updates; block-sync also suppresses digest updates                                         |
|                       --dont-write-digest | Perform run with no updates only to digest file                                                             |
|                           --preallocate | Reserve added target space before copying (opt-in)                                                         |
|                               -y, --yes | Accept target shrinking without prompting                                                                  |
|                                   --force | Override incompatible digest parameters or sync a larger device's source-sized prefix; does not approve shrinking or enlarge smaller devices |
|                                  --silent | Doesn't print any messages                                                                                  |
|                                -h, --help | Show this help message                                                                                      |
|                             -V, --version | Show version                                                                                                |

## Hashing algos

Hashing algorithms are used to calculate the checksum for each block when using digest files. The smaller block size, the more checksums need to be computed. The length of the algorithm key, which affects the size of the digest file, is also important.

See all available hashing algorithms:

```console
$ blocksync-fast --list-algos
```

The default selection is CRC32 with a block size of 4KB. CRC32 has a hardware acceleration in modern processors with the SSE 4.2 instruction. Your system must support it and the appropriate kernel module should be loaded.

To check whether the "crc32c-intel" module is used in your system:

```console
$ cat /proc/crypto | grep crc32c_intel
```

For installations with the xxxHash library, the default choice is XXH3LOW. The XXH3LOW checksum is the lower 32 bits of the 64-bit hash from the fast XXH3 algorithm.
The instructions (SSE2, AVX2) of the processor will be auto-detected during build program, which will optimize the calculation of XXXH3 checksums.

Choosing the right hashing algorithm depends on user individual requirements and factors such as efficiency in calculations, the block-size or collision resistance. In practice, CRC32 and XXH3LOW for 4KB blocks are a good and sufficient solution.

To see how calculations are performed on your processor, you can run a benchmark and decide which algorithm to select:

```console
$ blocksync-fast --benchmark-algos --block-size=4KB
```

#### Benchmark algos on Intel(R) Xeon(R) CPU E5-1620 v3 @ 3.50GHz

```markdown
Algo: SHA1 Hash size: 20 bytes Speed: 234549 hashes/s Processing: 916.21 MiB/s
Algo: RMD160 Hash size: 20 bytes Speed: 102646 hashes/s Processing: 400.96 MiB/s
Algo: MD5 Hash size: 16 bytes Speed: 159440 hashes/s Processing: 622.81 MiB/s
Algo: TIGER Hash size: 24 bytes Speed: 137125 hashes/s Processing: 535.64 MiB/s
Algo: TIGER1 Hash size: 24 bytes Speed: 137676 hashes/s Processing: 537.80 MiB/s
Algo: TIGER2 Hash size: 24 bytes Speed: 138241 hashes/s Processing: 540.00 MiB/s
Algo: SHA224 Hash size: 28 bytes Speed: 103045 hashes/s Processing: 402.52 MiB/s
Algo: SHA256 Hash size: 32 bytes Speed: 102584 hashes/s Processing: 400.72 MiB/s
Algo: SHA384 Hash size: 48 bytes Speed: 133439 hashes/s Processing: 521.25 MiB/s
Algo: SHA512 Hash size: 64 bytes Speed: 136491 hashes/s Processing: 533.17 MiB/s
Algo: SHA512_224 Hash size: 28 bytes Speed: 136299 hashes/s Processing: 532.42 MiB/s
Algo: SHA512_256 Hash size: 32 bytes Speed: 135942 hashes/s Processing: 531.02 MiB/s
Algo: SHA3_224 Hash size: 28 bytes Speed: 93003 hashes/s Processing: 363.29 MiB/s
Algo: SHA3_256 Hash size: 32 bytes Speed: 87320 hashes/s Processing: 341.09 MiB/s
Algo: SHA3_384 Hash size: 48 bytes Speed: 67098 hashes/s Processing: 262.10 MiB/s
Algo: SHA3_512 Hash size: 64 bytes Speed: 48461 hashes/s Processing: 189.30 MiB/s
Algo: CRC32 Hash size: 4 bytes Speed: 3089828 hashes/s Processing: 11.79 GiB/s
Algo: CRC32_RFC1510 Hash size: 4 bytes Speed: 3122850 hashes/s Processing: 11.91 GiB/s
Algo: CRC24_RFC2440 Hash size: 3 bytes Speed: 3073846 hashes/s Processing: 11.73 GiB/s
Algo: WHIRLPOOL Hash size: 64 bytes Speed: 54820 hashes/s Processing: 214.14 MiB/s
Algo: GOSTR3411_94 Hash size: 32 bytes Speed: 12396 hashes/s Processing: 48.42 MiB/s
Algo: STRIBOG256 Hash size: 32 bytes Speed: 26564 hashes/s Processing: 103.77 MiB/s
Algo: STRIBOG512 Hash size: 64 bytes Speed: 27441 hashes/s Processing: 107.19 MiB/s
Algo: BLAKE2B_160 Hash size: 20 bytes Speed: 269285 hashes/s Processing: 1.03 GiB/s
Algo: BLAKE2B_256 Hash size: 32 bytes Speed: 269841 hashes/s Processing: 1.03 GiB/s
Algo: BLAKE2B_384 Hash size: 48 bytes Speed: 267555 hashes/s Processing: 1.02 GiB/s
Algo: BLAKE2B_512 Hash size: 64 bytes Speed: 267689 hashes/s Processing: 1.02 GiB/s
Algo: BLAKE2S_128 Hash size: 16 bytes Speed: 158033 hashes/s Processing: 617.32 MiB/s
Algo: BLAKE2S_160 Hash size: 20 bytes Speed: 158724 hashes/s Processing: 620.02 MiB/s
Algo: BLAKE2S_224 Hash size: 28 bytes Speed: 158736 hashes/s Processing: 620.06 MiB/s
Algo: BLAKE2S_256 Hash size: 32 bytes Speed: 158570 hashes/s Processing: 619.41 MiB/s
Algo: SM3 Hash size: 32 bytes Speed: 80384 hashes/s Processing: 314.00 MiB/s
Algo: XXH32 Hash size: 4 bytes Speed: 1655457 hashes/s Processing: 6.32 GiB/s
Algo: XXH64 Hash size: 8 bytes Speed: 3051808 hashes/s Processing: 11.64 GiB/s
Algo: XXH3LOW Hash size: 4 bytes Speed: 5754768 hashes/s Processing: 21.95 GiB/s
Algo: XXH3 Hash size: 8 bytes Speed: 5753291 hashes/s Processing: 21.95 GiB/s
Algo: XXH128 Hash size: 16 bytes Speed: 5525476 hashes/s Processing: 21.08 GiB/s
```

## Block-based backup with rotation, compression and data deduplication

#### Why block-based backup

Block-level backups are used in environments where there is a need to backup entire disks, selected partitions or virtual disk images, preserving the structure of the underlying file system. It allows you to backups independently of the used file system offering more flexibility compared to file synchronization. When synchronizing at the file system level, important metadata can be omitted. While basic POSIX files permissions and attributes can be copied during file-based sync, the preservation of extended file attributes (xattr) can be more challenging. In addition there are more file system features stored in metadata like subvolumes and qgroups in the BTRFS or datasets in the case of ZFS and others whose structure must be specially dumped. The advantage of block-based backup is the ability to quickly restore the full structure of the file system in the event of a failure, along with all data, as well as selectively extract part of the data from the backup due to the ability of mount disk image.

#### BTRFS as backup repository

[BTRFS](https://github.com/btrfs/linux) is a Copy-on-Write file system with snapshots and transparent data compression. It is a very well suited for storing deduplicated copies of disk images from many backup cycles, minimizing the occupied disk space.

#### BTRFS compression mount option

BTRFS supports multiple algorithms with various levels of compression. Zstandard (ZSTD) is an algorithm with a good ratio and compression and decompression speeds.

Compression support should be added to the BTRFS file system mounting option in the backup repository:

```console
compress-force=zstd:3
```

#### Local mounted repository

Locally mounted repositories can be disks that are physically located on the server or disks located in a different physical location. The Storage Area Network (SAN) allows direct access to storage over the network. Examples of such solutions are Fiber Channel, iSCSI, FCoE, InfiniBand, AoE. The SAN accesses the device at the block level and mounts it as a native drive whose file system is maintained at the local machine level.

#### Network mounted repository

In the case of using repositories mounted as network resources, a file operations are handled through an additional protocol. Configuration and maintenance of the native file system is done on the remote host that hosts the disk device. Examples of Network Attached Storage (NAS) are NFS or SAMBA/CIFS. Mounting repository via SSHFS (FUSE-based filesystem) can also be applied to this group. Blocksync-fast also synchronizes blocks of files on devices mounted as network resources.

#### Preparing mounted volume to backup

During the process of syncing blocks from an active partition, any changes that will occur in the partition's file system may lead to inconsistency in the disk image backup. To ensure the integrity and consistency of the backup data, it is recommended to freeze the copied file system for the time of sync.
Recommended solution is to use LVM for partition management and take a snapshot of volume. Alternative methods are use fsfreeze on the copied file system or pause, suspend or halt the virtual machine where the partition is mounted.

#### Deduplication on BTRFS repository

File system deduplication allows you to store the same data in different files by referencing common blocks of data in the file's metadata to reduce physical disk usage.
Blocksync-fast during synchronization will overwrite only those blocks that have changed since the last synchronization. In order to share data blocks in the repository from different synchronization cycles, copy the current version of the disk image using the Copy-On-Write mechanism.

Copy with reflink runs on a locally mounted partition or mounted as network repository including NFS 4.2 or SMB 3.

```console
$ blocksync-fast -s /dev/vol -d /mnt/backups/vol-current.img -f /var/cache/backups/vol.digest
$ cp --reflink vol-current.img vol-2024-05-07-00.img
```

Alternatively, deduplication can also be made by creating snapshots in the BTRFS repository. Snapshots can be only taken on the host where the BTRFS partition is mounted locally.

```console
$ blocksync-fast -s /dev/vol1 -d /mnt/backups/current/vol1.img -f /var/cache/backups/vol1.digest
$ blocksync-fast -s /dev/vol2 -d /mnt/backups/current/vol2.img -f /var/cache/backups/vol2.digest
$ btrfs subvolume snapshot /mnt/backups/current/ /mnt/backups/2024-05-07-00/
```

#### Deduplication without Copy-on-Write repository

On repositories with a file systems without copy-on-write you can store delta files containing only changed blocks instead of storing full disk images from different cycles. Delta files should be treated as separate patches that should be applied to the full disk image in the correct order.
The disadvantage of this solution is the lack of direct access to the full disk image for each cycle except the oldest without applying appropriate patches.

Files in repository from 7 days rotation cycles:

```console
vol-2024-05-01-00.img - full image from oldest cycle
vol-2024-05-02-00.delta
vol-2024-05-03-00.delta
vol-2024-05-04-00.delta
vol-2024-05-05-00.delta
vol-2024-05-06-00.delta
vol-2024-05-07-00.delta - patch from the latest cycle
```

Deleting the oldest copy during rotation:

```console
$ blocksync-fast -d vol-2024-05-02-00.img --apply-delta -D vol-2024-05-01-00.delta
$ mv vol-2024-05-01-00.img vol-2024-05-02-00.img
```

Creating the newest copy during rotation:

```console
$ blocksync-fast -s /dev/vol -f /var/cache/backups/vol.digest --make-delta -D vol-2024-05-08-00.delta
```

Restore disk image vol-2024-05-07-01.img:

```console
$ cp vol-2024-05-02-00.img vol-2024-05-06-00.img
$ blocksync-fast -d vol-2024-05-06-00.img --apply-delta -D vol-2024-05-03-00.delta
$ blocksync-fast -d vol-2024-05-06-00.img --apply-delta -D vol-2024-05-04-00.delta
$ blocksync-fast -d vol-2024-05-06-00.img --apply-delta -D vol-2024-05-05-00.delta
$ blocksync-fast -d vol-2024-05-06-00.img --apply-delta -D vol-2024-05-06-00.delta
$ blocksync-fast -d vol-2024-05-06-00.img --apply-delta -D vol-2024-05-07-00.delta
```

## DM-era support

Linux [device-mapper era](https://docs.kernel.org/admin-guide/device-mapper/era.html)
tracks writes to a mapped device. `era_invalidate` from
[thin-provisioning-tools](https://github.com/jthornber/thin-provisioning-tools)
exports the changed block indices as XML. Sven-Ola Tuecke's `--era` support lets
`--make-digest` and `--make-delta` skip source ranges absent from that list.
Block-sync, apply-delta and queued reads do not accept `--era`.

Use `-e PATH` or `--era=PATH` for the XML file, or `--era=-` for XML on stdin.
`-E N` or `--era-sectors=N` specifies the mapping's tracking block size in
512-byte sectors (default: 4096 sectors, or 2 MiB). This must match the actual
mapping; the XML does not contain its block size. The hash block size remains
independent. Selected era ranges expand to all intersecting hash blocks.

A complete, compatible, named baseline digest is required, including for empty
change lists. Generate a full baseline first; missing, truncated, resized or
incompatible baselines are rejected before rewriting the digest, even with
`--force`. The XML must describe every change since that baseline for the same
source device. Writes bypassing the era mapping and discard/TRIM changes are
not reliably covered: omit `--era` and perform a full scan after such changes.
A stopped workload or stable snapshot is still needed for a consistent backup.

The accepted XML schema is `<blocks>` containing self-closing
`<block block="N"/>` and `<range begin="N" end="N"/>` elements; range ends are
exclusive. Whitespace, comments, an optional XML declaration, either quote style,
and either attribute order are accepted. Unsorted/overlapping ranges are merged.
Malformed XML, missing closing tags, negative/overflowing indices, unknown
attributes and ranges outside source capacity are rejected before output changes.
Input is limited to 16 MiB; entities and DTDs are unsupported.

For an existing era mapping named `test-era` using metadata device `test-meta`,
a full baseline backup can be created while the workload is stopped:

```sh
# Record the current era from dmsetup status alongside this baseline.
dmsetup message test-era 0 checkpoint
dmsetup status test-era
blocksync-fast -s /dev/mapper/test-era -d backup.img -f baseline.digest
```

Before a later incremental backup, stop the workload again. Set `BASELINE_ERA`
to the recorded era number, then export the held metadata snapshot and generate
the delta (this example assumes the mapping uses 4096-sector tracking blocks):

```sh
dmsetup message test-era 0 take_metadata_snap
era_invalidate --metadata-snapshot --written-since "$BASELINE_ERA" \
  /dev/mapper/test-meta > changes.xml
dmsetup message test-era 0 drop_metadata_snap
cp baseline.digest baseline.digest.previous
blocksync-fast --make-delta -s /dev/mapper/test-era -f baseline.digest \
  --era=changes.xml --era-sectors=4096 -D next.delta
blocksync-fast --apply-delta -d backup.img -D next.delta
```

Delta generation updates the digest before delta application. Keep the previous
digest and era number until the delta has been safely stored and applied; if
application fails, restore the previous digest before regenerating a delta.
Do not advance the recorded era past writes not represented in the completed
backup. Check the current era via `dmsetup status`; a checkpoint does not
necessarily increment it.

Digest updates persist `<digest>.incomplete` before mutation and remove it after
output synchronization. Failed or interrupted era updates require recovery with
a full baseline; selective retries refuse the incomplete marker. Preserve the
previous baseline, or rebuild the backup and digest in full. To restore the
previous baseline, first move the damaged digest and its marker out of the way
together, then restore the saved digest and use its original era number.
Keep any incomplete
marker with its digest. `--dont-write` supports non-mutating era digest checks;
era delta generation rejects write-suppression options. `--no-compare` is also
rejected because skipping unlisted ranges requires trusting the baseline.

## Limitations and Notes

Blocksync-fast is a tool for fast synchronization of block devices, designed to improve block-based backups. To use it as an automatic backup tool, it is recommended to include it in a BASH script, which will allow you to set up a solution adjusted to your individual needs, using various features and tools available in Linux. It should be taken into account the possibility of synchronization interruption due to network disconnection, device detachment, or other errors that may occur. In case of synchronization failure, the program will return an error code greater than 0, which should be handled in the BASH shell and appropriate actions, such as generating reports or retrying the synchronization, should be taken. Interrupted block-sync runs retain an incomplete marker and recover automatically by comparing destination bytes on the next run. Interrupted DM-era digest/delta updates require a complete baseline rebuild or restoration of the previous baseline; see the DM-era section above.

An example backup script with GFS rotation scheme can be found in [scripts directory](https://www.github.com/nethappen/blocksync-fast/tree/main/scripts).

## FAQ

<details>
<summary>How does this block devices synchronization program work ?</summary>
<br>
The program works by comparing the data of the source device (src) with the target device (dst) at the level of data blocks and copying only changed blocks, which allows for fast and file system independent synchronization. Additionally, the program can create and use Digest files that store checksums of blocks to speed up synchronization process by eliminating data reading from the target device.

</details>

<details>
<summary>What are the main uses of this program ?</summary>
<br>
The program can be used for simple cloning of entire disks or partitions as well as their re-synchronization. The main purpose of the program is block-based backup.

</details>

<details>
<summary>Does the program allow you to synchronize blocks between different devices ?</summary>
<br>
You can synchronize data between all devices containing data blocks, both block devices or regular files stored in the file system. Source and destination can be different types.

</details>

<details>
<summary>What is a Digest file ?</summary>
<br>
The Digest file is generated during block devices synchronization. It stores checksums of blocks copied to the target device, which will be used during the next synchronization to compare the data with the blocks of the source device. This eliminates the need to read blocks from the target device, access to which may be slower. Digest file also store synchronization parameters such as block size or the algorithm used to calculate checksums.

</details>

<details>
<summary>What is a Delta file ?</summary>
<br>
The Delta file is created as a result of synchronization between the source device and the Digest file, which reflects the state of the target device's blocks. The Delta file contains data only of those blocks that are needed to update the target device. Thanks to this process, it is possible to synchronize and transfer data to a remote server and store incremental copies of data.

</details>

<details>
<summary>How can I monitor the progress of block copying ?</summary>
<br>
The "--progress" should be added as an argument of the program. Also "--progres-detail" can be used to monitor detailed activity.

</details>

<details>
<summary>What data block size is used during synchronization ?</summary>
<br>
The default block size is 4KB, which is the preferred block size of most file systems. You can adjust the block size by the "--block-size" parameter without any restrictions, but there is no advantage in setting this size below the underlying filesystem block size.

</details>

<details>
<summary>Can I synchronize block devices via the network ?</summary>
<br>
Yes, however the application itself does not have a built-in client-server option running on its own protocol. You must ensure that the remote disk is mounted via SAN or NAS yourself. You can also use and copy Delta files via pipelining and redirect stdout to stdin on a remote server via SSH.

</details>

<details>
<summary>How to restore data from a backup ?</summary>
<br>
To restore data from a backup, you must reverse the process and specify the source device as the backup file and the target device to which the data is to be restored as parameters. If there are also Delta files in the repository, restore the base copy of the disk image first, then sequentially apply the Delta files in the correct order, starting with the oldest.

If you need to extract only some of the files from the backup, you can directly mount the backup disk image to access the file system: "mount -o loop backup.img /mnt/restore". In the case of an incremental backup, Delta files must be applied to the base copy of the disk.

</details>

<details>
<summary>What are the differences between this tool and other block syncing tools or backup programs ?</summary>
<br>
Blocksync-fast was created for fast and efficient synchronization of block devices. It is written in the C language and it uses low-level operations and directly calls system functions allowing for direct communication with block devices, which translates into effective work and fast data processing. The program minimizes resource consumption through effective memory management, and thanks to the use of a data buffer, it achieves high performance when working on small data blocks.

The program allows you to generate Digest files that significantly speed up the synchronization process by tracking changes and differences between data copies. In addition, Delta files, which contain only differences between data versions, enable quick copying, updating and remote synchronization, as well as data storage in the form of incremental backup.

Blocksync-fast is simple to use and provides you the freedom to build your own script-based backup solution using the Linux tools. It allows you to store data as raw disk image and is not limited to specific repository types like other complex backup programs. You can freely utilize modern file system mechanisms, such as BTRFS, to effectively store data with deduplication and compression.

</details>

## Licensing

Blocksync-fast is licensed under the Apache License, Version 2.0. See [LICENSE](https://www.github.com/nethappen/blocksync-fast/tree/main/LICENSE) for the full license text.
