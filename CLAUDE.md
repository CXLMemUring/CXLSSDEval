# CXLSSDEval Project Overview

## Project Description

This project contains the evaluation framework and paper for **AgileStore**, a novel operating system storage abstraction that treats CXL SSDs as moderately coupled, agility-aware accelerators. The paper is targeted at OSDI-style venues.

## Directory Structure

```
CXLSSDEval/
├── paper/                      # LaTeX paper source
│   ├── main.tex               # Main paper file (contains all sections)
│   ├── eval.tex               # Legacy eval file (not used)
│   ├── cite.bib               # Bibliography
│   └── img/                   # Paper figures
│       ├── byte_addressable.pdf
│       ├── thermal_throttling.pdf
│       ├── blocksize_comparison.pdf
│       ├── qd_scalability.pdf
│       ├── access_pattern.pdf
│       ├── rwmix_performance.pdf
│       ├── cmb_bandwidth.pdf
│       ├── pmr_latency_cdf.pdf
│       ├── compression_comparison.pdf
│       ├── stream.pdf
│       ├── deepseek_cxl_ssd.pdf
│       └── windows.pdf        # WASM runtime comparison
│
├── scripts/                    # Benchmarking and plotting scripts
│   ├── CLAUDE.md              # Scripts documentation
│   ├── config.yaml            # Test configuration
│   ├── run_all_tests.sh       # Master test orchestration
│   │
│   ├── plot_*.py              # Plotting scripts (all use 20pt fonts)
│   │   ├── plot_byte_addressable.py
│   │   ├── plot_thermal_throttling.py
│   │   ├── plot_blocksize.py
│   │   ├── plot_qd_scalability.py
│   │   ├── plot_access_pattern.py
│   │   ├── plot_rwmix.py
│   │   ├── plot_cmb_bandwidth.py
│   │   ├── plot_pmr_latency_cdf.py
│   │   ├── plot_compression_comparison.py
│   │   └── plot_windows.py
│   │
│   ├── *_raw/                 # Raw benchmark data directories
│   │   ├── samsung_raw/       # Samsung SmartSSD results
│   │   ├── scala_raw/         # ScaleFlux CSD1000 results
│   │   ├── cxl_raw/           # CXL SSD results
│   │   └── true_cxl_raw/      # Additional CXL data
│   │
│   └── *_byte_addressable_result/  # Byte-addressable test summaries
│
└── claude.md                   # This file
```

## Paper Structure

1. **Introduction** - Motivation and contributions
2. **Background and Motivation** - CXL overview, prior work limitations
3. **AgileStore Design** - Architecture, actors, WASM execution, migration
4. **Implementation** - RTL, host integration, MONITOR/MWAIT, MVVM runtime
5. **Evaluation** - Comprehensive benchmarks across three platforms
6. **Discussion** - Design lessons and OLAP relevance
7. **Related Work** - Persistent memory, CSDs, CXL memory
8. **Conclusion**

## Evaluation Figures

| Figure | Script | Description |
|--------|--------|-------------|
| byte_addressable.pdf | plot_byte_addressable.py | Sub-512B I/O performance comparison |
| thermal_throttling.pdf | plot_thermal_throttling.py | Throughput/temperature over time |
| blocksize_comparison.pdf | plot_blocksize.py | Sequential R/W vs block size |
| qd_scalability.pdf | plot_qd_scalability.py | IOPS scaling with queue depth |
| access_pattern.pdf | plot_access_pattern.py | Sequential vs random 4KB |
| rwmix_performance.pdf | plot_rwmix.py | Read/write mix throughput |
| cmb_bandwidth.pdf | plot_cmb_bandwidth.py | CMB bandwidth and CPU utilization |
| pmr_latency_cdf.pdf | plot_pmr_latency_cdf.py | PMR access latency CDF |
| compression_comparison.pdf | plot_compression_comparison.py | Compression ratio and overhead |
| windows.pdf | plot_windows.py | MVVM vs Native runtime |
| stream.pdf | (external) | STREAM HPC with nvmex |
| deepseek_cxl_ssd.pdf | (external) | DeepSeek LLM inference |

## Key Performance Numbers

### Byte-Addressable Performance (8B writes)
- Samsung SmartSSD: 0.20 MB/s, 38 µs latency
- CXL SSD: 14.27 MB/s, 0.13 µs latency
- **Improvement: 71× bandwidth, 292× latency**

### Queue Depth Scalability (QD=32)
- Samsung SmartSSD: 384K read IOPS, 350K write IOPS
- CXL SSD: 652K read IOPS, 577K write IOPS
- **Improvement: 1.7× read, 1.65× write**

### Block Size Performance (256KB)
- Samsung SmartSSD: 1398 MB/s
- CXL SSD: 2524 MB/s
- **Improvement: 1.8×**

### PMR Latency
- CXL PMR: ~750 ns median
- Traditional PCIe BAR: ~9 µs median
- **Improvement: 10.9×**

## TODO Items in main.tex (需要手动填写)

以下TODO项需要您根据实际数据填写:

| 位置 | TODO | 说明 |
|------|------|------|
| Fig 6 caption & text | `\todo{X$\times$}` | Access pattern: CXL SSD seq/rand gap缩小到多少倍 |
| Fig 7 caption & text | `\todo{Y\%}` | RWMIX: CXL SSD在50/50 mix时保持峰值性能的百分比 |

## Regenerating Figures

All plotting scripts are configured with 20pt minimum font size. To regenerate:

```bash
cd /root/CXLSSDEval/scripts
python3 plot_byte_addressable.py
python3 plot_thermal_throttling.py
python3 plot_blocksize.py
python3 plot_qd_scalability.py
python3 plot_access_pattern.py
python3 plot_rwmix.py
python3 plot_cmb_bandwidth.py
python3 plot_pmr_latency_cdf.py
python3 plot_compression_comparison.py
python3 plot_windows.py

# Copy to paper directory
cp /root/img/*.pdf /root/CXLSSDEval/paper/img/
```

## Data Sources

- **samsung_raw/**: Samsung SmartSSD FIO benchmark results
- **scala_raw/**: ScaleFlux CSD1000 FIO benchmark results
- **cxl_raw/**: CXL SSD FIO benchmark results
- **samsung_byte_addressable_result/**: Samsung sub-512B test CSV
- **cxl_byte_addresable_result/**: CXL sub-512B test CSV (note: typo in directory name)
- **data.csv**: MVVM vs Native runtime benchmark data

## Notes

- Font: Scripts use Helvetica (fallback to system default if unavailable)
- All figures use minimum 20pt font size for readability in double-column format

## 已完成的修改

1. **修复了大部分eval TODO项**
   - 256KB throughput: 1.8×
   - QD scalability: QD=32, 652K read, 577K write IOPS
   - Distribution parameters: σ=20%, α=1.16
   - PMR latency: 750ns, 32GB, 10.9×
   - Compression thresholds: 80%, 40%, 95%

2. **调整了所有作图脚本字体为20pt**

3. **保留了两个TODO供您填写正确数据**
   - Access pattern seq/rand gap
   - RWMIX 50/50 mix performance percentage
