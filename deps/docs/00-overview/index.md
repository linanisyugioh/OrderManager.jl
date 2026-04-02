# Order Manager 项目文档入口

> **版本**: v1.0  
> **日期**: 2026-03-18  
> **状态**: 渐进式文档架构已启用

---

## 1. 系统一句话描述

Order Manager 是一个**期货委托、持仓、资金管理系统**，构建为动态库（.dll/.so），对外提供 C 风格接口，支持多账户、多策略、逐日盯市结算。

---

## 2. 核心概念速览

| 概念 | 说明 | 对应数据结构 |
|------|------|-------------|
| **委托(Order)** | 买卖指令，记录委托状态变化 | `OmOrder` 结构体 |
| **组合委托(ComboOrder)** | 两腿合约的套利交易指令（如 DCE.b2606&b2612） | `OmOrder` 结构体（特殊code格式） |
| **成交(Trade)** | 成交明细记录；当前仅入库，扩展预留可驱动持仓 | `OmTrade` 结构体 |
| **持仓单元(PositionUnit)** | 每手持仓一条记录，FIFO管理 | `PositionUnit` 结构体 |
| **合约统计(ContractStat)** | 今/昨、多/空持仓量统计 | `ContractStat` 结构体 |
| **资金表(Fundtable)** | 策略级资金记录 | `Fundtable` 结构体 |
| **账户资金(AccountFundtable)** | 账户级资金记录 | `AccountFundtable` 结构体 |
| **账户持仓(AccountPositionUnit)** | 账户级持仓单元 | `AccountPositionUnit` 结构体 |
| **组合持仓(CombinationUnit)** | 账户级保证金优惠组合 | `CombinationUnit` 结构体 |

### 2.1 关键术语

| 术语 | 说明 |
|------|------|
| **逐日盯市(MTM)** | 每日结算盈亏，持仓价更新为结算价 |
| **今仓/昨仓** | 当日开仓 vs 历史持仓（跨日） |
| **冻结资金** | 委托挂单的预估资金占用 |
| **保证金** | 持仓占用的履约担保资金 |
| **权益(Equity)** | 总权益 = 保证金 + 可用资金 + 冻结资金 + 浮动盈亏 |
| **作用域(Scope)** | 定位资金/持仓的4维度：run_id + account_id + account_type + strategy_id |

---

## 3. 架构分层

```
┌─────────────────────────────────────────────────────────────┐
│  对外API层 (om_manager_api.h, om_hft_api.h, om_query.h)     │
│  - om_init, om_trading_day_update, om_add_fee_info, om_handle_order, om_handle_newprice, om_handle_trade, om_trading_day_end   │
│  - om_handle_order_hft, om_handle_trade_hft                 │
│  - om_set_query_scope, om_query_order, om_query_fund, ...   │
├─────────────────────────────────────────────────────────────┤
│  service层 (业务编排)                                         │
│  - OmService: 主服务，对外API编排、事务控制                   │
│  - TradingDayInitService: 交易日初始化、合约统计重建、资金校验 │
│  - TradingDayEndService: 日终结算、未终态委托处理、历史归档   │
│  - ComboOrderService: 组合委托解析、拆腿、配对持仓            │
│  - QueryKitService: 查询作用域管理、并发查询封装             │
│  详见: 01-architecture/module-service.md                     │
├─────────────────────────────────────────────────────────────┤
│  core层 (核心计算)           │  kit层 (扩展套件，与core平行)  │
│  - OrderProcessor: 委托处理  │  - QueryKitPool: 查询套件池    │
│  - PositionProcessor: 持仓   │  - QueryKit: 独立查询连接      │
│  - FundtableProcessor: 资金  │  - (可扩展其他类型套件)        │
│  详见: 01-architecture/module-core.md | 详见: 01-architecture/module-kit.md |
├─────────────────────────────────────────────────────────────┤
│  data层 (数据持久化)                                         │
│  - OrderStore, PositionUnitStore, ContractStatStore          │
│  - FundtableStore, FundtableHisStore                        │
│  - AccountFundtableStore, AccountPositionUnitStore         │
│  详见: 01-architecture/module-data.md                        │
└─────────────────────────────────────────────────────────────┘
```

**架构说明**：
- **service 层**：业务编排层，不实现计算逻辑，负责 API 实现、交易日管理、事务控制，编排 core/kit 层调用
- **core 层**：核心计算逻辑，委托处理、持仓管理、资金计算等核心功能
- **kit 层**：扩展功能套件，与 core 层平行，通过独立组件为系统提供额外能力（如查询套件提供并发查询能力），不修改核心计算逻辑

---

## 4. 命名空间约定

| 目录 | 命名空间 | 说明 |
|------|----------|------|
| include/ | 无 | 对外头文件，C 风格类型与接口，不含命名空间 |
| api/ | 无 | API 实现调用 `om::OmService` 等 |
| service/, core/, data/ | `namespace om` | 主体业务代码均位于 `om` 命名空间 |
| utils/, common/ | 无 | 工具与兼容层，不包裹命名空间 |

---

## 5. 支持的交易所

| 代码 | 交易所 | 平仓特点 |
|------|--------|----------|
| SHFE | 上期所 | 必须指定平今/平昨 |
| DCE | 大商所 | 不区分今昨 |
| CZCE | 郑商所 | 不区分今昨 |
| CFFEX | 中金所 | 不区分今昨 |
| INE | 能源所 | 必须指定平今/平昨 |
| GFEX | 广期所 | 不区分今昨 |

---

## 6. 文档使用指南

### 6.1 四层文档架构

| 层级 | 目录 | 用途 | 何时阅读 |
|------|------|------|----------|
| **L0** | `00-overview/` | 项目概览、快速索引 | 首次进入项目、快速查找 |
| **L1** | `01-architecture/` | 模块职责、依赖关系 | 了解模块分工 |
| **L2** | `02-domain/` | 业务模型、字段定义、计算公式 | 编码实现时 |
| **L3** | `03-implementation/` | 流程、接口、场景 | 具体实现时 |
| **L4** | `04-reference/` | 规范、字段要求、决策记录 | 查阅约定、追溯决策 |

### 6.2 按任务类型选择文档

| 任务类型 | 推荐阅读 |
|----------|----------|
| 修改订单处理逻辑 | `02-domain/order-lifecycle.md` → `03-implementation/flows/order-flow.md` |
| 修改组合委托逻辑 | `02-domain/combination-order.md` → `02-domain/combination-position.md` |
| 修改成交处理逻辑 | `02-domain/trade-lifecycle.md` → `03-implementation/interfaces/public-apis.md` |
| 修改持仓计算 | `02-domain/position-model.md` → `03-implementation/interfaces/processor-apis.md` |
| 修改组合持仓逻辑 | `02-domain/combination-position.md` → `03-implementation/interfaces/public-apis.md` |
| 修改资金计算 | `02-domain/fund-model.md` → `02-domain/calc-formulas.md` |
| 新增测试场景 | `03-implementation/scenarios/` 目录参考已有场景 |
| 理解日终结算 | `02-domain/fund-model.md` §日终结算 + `03-implementation/flows/settlement-flow.md` |

---

## 7. 核心公式速查（详见 02-domain/calc-formulas.md）

```
保证金/手 = price × multiply × margin_ratio / 10000
手续费/手 = price × multiply × rate / 100000  (rate已扩大10万倍)
浮动盈亏  = (last_price - hold_cost) × multiply × dir_sign
实现盈亏  = (close_price - hold_cost) × multiply × dir_sign
权益      = margin + avail_cash + frozen_cash + pnl
```

---

## 8. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-03-18 | 架构图补充 module-service、module-data 文档引用；日期更新 |
| 2026-03-14 | 初始版本 |

---

## 9. 下一步阅读建议

1. **快速了解项目** → 阅读本文档 + `navigation.md`
2. **了解模块职责** → 阅读 `01-architecture/` 目录
3. **开始编码实现** → 根据任务类型查阅 `02-domain/` 和 `03-implementation/`
