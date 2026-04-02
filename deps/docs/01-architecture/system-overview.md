# 系统架构总览

> 从old_docs/architecture/overview.md迁移精简  
> 核心关注：模块划分、依赖关系、对外API

---

## 1. 系统概述

OrderManager 是一个期货委托、持仓、资金管理系统，构建为动态库（Windows .dll / Linux .so），对外提供 C 风格接口，供上层量化交易系统调用。

### 1.1 核心业务场景

- **委托管理**：接收委托状态变化（报单、成交、撤单等），记录并驱动后续持仓与资金计算
- **持仓管理**：精确到每一手（PositionUnit）的开平仓跟踪，按 FIFO 逻辑匹配平仓
- **资金管理**：实时计算冻结资金、保证金、手续费、浮动盈亏、可用资金、总权益
- **行情驱动**：接收合约最新价，刷新所有未平仓持仓的浮动盈亏
- **交易日管理**：支持交易日初始化（清理前日数据）和日终结算（快照持仓/资金至历史表）

### 1.2 主要工作流程

```
1. om_init()                      ← 系统初始化
2. om_trading_day_update(date)    ← 交易日初始化
3. 业务循环：
   ├─ om_handle_order(order, fee) ← 委托处理
   ├─ om_handle_newprice(code, p) ← 行情刷新
   └─ 查询接口                    ← 状态查询
4. om_trading_day_end()           ← 日终结算
   ... 回到步骤 2，进入下一个交易日 ...
N. om_release()                   ← 系统关闭
```

---

## 2. 模块划分

### 2.1 五层架构

| 层级 | 模块名 | 职责（一句话） | 对应源码目录 | 设计文档 |
|------|--------|---------------|-------------|---------|
| 接口层 | api | 对外 C 风格动态库接口的实现，参数校验，转调 service | `api/` | 03-implementation/interfaces/public-apis.md |
| 业务层 | service | 基于 core 的计算结果进行业务级编排、查询与聚合 | `service/` | 01-architecture/module-service.md |
| 核心计算层 | core | 委托处理、持仓 FIFO 匹配、资金计算 | `core/` | 01-architecture/module-core.md |
| 扩展套件层 | kit | 查询套件等可插拔扩展功能，与 core 层平行 | `kit/` | 01-architecture/module-kit.md |
| 数据层 | data | 数据的查询、更新与持久化存储（SQLite） | `data/` | 01-architecture/module-data.md |

### 2.2 公共模块

| 模块名 | 职责 | 对应源码目录 |
|--------|------|-------------|
| include | 对外暴露的 C 风格数据类型、枚举、API 声明 | `include/` |
| common | 项目相关的公共函数、内部 C++ 数据结构 | `common/` |
| utils | 纯工具：跨平台文件操作、日志管理 | `utils/` |

### 2.3 模块协作关系

```
┌─────────────────────────────────────────────────────────────────────┐
│  service 层 (业务编排)                                               │
│  ┌─────────────────┐    ┌────────────────────────────────────────┐ │
│  │  调用 core 层    │    │  调用 kit 层 (可选，如查询套件)          │ │
│  │  - 委托/持仓/资金 │    │  - QueryKitPool 查询套件池               │ │
│  │    核心计算      │    │  - (可扩展其他套件)                      │ │
│  └─────────────────┘    └────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

**说明**：
- **core 层**：核心计算逻辑，委托处理、持仓管理、资金计算等核心功能
- **kit 层**：扩展功能套件，与 core 层平行，通过独立组件为系统提供额外能力（如查询套件提供并发查询能力），不修改核心计算逻辑
- service 层根据业务需要调用 core 和/或 kit 层组件

---

## 3. 模块依赖关系

```
调用方向：从上到下（上层调用下层，禁止反向调用）

┌──────────────────────────────────────────────────┐
│                 include/                          │  ← 对外头文件
│  om_data_types.h, om_def.h, om_manager_api.h,     │
│  om_hft_api.h, om_query.h, hft_structs.h          │
└──────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────┐
│                   api/                            │  ← 接口层
│              调用 service 层                       │
└──────────────┬───────────────────────────────────┘
               │
┌──────────────▼───────────────────────────────────┐
│                 service/                          │  ← 业务层
│         调用 core 层和 kit 层                      │
└──────────────┬─────────────────┬──────────────────┘
               │                 │
┌──────────────▼────┐   ┌──────▼──────────────────┐
│      core/        │   │        kit/             │
│    核心计算层        │   │    扩展套件层(与core平行) │
│   调用 data 层     │   │   依赖 data 层          │
└──────────┬────────┘   └─────────────────────────┘
           │
           │
┌──────────▼────────────────────────────────────────┐
│                   data/                           │  ← 数据层
│              不依赖任何上层模块                      │
└───────────────────────────────────────────────────┘

横向依赖（所有层均可引用）：
┌─────────────┐  ┌──────────────────────────────────────┐
│   common/   │  │              utils/                   │
│ 项目公共工具  │  │ FileWrapper + LogManager              │
└─────────────┘  └──────────────────────────────────────┘
```

**依赖规则**：
- 上层可以调用下层，**不允许反向调用，不允许跨层调用**
- api 不能直接调 core、kit 或 data
- kit 层与 core 层**平行**，均由 service 层调用，两者互不依赖
- kit 层依赖 data 层（使用 Store 进行查询）
- common 和 utils 被所有层依赖，自身不依赖任何业务模块

---

## 3.1 命名空间约定

| 目录 | 命名空间 | 说明 |
|------|----------|------|
| include/ | 无 | 对外头文件，C 风格类型与接口，不含命名空间 |
| api/ | 无 | API 实现调用 `om::OmService` 等 |
| service/, core/, data/, kit/ | `namespace om` | 主体业务代码均位于 `om` 命名空间 |
| utils/, common/ | 无 | 工具与兼容层，不包裹命名空间 |

---

## 4. 目录结构

```
order-manager/
├── include/                         # 对外头文件
│   ├── om_data_types.h              # 核心数据结构
│   ├── om_def.h                     # 导出宏 + 枚举定义
│   ├── om_manager_api.h             # 主对外 API
│   ├── om_hft_api.h                 # HFT 适配 API
│   ├── om_query.h                   # 查询 API
│   ├── hft_structs.h                # HFT 结构体定义
│   └── om_error.h                   # 错误码定义
├── common/                          # 内部公共模块
│   └── om_compat.h                  # C++11 兼容层（make_unique polyfill）
├── api/                             # 接口层实现
├── service/                         # 业务层
│   ├── om_service.h/.cc             # 主服务类
├── core/                            # 核心计算层
│   ├── order_processor.h/.cc        # 委托处理编排
│   ├── position_processor.h/.cc       # 策略级持仓
│   ├── account_position_processor.h/.cc  # 账户级持仓
│   ├── fundtable_processor.h/.cc      # 策略级资金
│   ├── account_fundtable_processor.h/.cc # 账户级资金
│   ├── fundtable_snapshot_handler.h/.cc  # 资金快照
│   ├── order_context.h                # 处理上下文
│   ├── trade_processor.h/.cc          # 成交处理（入库）
│   └── calc_helper.h                  # 计算辅助函数
├── kit/                             # 扩展套件层（与core平行）
│   ├── query_kit.h/.cc                # 查询套件
│   ├── query_kit_pool.h/.cc           # 查询套件池
│   └── （可扩展其他类型套件）
├── data/                            # 数据层
│   ├── db_manager.h/.cc             # 数据库连接管理
│   ├── order_store.h/.cc            # 委托存储
│   ├── order_his_store.h/.cc        # 委托历史快照
│   ├── trade_store.h/.cc            # 成交存储
│   ├── trade_his_store.h/.cc        # 成交历史快照
│   ├── position_unit_store.h/.cc    # 持仓单元存储
│   ├── position_unit_his_store.h/.cc # 持仓单元历史
│   ├── contract_stat_store.h/.cc    # 合约统计存储
│   ├── fundtable_store.h/.cc        # 资金表存储
│   ├── fundtable_his_store.h/.cc    # 资金历史存储
│   ├── account_fundtable_store.h/.cc    # 账户资金存储
│   ├── account_fundtable_his_store.h/.cc  # 账户资金历史
│   ├── account_position_unit_store.h/.cc # 账户持仓存储
│   ├── account_position_unit_his_store.h/.cc # 账户持仓历史
│   ├── account_contract_stat_store.h/.cc # 账户合约统计
│   ├── combination_unit_store.h/.cc # 组合持仓存储
│   └── combination_unit_his_store.h/.cc # 组合持仓历史
├── utils/                           # 工具模块
│   ├── file_wrapper.h/.cc           # 文件操作
│   └── log_manager.h/.cc            # 日志管理
├── test/                            # 测试代码
│   ├── test.cc                      # 测试入口
│   ├── test_scenario*.h             # 场景测试
│   └── test_two_days_flow.h         # 两日流程演示
└── docs/                            # 设计文档（渐进式架构）
```

---

## 5. 技术选型

| 领域 | 选型 | 版本要求 | 选型理由 |
|------|------|---------|---------|
| 编程语言 | C++ | C++11 | 双平台兼容，满足性能需求 |
| 对外接口 | C | - | 保证跨语言 ABI 兼容 |
| 构建系统 | CMake | >= 3.10 | 跨平台构建 |
| 数据库 | SQLite3 | 3.37.2+ | 嵌入式、零配置、单文件 |
| 日志 | 自研 LogManager | - | 轻量、无第三方依赖 |

---

## 6. 对外 API 总览

### 6.1 API 分类

| 类别 | 头文件 | 核心 API | 职责概述 |
|------|--------|---------|---------|
| **系统管理** | `om_manager_api.h` | `om_init`, `om_release` | 系统初始化与释放 |
| **交易日管理** | `om_manager_api.h` | `om_trading_day_update`, `om_trading_day_end` | 交易日切换与结算 |
| **委托处理** | `om_manager_api.h` | `om_handle_order` | 接收委托状态变化，驱动持仓资金计算 |
| **成交处理** | `om_manager_api.h` | `om_handle_trade` | 接收成交记录 |
| **行情刷新** | `om_manager_api.h` | `om_handle_newprice` | 刷新浮动盈亏 |
| **资金配置** | `om_manager_api.h` | `om_set_fund_config`, `om_set_account_fund_config` | 初始化资金记录 |
| **HFT 适配** | `om_hft_api.h` | `om_handle_order_hft`, `om_handle_trade_hft` | HFT 结构适配层 |
| **查询接口** | `om_query.h` | `om_query_order`, `om_query_fund`, ... | 状态查询（策略级+账户级） |

### 6.2 主流程时序

```
om_init() → om_trading_day_update() → [业务循环] → om_trading_day_end() → ...
                          ↑
业务循环: om_handle_order() / om_handle_trade() / om_handle_newprice() / om_query_*()
```

> **详细接口定义**：见 [`03-implementation/interfaces/public-apis.md`](../03-implementation/interfaces/public-apis.md)

---

## 7. 结算模型：逐日盯市

本系统采用与中国期货市场一致的**逐日盯市（Mark-to-Market）**结算制度。

### 7.1 核心规则

| 持仓类别 | hold_cost 含义 | 来源 |
|---------|---------------|------|
| 今仓（当日开仓） | 开仓成交价 | 开仓时写入 |
| 昨仓（跨日持仓） | **前一日结算价** | 日终结算时更新 |

### 7.2 结算核心步骤

| 步骤 | 操作 | 资金影响 | 持仓影响 |
|------|------|---------|---------|
| 1 | 结算盈亏计算兑现 | `avail_cash += 结算盈亏`, `pnl = 0` | - |
| 2 | 更新持仓价 | - | `hold_cost = 结算价` |
| 3 | 重算保证金 | `avail_cash -= 保证金差额` | `margin = 新保证金` |

> **模型详情**：见 [`02-domain/fund-model.md`](../02-domain/fund-model.md) §4 日终结算  
> **流程实现**：见 [`03-implementation/flows/settlement-flow.md`](../03-implementation/flows/settlement-flow.md)

---

## 8. 核心设计特点

1. **手级别持仓追踪**：core 层持仓计算精确到每一手，每手的开平价格、时间、手续费、保证金独立计算
2. **FIFO 平仓匹配**：平仓时按先进先出顺序匹配持仓单元
3. **费率外部传入**：费率与合约参数由调用方随委托一并传入，本系统不维护费率数据
4. **数据集中存储**：所有数据储存在 data 层，core/service/api 层不存储任何数据
5. **双维度维护**：策略级 + 账户级持仓/资金独立维护，实时同步
6. **增量处理**：om_handle_order 每次只处理与上次调用的增量

---

## 9. 相关文档

| 主题 | 文档位置 | 层级 | 说明 |
|------|---------|------|------|
| **模块详细职责** | 本目录下 `module-core.md`、`module-service.md`、`module-data.md`、`module-kit.md` | L1 | core/data/service/kit 各模块详细设计 |
| **业务模型定义** | [`02-domain/`](../02-domain/) | L2 | 订单/持仓/资金/组合模型定义 |
| **计算公式** | [`02-domain/calc-formulas.md`](../02-domain/calc-formulas.md) | L2 | 保证金/盈亏/手续费公式 |
| **业务流程** | [`03-implementation/flows/`](../03-implementation/flows/) | L3 | 委托/成交/结算/行情流程 |
| **接口定义** | [`03-implementation/interfaces/`](../03-implementation/interfaces/) | L3 | Store/Processor/Public API |
| **测试场景** | [`03-implementation/scenarios/`](../03-implementation/scenarios/) | L3 | 场景测试文档 |
| **全局约定** | [`01-architecture/conventions.md`](./conventions.md) | L1 | 命名规范/错误处理/精度约定 |
| **文档规范** | [`04-reference/DOCUMENT_STANDARD.md`](../04-reference/DOCUMENT_STANDARD.md) | L4 | 本文档编写规范 |
