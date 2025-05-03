
# Change Log
All notable changes to this project will be documented in this file.
 
The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

## [1.0.7] - 2025-05-03
### Fixed
- Fixed compilation issues when using GCC 15.


## [1.0.6] - 2024-09-05
### Added
- Streams: allows use STDOUT for digest data in Make-digest operation
### Fixed
- Improved STDIN stream handling: the program now notifies if the input size exceeds or is smaller than the declared limit.
- Minor corrections


## [1.0.5] - 2024-08-29
### Fixed
- Fixed issues with detection of the `libgcrypt` library during configuration


## [1.0.4] - 2024-08-06
### Added
- Streams: allows use STDIN as source image
- Options: -S or --size option to specify the size of source data
- Progress: higher percentage precision in tracking progress for large images
- Options: --show-progress as alias --progress
- Options: --show-progress-detail as alias --progress-detail
- Scripts: gfs-lvm-backup.sh with example file


## [1.0.3] - 2023-05-15
### Added
- Options: delta-info and digest-info to check and prints information from file header
- Streams: allows use STDIN and STDOUT for delta data
- Apply-delta: option to apply delta to destination device
- Make-delta: option to create delta from source device and digest
- Delta files: stores only differential changes between source and target device
- Delta files support

 
## [1.0.1] - 2023-04-19
### Added
- Benchmark algos: added option to benchmark all supported hash algorithms
- xxHash library: optional support for fast hashing algos: XXH32, XXH64, XXH3LOW, XXH3, XXH128
- xxHash support added


## [1.0.0] - 2023-03-11
### Added
- Mmap: alternative read/write method
- Digest file: which stores checksums of target device
- Libgcrypt support (1.90): provides multiple hashing algos for digest file
- Block size: adjustable block-size option
- Buffer size: adjustable buffer-size option
- Blocksync operation mode: which compares and syncs the source and target device
- Initial version release
