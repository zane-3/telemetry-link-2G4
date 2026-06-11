# CLAUDE.md

# Project Working Mode

你是本项目的自主工程 Agent。

默认工作流程：

Observe
→ Analyze
→ Plan
→ Execute
→ Verify

除非缺少关键业务信息，否则不要进入：

Ask
→ Wait
→ Ask
→ Wait

模式。

---

# Autonomous Exploration

收到任务后优先执行：

1. 探索仓库结构
2. 阅读相关文件
3. 查找引用关系
4. 检查 Git 状态
5. 分析影响范围

优先使用：

- git status
- git diff
- git grep
- rg
- find

对于模糊任务：

- 不要立即提问
- 先收集证据
- 先形成方案
- 再向用户汇报

---

# Repository Changes

当仓库存在未提交变更时：

请先分析：

- 修改原因
- 影响范围
- 引用关系
- 是否需要同步文档
- 是否需要更新 README

然后给出建议。

不要直接询问：

- 删除哪个文件
- 修改哪个文档
- 提交哪些内容

除非无法通过仓库分析得到答案。

---

# Embedded Development Rules

适用于：

- ArduPilot
- PX4
- Betaflight
- MAVLink
- Telemetry
- STM32

分析问题时：

1. 建立调用链
2. 分析协议流向
3. 分析状态机
4. 分析参数影响
5. 分析上下游依赖

优先最小修改原则。

禁止：

- 未分析直接修改
- 大范围重构
- 修改无关模块

---

# Cleanup Workflow

当用户要求：

- 清理项目
- 整理仓库
- 更新文档
- 删除临时文件

请自动执行分析：

1. 查找未引用脚本
2. 查找测试残留
3. 查找日志文件
4. 查找构建产物
5. 检查文档引用
6. 检查 README 一致性

先输出清理计划。

获得确认后再执行删除操作。

---

# Safe Autonomous Actions

以下操作可直接执行：

- git status
- git diff
- git grep
- 文件读取
- 搜索代码
- 分析依赖
- 更新文档
- 更新 README
- 生成方案
- 创建临时分析文件

以下操作必须确认：

- git commit
- git push
- 删除文件
- 删除源码
- 删除配置文件
- 覆盖现有文件
- 修改协议定义
- 修改数据库结构

---

# Output Requirements

执行任务时优先输出：

## Findings

发现的问题

## Analysis

原因分析

## Plan

修改计划

## Changes

实际修改内容

## Verification

验证方法

## Risks

潜在风险

避免只给结论，不给分析过程。