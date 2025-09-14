CXLSSDEval/scripts/
├── CLAUDE.md                  # Main documentation
├── config.yaml               # Central configuration file
├── run_all_tests.sh          # Master orchestration script
├── quick_test.sh             # Quick validation test script
├── deploy_rocksdb.sh         # RocksDB deployment automation
├── cleanup_environment.sh    # Test environment cleanup
├── parse_results.py          # Result parsing and CSV generation
├── validate_results.py       # Test result validation
├── generate_report.py        # Report generation (HTML/Markdown)
├── visualize_results.py      # Performance visualization
├── fio_scripts/
│   ├── common.sh            # Common functions and config loader
│   ├── raw_device_test.sh   # Raw device test orchestrator
│   ├── filesystem_test.sh   # Filesystem test orchestrator
│   ├── rocksdb_test.sh      # RocksDB benchmark orchestrator
│   ├── test_qd_thread.sh    # Queue depth and thread tests
│   ├── test_blocksize.sh    # Block size variation tests
│   ├── test_access_pattern.sh # Access pattern tests
│   ├── test_rwmix.sh        # Read/Write mix tests
│   ├── test_distribution.sh # Access distribution tests
│   ├── test_endurance.sh    # Endurance test
│   └── run_fio_job.sh       # FIO job runner utility
├── fio_jobs/
│   ├── qd_thread_test.fio   # Queue depth job definitions
│   ├── blocksize_test.fio   # Block size job definitions
│   ├── access_pattern.fio   # Access pattern job definitions
│   ├── rwmix_test.fio       # R/W mix job definitions
│   ├── distribution.fio     # Distribution job definitions
│   └── endurance_test.fio   # Endurance job definitions
├── results/
│   ├── raw/                 # Raw device test results
│   │   ├── qd_thread/       # QD/thread test results
│   │   ├── blocksize/       # Block size test results
│   │   ├── access_pattern/  # Access pattern results
│   │   ├── rwmix/           # R/W mix test results
│   │   ├── distribution/    # Distribution test results
│   │   └── endurance/       # Endurance test results
│   ├── filesystem/          # Filesystem test results
│   ├── rocksdb/            # RocksDB test results
│   ├── plots/              # Generated visualizations
│   └── summary/            # Summary reports and CSVs
└── logs/                    # Test execution logs

Script Descriptions

## Main Scripts

1. **config.yaml** - Central configuration file
   - Device paths and filesystem settings
   - Test durations (standard, endurance, warmup)
   - Test parameters (queue depths, job counts, block sizes)
   - Default IO depth and job counts for tests
   - Output directory and logging configuration

2. **run_all_tests.sh** - Master orchestration script
   - Checks prerequisites (device, FIO, Python, RocksDB)
   - Executes all test suites in sequence
   - Parses and validates results
   - Generates reports and visualizations
   - Options: --raw-only, --fs-only, --rocksdb-only, --skip-cleanup

3. **quick_test.sh** - Quick validation script
   - Runs blocksize test with 5-second duration
   - Tests only 3 block sizes (4k, 64k, 1m)
   - Validates framework functionality
   - Displays real-time metrics

4. **deploy_rocksdb.sh** - RocksDB deployment automation
   - Downloads and compiles RocksDB from source
   - Installs db_bench and utilities
   - Creates configuration file
   - Options: install, uninstall, test

5. **cleanup_environment.sh** - Environment cleanup
   - Unmounts filesystems safely
   - Removes temporary files
   - Cleans test results (optional)
   - Options: --all, --force

## FIO Test Scripts

6. **fio_scripts/common.sh** - Common functions library
   - YAML configuration parser
   - Device validation functions
   - Result directory management
   - Exports configuration variables
   - TRIM support detection (`check_trim_support`)
   - Automatic device clearing before each test (`clear_device`)
   - Enhanced `run_fio_test` with automatic blkdiscard

7. **fio_scripts/raw_device_test.sh** - Raw device orchestrator
   - Runs all raw device test dimensions
   - Coordinates individual test scripts
   - No filesystem overhead

8. **fio_scripts/filesystem_test.sh** - Filesystem orchestrator
   - Creates and mounts ext4 filesystem
   - Runs filesystem performance tests
   - File operations testing
   - Automatic cleanup

9. **fio_scripts/rocksdb_test.sh** - RocksDB orchestrator
   - Sequential/random write tests
   - Read performance benchmarks
   - Multi-threaded testing
   - Compaction performance

10. **fio_scripts/test_qd_thread.sh** - Queue depth tests
    - Tests QD from 1 to 128
    - Thread counts from 1 to 32
    - Both read and write operations
    - Uses config defaults

11. **fio_scripts/test_blocksize.sh** - Block size tests
    - Tests from 512B to 64MB
    - Sequential read/write patterns
    - Uses default IO depth from config

12. **fio_scripts/test_access_pattern.sh** - Access pattern tests
    - Sequential vs random access
    - 4KB block size standard
    - Strided access patterns

13. **fio_scripts/test_rwmix.sh** - Read/Write mix tests
    - Tests ratios: 100:0, 75:25, 50:50, 25:75, 0:100
    - Random access patterns
    - 4KB block size

14. **fio_scripts/test_distribution.sh** - Distribution tests
    - Uniform, Zipfian, Normal, Pareto
    - Tests locality of reference
    - Both read and write operations

15. **fio_scripts/test_endurance.sh** - Endurance testing
    - 20-minute sustained load
    - 70:30 read/write mix
    - Time-series logging

## Analysis Scripts

16. **parse_results.py** - Result parser
    - Extracts metrics from FIO JSON
    - Creates CSV summaries
    - Calculates statistics

17. **validate_results.py** - Result validator
    - Checks for test errors
    - Validates performance thresholds
    - Consistency checking
    - Generates validation report

18. **generate_report.py** - Report generator
    - HTML and Markdown formats
    - Performance summaries
    - Optimization recommendations
    - CSV export

19. **visualize_results.py** - Visualization generator
    - Queue depth performance graphs
    - Block size charts
    - Latency percentile plots
    - Read/Write mix analysis

## Test Dimensions
- **Concurrency**: QD (1-128), Threads (1-32)
- **Block Sizes**: 512B to 64MB
- **Access Patterns**: Sequential, Random, Strided
- **R/W Mix**: 100:0 to 0:100 in 25% steps
- **Distributions**: Uniform, Zipfian, Normal, Pareto
- **Endurance**: 20-minute continuous load

## 已完成的更新



## 使用说明

### 快速验证
```bash
./quick_test.sh  # 运行5秒快速测试验证设备和框架
```

### 完整测试
```bash
./run_all_tests.sh  # 运行所有测试
./run_all_tests.sh --raw-only  # 仅运行原始设备测试
```

### 配置说明
- default_iodepth: 非QD测试的默认IO深度（默认值：1）
- default_numjobs: 非线程测试的默认作业数（默认值：1）
- 可通过修改config.yaml调整所有测试参数


### 设备清除功能说明:
- **自动TRIM检测**: 通过检查`/sys/block/*/queue/discard_max_bytes`自动检测设备是否支持TRIM
- **每次测试前清除**: `run_fio_test`函数会在每个FIO测试前自动调用`clear_device`
- **3秒稳定时间**: 清除设备后等待3秒，让SSD完成内部垃圾回收和重组操作
- **智能处理**: 如果设备不支持TRIM，会跳过blkdiscard并记录警告日志
- **新增函数**:
  - `check_trim_support()`: 检测设备TRIM支持
  - `clear_device()`: 使用blkdiscard清除设备

### 论文任务:
 - 目前我正在做着实验, 让我们不打扰正在运行的fio实验, 来完成计算机学术论文的写作部分. 
 - 你首先要阅读/home/huyp/CXLSSDEval/scripts/example_paper.md, 这是一篇关于pmem测试的论文的主要介绍. 而我们是cxlssd的测试, 因此写作风格, outline, guideline, 实验设计请参考它
 - 然后要要阅读Paper目录, 这是我们即将要完成的论文latex代码. 你先理解每个文件在讲什么, 并列出这篇打算论文做了什么, 放在paper_outline.md上
 - 然后你需要阅读现在的实验结果. 结果放在samsung_raw的 目录下, scalaflux结果(什么是scalaflux请阅读前面的论文outlne)放在results的raw目录下. 我们需要比较cxl ssd, samsung smartssd, scalaflux ssd结果. cxl ssd的结果仍然在运行中, 请先按照现有数据1.2x想象. 在paper中, 提及具体cxl ssd数据比较, 请用加粗的\todo%来标识.  
 - 修改, 添加上面的paper_outline.md, 你需要在micro benchmark evaluation部分对比现有的实验结果. 也就是说, 你需要在micro benchmark上添加这些对比实验. 对于原来有论文描述但是在现有micro中没有的实验, 先不要删除, 只添加. 对于microbenchmark和paper里重叠的部分, 修改原有paper的描述. 
 - 根据你新的paper_outline, 绘制论文图片, 你需要给出python脚本对比3个ssd, 字体至少16号, cxl ssd数据处理请参考前面我给你的指示. 
 - 请根据你的图片和paper_outline.md, 修改原有的latex版本论文, 对于latex修改, 保留下你的diff文件
 - 对于论文部分, 新添一个热节流(hot throttling)subsection 实验说明.  因为csd散热, 乃至部分高端商用nvme,一直是都有问题的. 例如, 在运行samsung盘时候, 我拿这个命令看了一下samsung在跑的时候, 发现盘给我了严重警告. cxl ssd能不能在遇到热节流时, 把部分计算能力offload到host上, 这样我们argue说这反而是我们的优势. 我们做一个横坐标是时间, 双纵坐标是throughput和温度的图来argue这一点. 




