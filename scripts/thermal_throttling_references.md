下面把“计算型 SSD（CSD）/高功耗 SSD 的热节流是否更严重、严重到什么程度、用专用风扇是否能解决”逐条说清楚，并在每一条后给出英文权威参考链接（均为公开规范、白皮书或厂商/学术资料）。

---

# 结论先行

* **会更严重**：CSD 与高功耗 SSD 的热风险显著高于传统 10–14 W 等级的企业 NVMe。原因是**更高的功耗上限（25–70 W）**、**更高的持续带宽**以及**设备内新增“计算”部件（如 FPGA/SoC）**带来的额外热源与**二次节流/关断回路**。
* **严重到的程度**：在散热设计不足时，这类设备不仅触发 NVMe 层面的 77 °C 复合温度节流，**还可能触发加速器（FPGA）降频乃至时钟停摆与整卡关断**，出现**PCIe 断链并需重启恢复**的服务中断。
* **但可工程化解决**：只要**按规范给足风量/风速并控制进风温度**，热节流在数据中心应属“异常工况下才会触发”的保护手段；E1.S/E3.S 等新形态专为更高功耗与更好风道而设计，可将风险控制在可接受范围。下文给出可执行的风量/风速与测试条件。

---

# 逐条论点 + 证据

## 1) CSD/高功耗 SSD 的**功耗档位更高**，先天热负载更大

* **OCP 数据中心 NVMe 规范**要求设备提供至少至 **35 W** 的运行功耗档位，还允许更高档位；并明确热节流与关断的温度门槛（见条目 2）。
* **EDSFF E3** 形态（新一代 2U 主流）**电源画像可到 70 W**，以支持更大主控/更多 NAND 及更高带宽。([KIOXIA America, Inc.][1])
* 老牌“高功耗”代表：**Intel P3608 AIC** 默认 **40 W**（可限到 35/25 W），且给出风量需求（见条目 3）。这类卡本质上已接近小型加速卡的热密度。([Intel][2])

## 2) **温度触发点**更容易被碰到，且 CSD 还有**第二层节流/关断**

* **OCP NVMe Cloud SSD v1.0.3**：

  * 热节流（Thermal Throttling）统一以**复合温度 77 °C**为触发点；
  * **热关断阈值 ≥ 85 °C**；
  * 规范还明确：热节流**只应在异常情形**（高进风温、风扇失效等）下发生。
* **CSD（以 Samsung/AMD SmartSSD 为例）**：除 SSD 自身的温控外，板上 **FPGA 有独立的“降频/停钟/整卡关断”保护链路**：

  * **FPGA 降频起点 93 °C**（并有 25 W 功率节流）；
  * **FPGA 时钟停摆 97 °C** → 触发 AXI 防火墙、驱动重置，**应用可能崩溃**，卡需数分钟恢复；
  * **整卡关断 100 °C** → **PCIe 断链，需冷重启**。
    这说明 CSD 的**热后果严重性**明显强于“仅限 NVMe 降速”的传统 SSD。([AMD Documentation][3])

## 3) **要不要专用风扇？要**——并且需要按功耗给出**明确风量/风速**

* **U.2 高功耗 NVMe（Micron 9300）**：要求**约 450 LFM** 的顺流风速（25 °C 进风），且 **SMART 温度 > 75 °C 即进入节流**。这给了工程侧很直观的“给风即保性能”的标尺。([Stebis Brochure][4])
* **E1.S 25 W 形态（Western Digital 演讲稿公式化给出）**：在 **35 °C 进风**下，

  * 20 W 等级需 **≈ 2.02 CFM/盘**；
  * **25 W 等级需 ≈ 4.1 CFM/盘**。
    这就是配置风扇曲线或“每列风道”设计的直接输入。([146a55aca6f00848c565-a7635525d40ac1c70300198708936b4e.ssl.cf1.rackcdn.com][5])
* **高功耗 AIC（Intel P3608 40 W）**：明确列出**400 LFM** 等级的入风需求，并支持把卡限功到 35/25 W 对应降低热负载与性能。([Intel][2])
* **OCP 平台热验证条件**：要求在**35 °C 进风、1.5 m/s（≈ 300 LFM）最差风速**下完成整机热验证；并给出**海拔去密度折算**（> 6000 ft 需按 0.9 °C/1000 ft 降额）。这从平台侧保证“给风给到位”。

> 小结：**有专用风扇且风道设计达标**（风量/风速/进风温受控）时，哪怕 25–40 W 级别的 NVMe 与 CSD，**热节流在常态下不应发生**；一旦发生，通常意味着**超环温、风扇失效、风道被阻**等异常，需要排障而非“接受降速”。

## 4) **EDSFF（E1.S/E3）就是为“更高功耗+更易散热”而来**

* **E1.S/E3** 由 SNIA/PCI-SIG/OCP 牵头定义，目标就包括**更好的风道与更高功耗扩展**；资料显示 **E1.S 15 mm** 方案面向 **25–35 W @ 35 °C** 的使用情景，**E3** 则把功耗天花板推到 **70 W** 等级，并改进机箱风道组织。([SNIA | Experts on Data][6])

## 5) **温度与可靠性**：大规模线上研究确认**温度升高→故障/错误率上升**，而**主动节流**能在高温时显著降低失效率

* **Facebook/CMU 的 SIGMETRICS’15 线上研究**披露：**更高温度与更高故障率相关**；同时在实行更激进节流的平台上，**温度对失效率的影响被抑制**（因为节流降低了写入/访问速率与热应力）。这从“可靠性”角度说明**节流不是“坏事”，而是必要时保护介质**。([Carnegie Mellon University ECE][7])

---

# “严重到什么地步？”的工程量化视图

| 场景                        | 触发条件/指标                                                                                   | 典型后果                            |
| ------------------------- | ----------------------------------------------------------------------------------------- | ------------------------------- |
| **常规 NVMe（按 OCP）**        | 复合温度 **77 °C** 节流；**≥ 85 °C** 允许关断；应仅在异常工况触发                                              | IOPS/带宽被动态下调，温度回落后恢复            |
| **高功耗 U.2/E1.S（25–35 W）** | 给风不足：例如未满足 \*\*450 LFM（U.2）\*\*或 **\~4 CFM/盘（25 W E1.S）**；或进风 > 35 °C                     | 更容易触发 77 °C 节流，持续负载下**带宽/延迟抖动** |
| **CSD（SSD+FPGA/SoC）**     | 除 NVMe 节流外，**FPGA** 温度 **93 °C** 起降频；**97 °C 停钟**→ 驱动复位；**100 °C 整卡关断**→ **PCIe 断链、需冷重启** | **服务中断/作业失败**，恢复耗时分钟级           |

证据：OCP v1.0.3（77/85 °C 及平台热验证条件）、Micron 9300（450 LFM 与 > 75 °C 节流）、WD E1.S（2.02/4.1 CFM）、SmartSSD（93/97/100 °C 阈值与复位机制）。

---

# “专用风扇能否解决？”——可操作的配置建议

1. **按功耗预算风量/风速**

   * U.2 25 W 等级：目标 **≈ 450 LFM** 沿盘流向；进风温\*\*≤ 35 °C\*\* 优先。([Stebis Brochure][4])
   * E1.S 25 W 等级：**≈ 4.1 CFM/盘 @ 35 °C**；20 W 则约 **2.02 CFM/盘**。([146a55aca6f00848c565-a7635525d40ac1c70300198708936b4e.ssl.cf1.rackcdn.com][5])
   * AIC 40 W 等级：**≈ 400 LFM** 入风；必要时启用卡级\*\*功率限幅（如 35/25 W）\*\*对性能做可控折中。([Intel][2])

2. **平台级热验证**

   * 参考 OCP 要求，在**35 °C 进风、1.5 m/s 最差风速**下做整机验证；高海拔场景按 **0.9 °C/1000 ft** 做降额校核。

3. **优先采用 EDSFF（E1.S/E3）风道友好机箱**

   * E1.S 15 mm/带散热片与 E3.S 在 1U/2U 中可显著改善风道与冷却效率，适配 **25–70 W** 设备密度。([PCI-SIG][8])

4. **运行期可观测性**

   * 打开并监控 NVMe **温度 AEN** 与 **热节流计数**（OCP 扩展 SMART 字段已规定），出现事件即视为风道/风扇/挡板等**物理层问题需排障**。

5. **CSD 特有措施**

   * 关注**FPGA 侧温度/功率传感器**与阈值（93/97/100 °C）；必要时**降低 FPGA 目标温度/功率阈值**作为保守策略（SmartSSD 提供配置方法）。([AMD Documentation][3])

---

# 关键参考（英文，逐点支撑）

* **OCP NVMe Cloud SSD Specification v1.0.3**：统一规定**77 °C 节流、≥ 85 °C 关断**，以及**35 °C 进风/1.5 m·s⁻¹**的整机热验证条件与海拔折算；同时强调节流应仅在异常工况触发。
* **Samsung/AMD SmartSSD CSD User Guide (UG1382 v1.3)**：给出 **FPGA 降频 93 °C、时钟停摆 97 °C、整卡关断 100 °C**，及**卡级/SSD 级功率节流协同**与**断链/重启**行为。([AMD Documentation][3])
* **Micron 9300 NVMe Datasheet**：要求**450 LFM** 风速（25 °C 进风），并在 **SMART > 75 °C** 时进入节流。([Stebis Brochure][4])
* **Western Digital（E1.S Airflow vs. Power）**：在 **35 °C 进风**下，**20 W≈2.02 CFM/盘**，**25 W≈4.1 CFM/盘**的定量风量指引。([146a55aca6f00848c565-a7635525d40ac1c70300198708936b4e.ssl.cf1.rackcdn.com][5])
* **KIOXIA E3 形态白皮书/页面**：**E3 最高至 70 W** 的电源画像，面向更高性能/更大主控/更多 NAND 的热-功升级。([KIOXIA America, Inc.][1])
* **Intel SSD DC P3608（40 W AIC）规格书**：40 W 默认功耗、可限到 35/25 W，并给出**约 400 LFM** 的入风要求。([Intel][2])
* **Facebook/CMU SIGMETRICS’15 大规模线上失效研究**：温度升高与失效率上升相关；平台层**主动节流**可显著缓和高温下的失效率攀升。([Carnegie Mellon University ECE][7])
* **（补充）E1.S/E3 形态与“更好散热/更高功耗”的设计初衷**：SNIA/PCI-SIG/OCP 公开材料。([PCI-SIG][8])

---

---


[1]: https://americas.kioxia.com/en-us/business/resources/white-paper/edsff-e3-white-paper.html?utm_source=chatgpt.com "Introducing the EDSFF E3 Family of Form Factors"
[2]: https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/ssd-dc-p3608-spec.pdf "Intel® Solid State Drive Data Center P3608 Series Product Specification"
[3]: https://docs.amd.com/api/khub/documents/i43rDg9_~aZJY4cjgOIrwQ/content "SmartSSD Computational Storage Drive Installation and User Guide"
[4]: https://brochure.stebis.nl/Micron9300.pdf?utm_source=chatgpt.com "Data Sheet Micron 9300 NVMe SSD"
[5]: https://146a55aca6f00848c565-a7635525d40ac1c70300198708936b4e.ssl.cf1.rackcdn.com/images/da2ed080663271b6cb7056e5c047a16b190dca8b.pdf?utm_source=chatgpt.com "E1.S 14mm Airflow vs. Power"
[6]: https://www.snia.org/sites/default/files/SSSI/OCP%20EDSFF%20JM%20Hands.pdf?utm_source=chatgpt.com "OCP EDSFF JM Hands.pdf"
[7]: https://users.ece.cmu.edu/~omutlu/pub/flash-memory-failures-in-the-field-at-facebook_sigmetrics15.pdf?utm_source=chatgpt.com "A Large-Scale Study of Flash Memory Failures in the Field"
[8]: https://pcisig.com/sites/default/files/files/PCI-SIG_Webinar_EDSFF_FINAL.pdf?utm_source=chatgpt.com "PCI-SIG® Educational Webinar Series 2020"


**CMB — Controller Memory Buffer (≤150 words)**
The NVMe Controller Memory Buffer is an optional, controller-resident memory region exposed to the host through a PCIe BAR. When supported, hosts may place NVMe submission/completion queues, PRP/SGL lists, and—in some implementations—command data/metadata in the CMB instead of host DRAM, reducing PCIe transactions and latency and enabling peer-to-peer DMA paths. The controller advertises the CMB via CMBLOC/CMBSZ and restricts what objects may reside there; each queue/list must be wholly in the CMB or wholly in host memory, and violations return “Invalid Use of Controller Memory Buffer.” CMB space is typically mapped as uncached MMIO on the host. ([NVM Express][1])

```bibtex
@misc{nvme13cmb,
  title        = {NVM Express Revision 1.3},
  author       = {{NVM Express, Inc.}},
  year         = {2017},
  howpublished = {\url{https://nvmexpress.org/wp-content/uploads/NVM_Express_Revision_1.3.pdf}},
  note         = {Defines Controller Memory Buffer (CMB): placement of queues, PRP/SGL, data; CMBLOC/CMBSZ; invalid-use semantics},
  urldate      = {2025-09-15}
}
```

---

**PMR — Persistent Memory Region (≤150 words)**
The Persistent Memory Region is an optional, device-local area of persistent memory (e.g., power-protected DRAM or storage-class memory) that the host accesses with standard PCIe memory reads/writes. Unlike a CMB, a PMR provides durability semantics and operates independently of controller enable/disable; capability/control/status registers (PMRCAP/PMRCTL/PMRSTS) govern discovery and use, with additional descriptors (e.g., PMREBS, PMRSWTP) reporting elasticity and sustained-write characteristics. The NVMe specifications standardize the interface but intentionally leave PMR use-cases open (e.g., low-latency durable logs or offload paths). ([NVM Express][2])

```bibtex
@misc{nvme20dpmr,
  title        = {NVM Express Base Specification, Revision 2.0d},
  author       = {{NVM Express, Inc.}},
  year         = {2024},
  howpublished = {\url{https://nvmexpress.org/wp-content/uploads/NVM-Express-Base-Specification-2.0d-2024.01.11-Ratified.pdf}},
  note         = {Defines Persistent Memory Region (PMR): PMRCAP/PMRCTL/PMRSTS and related reporting},
  urldate      = {2025-09-15}
}
```

[1]: https://nvmexpress.org/wp-content/uploads/NVM_Express_Revision_1.3.pdf?utm_source=chatgpt.com "NVM Express Revision 1.3"
[2]: https://nvmexpress.org/wp-content/uploads/NVM-Express-Base-Specification-2.0d-2024.01.11-Ratified.pdf?utm_source=chatgpt.com "NVM Express Base Specification 2.0d"
