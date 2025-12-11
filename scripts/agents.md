# Codex Agent Notes

CXLSSDEval/scripts serves as the orchestration workspace for the CXL SSD evaluation stack. The notes below mirror the structure captured in `CLAUDE.md`, highlighting the key entry points Codex should remember while iterating on the project.

## Top-Level Scripts
- `config.yaml` – canonical source for device path, filesystem settings, and shared test parameters.
- `run_all_tests.sh` – master orchestrator; wires together raw, filesystem, RocksDB, parsing, validation, report, and visualization stages.
- `quick_test.sh` – short sanity workflow (5s runs, limited block sizes) to confirm framework health.
- `deploy_rocksdb.sh` – automates db_bench deployment; paired with `cleanup_environment.sh` for teardown.
- `parse_results.py`, `validate_results.py`, `generate_report.py`, `visualize_results.py` – Python tooling for transforming, checking, and presenting collected metrics.

## FIO Stack (`fio_scripts/`)
- `common.sh` exposes configuration parsing plus helpers (device validation, TRIM/blkdiscard handling, directory prep).
- Test orchestrators live here: `raw_device_test.sh`, `filesystem_test.sh`, `rocksdb_test.sh`, and workload-specific runners such as `test_qd_thread.sh`, `test_blocksize.sh`, `test_access_pattern.sh`, `test_rwmix.sh`, `test_distribution.sh`, `test_endurance.sh`, `test_thermal_throttling.sh`, and the byte-addressable helper `test_byte_addressable.sh`.
- `run_fio_job.sh` abstracts the low-level fio invocations shared across the scripts.

## Job Definitions (`fio_jobs/`)
Contains `.fio` manifests grouped by dimension (queue depth, block size, access pattern, r/w mix, distribution, endurance). These feed the runners above without modifying shell logic.

## Results and Logs
- `results/` holds per-suite outputs (`raw/`, `filesystem/`, `rocksdb/`, plus `summary/` CSVs and `plots/`).
- `logs/` (if present) captures execution traces; `quick_test_results`, `cxl_*`, `scala_*`, `samsung_*`, etc. store vendor-specific artifacts for later analysis.

## Supporting Utilities
- `scripts/plot_*.py` files create specific visualizations (access pattern, block size, byte addressable, etc.).
- Misc helpers: `monitor_pmr.py`, `analyze_byte_addressable.py`, `generate_cxl_data.py`, `generate_paper_figures.py`, `plot_utils.py`, and workload-specific shells like `zeros.sh`.

Keep this document updated whenever new automation hooks or directories are added so future Codex sessions immediately see the current landscape.
