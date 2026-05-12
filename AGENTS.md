# Repository Guidelines

## 项目结构与模块组织
PX4 是一个以 C/C++ 为主的单体仓库。核心飞控逻辑位于 `src/modules`，驱动位于 `src/drivers`，板级代码位于 `boards/<vendor>/<board>/src`，平台适配代码位于 `platforms/`。NuttX 源码作为子树保存在 `platforms/nuttx/NuttX`，启动脚本在 `ROMFS/px4fmu_common`，消息定义在 `msg/` 和 `srv/`，仿真与测试相关内容位于 `Tools/`、`test/`、`integrationtests/` 和 `validation/`。

## 构建、测试与开发命令
- `make px4_sitl_default`：构建默认 SITL 仿真目标。
- `make px4_fmu-v6xhpm_default`：构建本分支常用的 HPM6750 板级固件。
- `ninja -C build/px4_fmu-v6xhpm_default`：在已有构建目录中快速续编。
- `make tests`：构建仓库支持的单元测试。
- `ctest --test-dir build/px4_sitl_default --output-on-failure`：运行已配置构建目录中的 CTest 测试。
- `make format`：执行仓库统一格式化。

## 代码风格与命名规范
优先遵循目标文件现有风格。PX4 板级与模块代码中若已使用 Tab，则保持一致；其他文件按周边格式处理。默认使用 ASCII。新增代码注释使用中文，内容应简短、技术化，只解释必要上下文。命名遵循现有约定：函数和变量使用 `snake_case`，宏使用 `UPPER_CASE`，C++ 类型使用 `CamelCase`。不要在板级 bring-up 或 NuttX 代码中做与任务无关的大范围重构。

## 测试规范
单元测试优先放在相关子系统附近或 `src/.../test` 中，集成测试放在 `integrationtests/`。涉及板级、USB、存储、传感器改动时，至少要编译对应目标板，并尽量附带 NSH/UART 运行日志。测试名称应描述行为，例如 `test_usbmsc_start_sector`，不要只体现实现细节。

## 提交与合并请求规范
最近提交风格以简短作用域前缀为主，例如 `usbmsc: harden runtime switching on hpm6750`、`fmu-v6xhpm: add USB mass storage command`。建议采用 `<scope>: <祈使句摘要>`。每个提交应聚焦单一目的。PR 需要说明目标板卡或平台、用户可见行为变化、执行过的构建或测试命令；涉及硬件、USB、传感器或界面改动时，附上日志或截图。

## 配置与安全注意事项
板级构建通常依赖本地工具链和当前 NuttX 状态。未经明确确认，不要回退 `platforms/nuttx/NuttX` 或 `boards/*` 中与当前任务无关的修改。对于 USB、存储、启动链和传感器 bring-up，建议先增加最小化诊断日志定位问题，确认根因后再收敛日志输出。
