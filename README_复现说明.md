# 当前最佳工程精简源码包

来源：`E:\competition_best\best_arm5_20261029`的五舵机EDG2版本，保留新分类投放角度、量化模型、图像采集/推理、定位、投票与上位机控制逻辑。已同步NVF1无缺陷归类修复，三类缺陷阈值、定位与机械参数保持原方案。

无缺陷判票使用通过现有过滤、去重后的本帧候选，已剔除的边缘/几何无效候选不再阻止正常票。

## 包含内容

- `hardware`：当前实际使用的RTL、三份约束、9个独立IP的XCI配置及完整Block Design及官方导出的重建Tcl；`recreate_project.tcl`从零创建工程。生成的wrapper由BD重建。干净重建入口使用`hardware/create_bd.tcl`，显式恢复原AXIS转换器的TLAST/TUSER宽度，避免缺少旧IP缓存时的参数校验顺序问题；`hardware/bd`保留原BD配置作为参考。
- `firmware/src`：ARM裸机C/H、链接脚本、定位参考与抓取标定；`firmware/bsp`：驱动源码和当前硬件配置头文件，无预编译库。
- `pc`：上位机C#源代码、清单、配置与Parser/Control测试源代码。
- `model/sd_parameters`：运行当前已训练模型所必需的量化权重、偏置、激活表、配置及输入文件。它们是必要运行数据，不能当缓存删除。
- `runtime`：原有XSCT加载、状态检查、定位取证源脚本；`scripts`：从源码构建与生成新加载包的入口。
- `tests`：保留机械臂、PWM、寄存器与定位测试源码。辅助测试可按对应脚本运行。

不包含旧`.xpr`工程状态、`.runs/.gen/.cache/.Xil`、IP输出产品、DCP布局检查点、bit/XSA/ELF/EXE、对象文件、训练数据集、历史截图与大日志。构建时再生成这些文件。

## 工具

Windows，Vivado 2023.1（含XC7Z035器件支持）、Vitis 2023.1、.NET Framework 4.x。默认安装目录为`E:\vivado2023`；可使用脚本参数修改。硬件默认不依赖板卡预设文件，也不依赖原C盘工作目录。

## 重建

在PowerShell执行：

```powershell
cd E:\source_code
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_all.ps1
```

单项重建：

```powershell
.\scripts\build_hardware.ps1 -CreateOnly
# 换一个全新输出目录，仅检查工程与IP再生成：
.\scripts\build_hardware.ps1 -GenerateOnly -OutDir E:\source_code_buildcheck\hardware
.\scripts\build_firmware.ps1
.\scripts\build_gui.ps1
```

默认构建产物在`build_out`。已有硬件/BSP构建目录不会被静默覆盖；重跑时传入新的`-OutDir`或先自行清理自己生成的build_out。`build_all.ps1 -BuildRoot <新目录>`可将所有构建结果放到指定位置。

硬件重建使用完整综合和布局布线，不依赖旧的增量DCP。工具种子/环境与增量参考不同，结果不保证与历史bit逐字节一致；脚本检查setup/hold并保留时序与DRC报告，未过时序不发布bit。

固件先从BSP驱动源码重新构建库，再编译应用，并从新ELF生成符号地址，避免沿用旧ELF地址。硬件变化后应重新用新XSA生成BSP，不能用当前固定地址头文件适配任意新设计。

## 准备运行

`build_all.ps1`最后调用`prepare_runtime.ps1`，把本次bit、ELF、PS初始化脚本和符号表配套放到`build_out/runtime`，并重新计算加载哈希。加载器中的旧发布哈希没有被误用到新固件上。

把`model/sd_parameters`中的参数文件复制到SD卡根目录。不要混入旧BOOT.BIN；本包没有重新固化SD启动镜像。

支撑好机械臂，物理断开外部舵机电源后，在XSCT显式执行：

```tcl
source {E:/source_code/build_out/runtime/load_arm5_cold.tcl}
```

上位机为`build_out/pc/MetalDefectMonitor.exe`。加载后PWM默认失能，之后仍由操作者恢复舵机供电、使能HOME、记录观察位、首次完整内框定位验证，再启动分拣。构建脚本不会连接板卡、打开串口或驱动机械臂。

## 复现边界

本包复现的是已训练量化模型的硬件部署与控制工程，不是重新训练全过程；没有将手工标注训练集复制进来。机械臂底座、相机观察姿态与抓取标定发生变化时必须重新标定。电源板EDA原始工程、机械结构CAD未包含在当前已提供的FPGA源码工程中。

文件来源和SHA256见`source_provenance.json`与`SHA256SUMS.json`。

## 可选Vitis IDE与RTL测试

主构建入口使用命令行从BSP源码和C源码编译，不要求保存IDE大工作区。需要IDE时，在生成新XSA后可在XSCT执行`source E:/source_code/scripts/create_vitis_workspace.tcl`；新工作区默认位于`build_out/vitis_workspace`。该入口按当前源码布局创建Vitis工作区。

已有五项机械臂/PWM/插值/寄存器仿真可执行`scripts/run_rtl_tests.ps1`，结果写入`build_out/rtl_tests`。

无缺陷判票的定向C回归可运行`scripts/run_normal_vote_tests.ps1`；产物写入`build_out/normal_vote_tests`，可用`-OutDir`指定包外目录。
