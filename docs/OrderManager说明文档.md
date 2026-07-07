# OrderManager.jl 使用说明文档

> 版本：V0.1.17  
> 底层二进制：`om.dll`（Windows）/ `libom.so`（Linux）  
> 依赖：[FinancialStruct.jl](https://github.com/linanisyugioh/FinancialStruct.jl)、[CBinding.jl](https://github.com/analytech-solutions/CBinding.jl)

## 目录

- [1. 概述](#1-概述)
- [2. 安装](#2-安装)
- [3. 快速开始](#3-快速开始)
- [4. 核心概念](#4-核心概念)
- [5. 通用约定](#5-通用约定)
- [6. 接口速查表](#6-接口速查表)
- [7. Manager API — 生命周期与业务处理](#7-manager-api--生命周期与业务处理)
- [8. Query API — 简化查询接口](#8-query-api--简化查询接口)
- [9. 聚合查询 API — PositionWithOrder](#9-聚合查询-api--positionwithorder)
- [10. HFT Adapter API — HFT 结构体适配](#10-hft-adapter-api--hft-结构体适配)
- [11. 资产校验 API](#11-资产校验-api)
- [12. 错误码](#12-错误码)
- [13. 完整示例：两日流程](#13-完整示例两日流程)

---

## 1. 概述

`OrderManager.jl` 是订单管理系统（Order Management System, OM）的 Julia 封装，通过 `ccall` 调用 C/C++ 动态库 `om.dll` / `libom.so`，提供：

- **委托驱动**：接收 Order → 计算持仓 → 计算资金冻结/保证金/手续费；
- **成交回报**：接收 Trade → 落库；
- **持仓聚合**：按 `(open_order_id, close_order_id)` 二元组把持仓单元聚合为可用于查询的 `PositionWithOrder`；
- **资金管理**：策略级 `Fundtable` + 账户级 `AccountFundtable`，日切与日终结算；
- **资产校验**：与外部系统对齐 OM 系统的资金与持仓；
- **HFT 适配层**：直接接收 `HftOrder` / `HftTrade` / `HftCodeInfo` 结构体（`FinancialStruct.cFuOrder` / `cFuTrade` / `cFuCodeInfo`），自动完成 symbol→code、微秒→毫秒等转换。

## 2. 安装

```julia
using Pkg
Pkg.add(url="https://github.com/linanisyugioh/OrderManager.jl")
```

或克隆到本地后按路径安装：

```bash
git clone https://github.com/linanisyugioh/OrderManager.jl.git
```

```julia
using Pkg
Pkg.add(path="D:/path/to/OrderManager.jl")
```

安装时会自动通过 `Artifacts.toml` 下载对应平台的二进制文件（Windows / Linux x86_64）。

## 3. 快速开始

```julia
using OrderManager
using FinancialStruct

# 1) 初始化（工作目录 + 日志级别）
err = om_init("./om_workdir", 1)      # 0=Debug 1=Info 2=Warn 3=Error
@assert err == 0

# 2) 设置查询作用域（后续 Query API 都用该作用域）
om_set_query_scope("run_id_001", "acc_001", 2)  # account_type: 2=Futures

# 3) 写入账户级、策略级资金初值
om_set_account_fund_config(account_fund)  # cAccountFundtable
om_set_fund_config(strategy_fund)         # cFundtable

# 4) 交易日更新（开盘前）
om_trading_day_update(20260701)

# 5) 灌入合约费率信息
om_add_fee_info(fee_code_info)            # cFeeCodeInfo

# 6) 循环处理委托与成交
om_handle_order(order, fee_code_info)     # cOmOrder + cFeeCodeInfo
om_handle_trade(trade)                     # cOmTrade

# 7) 行情更新（刷新浮动盈亏）
om_handle_newprice("SHFE.rb2510", 4_5000_0000, 4_4800_0000, 0)  # 扩大 10000 倍

# 8) 收盘后触发日终结算
missing_codes = om_trading_day_end(0)      # 0=严格模式，1=允许最新价代替结算价
isempty(missing_codes) || @warn "缺少结算价: $missing_codes"

# 9) 释放资源
om_release()
```

## 4. 核心概念

### 4.1 作用域（Scope）

绝大多数 Query API 依赖预先设置的 **查询作用域**：`(run_id, account_id, account_type)`。调用 `om_set_query_scope` 一次后，后续查询无需重复传入这三个字段。

### 4.2 策略级 vs 账户级

| 级别 | 主键组成 | 典型结构 |
|------|---------|---------|
| 策略级 | `run_id + account_id + account_type + strategy_id` | `Fundtable`、`PositionUnit`、`ContractStat` |
| 账户级 | `run_id + account_id + account_type` | `AccountFundtable`、`AccountPositionUnit`、`AccountContractStat`、`CombinationUnit` |

账户级为跨策略汇总，支持组合持仓（`combination_id`）。

### 4.3 数值扩大倍数

| 类别 | 倍数 |
|------|------|
| 价格 / 保证金金额 / 盈亏 / 权益 | ×10000 |
| 保证金率 / 单手保证金 | ×10000 |
| 手续费比例 | ×10000000（部分 ×10000） |

### 4.4 持仓单元 vs 持仓聚合

- **PositionUnit**：每手一条，四条队列（今多、今空、昨多、昨空）；持久化到 DB。
- **PositionWithOrder**：非持久化，按 `(open_order_id, close_order_id)` 聚合，供查询用。

## 5. 通用约定

1. **字符串**：Julia `String` 通过 `Ptr{UInt8}` 传给 C；C 返回的字符串指针用 `unsafe_string` 解码。
2. **结构体**：使用 `FinancialStruct` 的 `c*` 别名（`cOmOrder`、`cOmTrade`、`cFeeCodeInfo` 等），构造方式见 [structs.md](../structs.md)。
3. **返回码**：`0` 表示成功；负数为错误码，具体见 [第 12 节](#12-错误码)。
4. **返回值语义**：
   - 单条查询接口返回 `Union{结构体, Nothing}`，`nothing` 表示未找到或出错；
   - 多条查询接口返回 `Vector{结构体}`，空数组表示无数据或出错；
   - `om_set_*`、`om_handle_*` 等接口直接返回错误码 `Cint`。

## 6. 接口速查表

### Manager API（生命周期与业务处理）

| 接口 | 功能 |
|------|------|
| [`om_init`](#71-om_init) | 初始化服务 |
| [`om_release`](#72-om_release) | 释放服务 |
| [`om_trading_day_update`](#73-om_trading_day_update) | 交易日更新（开盘前） |
| [`om_add_fee_info`](#74-om_add_fee_info) | 添加合约费率/保证金参数 |
| [`om_trading_day_end`](#75-om_trading_day_end) | 交易日结束（日终结算） |
| [`om_handle_order`](#76-om_handle_order) | 处理委托回报 |
| [`om_handle_trade`](#77-om_handle_trade) | 处理成交回报 |
| [`om_handle_newprice`](#78-om_handle_newprice) | 更新最新价/刷新浮动盈亏 |
| [`om_set_fund_config`](#79-om_set_fund_config) | 写入策略级资金初值 |
| [`om_set_account_fund_config`](#710-om_set_account_fund_config) | 写入账户级资金初值 |

### Query API（简化查询）

| 接口 | 功能 |
|------|------|
| [`om_set_query_scope`](#81-om_set_query_scope) | 设置查询作用域 |
| [`om_get_query_run_id`](#82-作用域读取) | 读取当前作用域 run_id |
| [`om_get_query_account_id`](#82-作用域读取) | 读取当前作用域 account_id |
| [`om_get_query_account_type`](#82-作用域读取) | 读取当前作用域 account_type |
| [`om_query_order`](#83-om_query_order) | 按主键查询单条委托 |
| [`om_query_order_ids`](#84-om_query_order_ids) | 查询策略下 order_id 列表 |
| [`om_query_order_id_by_cl_order_id`](#85-om_query_order_id_by_cl_order_id) | cl_order_id → 系统 order_id |
| [`om_query_order_cl_and_strategy`](#86-om_query_order_cl_and_strategy) | order_id → (cl_order_id, strategy_id) |
| [`om_query_position_codes`](#87-om_query_position_codes) | 查询策略下持仓 code 列表 |
| [`om_query_account_position_codes`](#88-om_query_account_position_codes) | 查询账户下持仓 code 列表 |
| [`om_query_contract_stat`](#89-om_query_contract_stat) | 查询策略级合约统计 |
| [`om_query_account_contract_stat`](#810-om_query_account_contract_stat) | 查询账户级合约统计 |
| [`om_query_fund`](#811-om_query_fund) | 查询策略级资金 |
| [`om_query_account_fund`](#812-om_query_account_fund) | 查询账户级资金 |
| [`om_query_strategy_ids`](#813-om_query_strategy_ids) | 查询当前作用域下所有 strategy_id |

### 聚合查询 API（PositionWithOrder）

| 接口 | 功能 |
|------|------|
| [`om_query_position_records`](#91-om_query_position_records) | 策略级按开仓委托聚合查询 |
| [`om_query_position_his`](#92-om_query_position_his) | 策略级历史快照读取 |
| [`om_query_account_position_records`](#93-om_query_account_position_records) | 账户级按开仓委托聚合查询 |
| [`om_query_account_position_his`](#94-om_query_account_position_his) | 账户级历史快照读取 |

### HFT Adapter API

| 接口 | 功能 |
|------|------|
| [`om_add_fee_info_hft`](#101-om_add_fee_info_hft) | HFT 合约信息添加 |
| [`om_handle_order_hft`](#102-om_handle_order_hft) | HFT 委托处理 |
| [`om_handle_trade_hft`](#103-om_handle_trade_hft) | HFT 成交处理 |

### 资产校验

| 接口 | 功能 |
|------|------|
| [`om_verify_assets`](#111-om_verify_assets) | 资金 + 持仓一致性校验 |

---

## 7. Manager API — 生命周期与业务处理

### 7.1 `om_init`

**功能**：初始化服务（日志、数据库、Store 与 service 组件）。

```julia
om_init(work_dir::String, log_level::Integer=1)::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `work_dir` | `String` | 工作目录（必须可写）；日志写入 `work_dir/logs`，数据库为 `work_dir/om.db` |
| `log_level` | `Integer` | 0=Debug / 1=Info（默认）/ 2=Warn / 3=Error |

**返回**：`Cint`  
`0` 成功；`-1` 参数非法；`-9` 已初始化；`-581` 数据库打开失败；`-582` 事务失败。

---

### 7.2 `om_release`

**功能**：释放资源；关闭数据库连接，与 `om_init` 配对使用。

```julia
om_release()::Cvoid
```

无入参、无返回值。

---

### 7.3 `om_trading_day_update`

**功能**：交易日更新（新交易日开始，开盘前调用）；清空 order 表，并移除 position 表中所有已平仓的记录。

```julia
om_trading_day_update(trading_date::Integer)::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `trading_date` | `Integer` | 交易日 YYYYMMDD |

**返回**：`Cint`  
`0` 成功；`-1` 参数非法；`-8` 未初始化；`-13` 账户级资金校验失败；`-14` 账户级持仓校验失败。

> 💡 建议调用后再遍历持仓 codes（用 `om_query_account_position_codes`）逐个 `om_add_fee_info`。

---

### 7.4 `om_add_fee_info`

**功能**：添加合约基础信息至缓存（合约乘数、保证金率、手续费等），供日终结算、开仓保证金计算使用。

```julia
om_add_fee_info(fee_info::cFeeCodeInfo)::Cint
om_add_fee_info(fee_infos::Vector{cFeeCodeInfo})::Nothing   # 批量便捷版
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `fee_info` | `cFeeCodeInfo` | 合约基础信息 |

**返回**：`Cint` — `0` 成功；`-1` 参数非法；`-8` 未初始化。

---

### 7.5 `om_trading_day_end`

**功能**：交易日结束触发日终结算，逐手计算结算盈亏并转入可用资金，更新持仓价为结算价，重算保证金。

```julia
om_trading_day_end(force_settle_with_last_price::Integer=0)::String
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `force_settle_with_last_price` | `Integer` | 0=严格模式（必须有结算价），1=强制模式（结算价缺失时用最新价替代） |

**返回**：`String` — 缺失结算价的合约代码列表（逗号分隔），仅在返回 `OM_MissingSettlementPrice` 时非空。

**可能错误**：`-10` 缺少结算价；`-11` 缺少费率缓存；`-12` 盈亏不一致（警告）。

---

### 7.6 `om_handle_order`

**功能**：接收委托最新状态，驱动 `Order → Position → Fundtable` 全流程；由调用方随委托传入该合约的费率/保证金参数。

```julia
om_handle_order(order::cOmOrder, fee_info::cFeeCodeInfo)::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `order` | `cOmOrder` | 委托最新状态（按值传入） |
| `fee_info` | `cFeeCodeInfo` | 与该委托标的对应的手续费率/保证金参数 |

**Order 必填字段**：
- **主键**：`order_id`, `oper_date`, `strategy_id`, `run_id`, `account_id`, `account_type`
- **业务**：`code`, `market`, `side`, `status`, `volume`, `price`
- **成交时**：`filled_volume`, `filled_turnover`（= 均价 × multiply × filled_volume）
- **系统计算字段**：`frozen`, `fee`, `margin_ratio` 入参填 0

**返回**：`Cint`  
`0` 成功；错误码分布 `-101 ~ -107`（OrderProc）、`-201 ~ -205`（PositionProc）、`-301 ~ -305`（FundtableProc），详见 [第 12 节](#12-错误码)。

---

### 7.7 `om_handle_trade`

**功能**：接收成交回报并写入 trade 表。当前实现仅落库，不参与持仓/资金计算，也不校验关联 Order 是否存在。

```julia
om_handle_trade(trade::cOmTrade)::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `trade` | `cOmTrade` | 成交数据（按值传入） |

**Trade 必填字段**：
- **主键（7 字段）**：`order_id`, `trade_date`, `strategy_id`, `run_id`, `account_id`, `account_type`, `match_seqno`
- **业务**：`code`, `side`, `volume`, `price`
- **辅助**：`filled_turnover`, `fee`, `order_volume`, `order_price`, `slippage`, `date`, `transact_time`, `match_type`

**返回**：`Cint`  
`0` 成功；`-601 ~ -604`（TradeProc）；`-461 ~ -464`（TradeStore）。

---

### 7.8 `om_handle_newprice`

**功能**：更新合约最新价，刷新该合约下所有未平仓持仓的浮动盈亏；同时写入最新价和结算价缓存。

```julia
om_handle_newprice(code::String, last_price::Integer,
                   pre_settlement_price::Integer, settlement_price::Integer)::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `code` | `String` | 合约代码，与 position/position_unit 的 code 一致，非空 |
| `last_price` | `Integer` | 最新价，扩大 10000 倍 |
| `pre_settlement_price` | `Integer` | 昨结算价，扩大 10000 倍 |
| `settlement_price` | `Integer` | 今结算价，扩大 10000 倍；0 表示盘中行情（无效） |

**返回**：`Cint` — `0` 成功；`-1` 参数非法；`-8` 未初始化。

**行为**：
1. `settlement_price > 0`：收盘结算，用结算价计算盈亏；
2. `settlement_price == 0`：盘中行情，用最新价计算盈亏；
3. 若缓存中已有 `settlement_price > 0`，直接返回，代表当天已终态。

---

### 7.9 `om_set_fund_config`

**功能**：写入策略级资金配置初值。按 `(run_id, account_id, account_type, strategy_id)` 唯一确定一条记录，已存在则返回错误，不覆盖。

```julia
om_set_fund_config(fund::cFundtable)::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `fund` | `cFundtable` | 策略级资金配置（金额字段扩大 10000 倍） |

**返回**：`Cint` — `0` 成功；`-2` 主键重复；`-431 ~ -433` FundtableStore 错误。

---

### 7.10 `om_set_account_fund_config`

**功能**：写入账户级资金配置初值。按 `(run_id, account_id, account_type)` 唯一确定；账户级资金跨策略汇总，用于风控和总权益计算。

```julia
om_set_account_fund_config(fund::cAccountFundtable)::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `fund` | `cAccountFundtable` | 账户级资金配置（金额字段扩大 10000 倍） |

**返回**：`Cint` — `0` 成功；`-471 ~ -474` AccountFundtableStore 错误。

---

## 8. Query API — 简化查询接口

所有 Query API **均使用先前 `om_set_query_scope` 缓存的作用域** `(run_id, account_id, account_type)`，只需传入具体的 `strategy_id`、`order_id`、`code` 等业务字段。

### 8.1 `om_set_query_scope`

**功能**：设置查询作用域。

```julia
om_set_query_scope(run_id::String, account_id::String, account_type::Integer)::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `run_id` | `String` | 实例ID，非空 |
| `account_id` | `String` | 账户ID，非空 |
| `account_type` | `Integer` | 账户类型（1=Stock, 2=Futures, 3=Options, ...） |

**返回**：`Cint` — `0` 成功；`-1` 参数非法；`-8` 未初始化。

---

### 8.2 作用域读取

```julia
om_get_query_run_id()::String
om_get_query_account_id()::String
om_get_query_account_type()::Cint
```

分别返回当前缓存的 `run_id`、`account_id`、`account_type`。

---

### 8.3 `om_query_order`

**功能**：按主键查询单条委托。

```julia
om_query_order(order_id::String, oper_date::Integer, strategy_id::String)::Union{cOmOrder, Nothing}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `order_id` | `String` | 委托ID |
| `oper_date` | `Integer` | 委托日期 YYYYMMDD |
| `strategy_id` | `String` | 策略ID |

**返回**：`cOmOrder` 或 `nothing`（未找到 / 出错）。

---

### 8.4 `om_query_order_ids`

**功能**：查询指定策略下委托的 `order_id` 列表。

```julia
om_query_order_ids(strategy_id::String, status::Integer,
                   code::String, side::Integer, bs::Integer)::Vector{String}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `strategy_id` | `String` | 策略ID，非空 |
| `status` | `Integer` | 0=未终态；1=已终态；2=所有状态 |
| `code` | `String` | 合约代码，空字符串 = 所有合约 |
| `side` | `Integer` | 0=平；1=开；3=全部 |
| `bs` | `Integer` | 0=空；1=多；3=全部 |

**返回**：`Vector{String}` — order_id 列表（空数组表示无数据）。

---

### 8.5 `om_query_order_id_by_cl_order_id`

**功能**：按 `strategy_id + cl_order_id` 查询系统内部 `order_id`。

```julia
om_query_order_id_by_cl_order_id(strategy_id::String, cl_order_id::String)::String
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `strategy_id` | `String` | 策略ID，非空 |
| `cl_order_id` | `String` | 客户委托ID，非空 |

**返回**：`String` — 系统 order_id；未找到返回空字符串。

---

### 8.6 `om_query_order_cl_and_strategy`

**功能**：按 `order_id` 反查 `(cl_order_id, strategy_id)`。

```julia
om_query_order_cl_and_strategy(order_id::String)::Tuple{String, String}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `order_id` | `String` | 系统委托ID，非空 |

**返回**：`(cl_order_id, strategy_id)`；未找到返回 `("", "")`。

---

### 8.7 `om_query_position_codes`

**功能**：查询指定策略下持仓的 `code` 列表。

```julia
om_query_position_codes(strategy_id::String, status::Integer,
                        period::Integer, side::Integer)::Vector{String}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `strategy_id` | `String` | 策略ID，非空 |
| `status` | `Integer` | 0=冻结；1=可用；2=全部 |
| `period` | `Integer` | 0=昨仓；1=今仓；2=全部 |
| `side` | `Integer` | 0=空；1=多；2=全部 |

**返回**：`Vector{String}` — code 列表。

---

### 8.8 `om_query_account_position_codes`

**功能**：查询账户下全部未平仓持仓的 distinct code。**用于交易日初始化后遍历调用 `om_add_fee_info`。**

```julia
om_query_account_position_codes()::Vector{String}
```

无入参。**返回**：`Vector{String}` — code 列表。

---

### 8.9 `om_query_contract_stat`

**功能**：查询策略级合约统计（今/昨、多/空的 volume 与 frozen_volume）。

```julia
om_query_contract_stat(strategy_id::String, code::String)::Union{cContractStat, Nothing}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `strategy_id` | `String` | 策略ID |
| `code` | `String` | 合约代码 |

**返回**：`cContractStat` 或 `nothing`。

---

### 8.10 `om_query_account_contract_stat`

**功能**：查询账户级合约统计（跨策略汇总）。

```julia
om_query_account_contract_stat(code::String)::Union{cAccountContractStat, Nothing}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `code` | `String` | 合约代码 |

**返回**：`cAccountContractStat` 或 `nothing`。

---

### 8.11 `om_query_fund`

**功能**：查询策略级资金。

```julia
om_query_fund(strategy_id::String)::Union{cFundtable, Nothing}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `strategy_id` | `String` | 策略ID |

**返回**：`cFundtable` 或 `nothing`。

---

### 8.12 `om_query_account_fund`

**功能**：查询账户级资金。

```julia
om_query_account_fund()::Union{cAccountFundtable, Nothing}
```

无入参。**返回**：`cAccountFundtable` 或 `nothing`。

---

### 8.13 `om_query_strategy_ids`

**功能**：查询当前作用域下所有 `strategy_id` 列表。

```julia
om_query_strategy_ids()::Vector{String}
```

无入参。**返回**：`Vector{String}` — strategy_id 列表。

---

## 9. 聚合查询 API — PositionWithOrder

以 `(open_order_id, close_order_id)` 二元组分组：未平部分取自 `position_unit`、已平部分取自 `position_unit_his`，逐组聚合为一条 `PositionWithOrder`。

**实现说明**：C 端在 service 内维护本次查询的结果缓存，通过 `out_buf` 出参返回指向该缓存的只读指针，避免调用方预分配缓冲区导致的容量试探。Julia 端拿到指针后立即用 `unsafe_wrap` + `copy` 拷贝为独立的 `Vector` 返回 —— C 端缓存"下次调用同一接口或 `om_release` 前有效"，一旦返回值就与 C 端缓存解耦，可以安全跨接口穿插调用。

### 9.1 `om_query_position_records`

**功能**：策略级按开仓委托聚合查询（实时聚合）。

```julia
om_query_position_records(strategy_id::String, open_order_id::String,
                          include_history::Integer)::Vector{cPositionWithOrder}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `strategy_id` | `String` | 策略ID，非空 |
| `open_order_id` | `String` | 开仓委托ID，非空 |
| `include_history` | `Integer` | 0=仅今日平仓+未平仓；1=全部（未平+今日平+历史平） |

**返回**：`Vector{cPositionWithOrder}` — 一个开仓委托可能产出 0..N 条已平 + 0..1 条未平记录。

---

### 9.2 `om_query_position_his`

**功能**：读取某日的 `position_his` 持仓聚合快照（每日结算物化写入）。

```julia
om_query_position_his(strategy_id::String, oper_date::Integer)::Vector{cPositionWithOrder}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `strategy_id` | `String` | 策略ID，非空 |
| `oper_date` | `Integer` | 归属日 YYYYMMDD |

**返回**：`Vector{cPositionWithOrder}`。

> 💡 当日数据请用 `om_query_position_records` 实时聚合；历史快照才用本接口。

---

### 9.3 `om_query_account_position_records`

**功能**：账户级按开仓委托聚合查询（跨策略汇总，支持 combination）。

```julia
om_query_account_position_records(open_order_id::String,
                                  include_history::Integer)::Vector{cAccountPositionWithOrder}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `open_order_id` | `String` | 开仓委托ID，非空 |
| `include_history` | `Integer` | 0=仅今日平仓+未平仓；1=全部 |

**返回**：`Vector{cAccountPositionWithOrder}`。

---

### 9.4 `om_query_account_position_his`

**功能**：读取某日账户级 `account_position_his` 快照。

```julia
om_query_account_position_his(oper_date::Integer)::Vector{cAccountPositionWithOrder}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `oper_date` | `Integer` | 归属日 YYYYMMDD |

**返回**：`Vector{cAccountPositionWithOrder}`。

---

## 10. HFT Adapter API — HFT 结构体适配

用于对接 HFT 交易框架，直接传入 `cFuOrder`（HftOrder）/ `cFuTrade`（HftTrade）/ `cFuCodeInfo`（HftCodeInfo）。转换规则：

- `symbol (市场.合约ID)` → `code`；
- 从 symbol 解析市场前缀映射为 Exchange 枚举（SHFE/DCE/CZCE/CFFEX/INE/GFEX 等）；
- 从 symbol 解析品种（合约代码中首个数字前的字母部分）填入 `product`；
- `order_status → status`、`order_type → order_type`、`side → side`（枚举值一致）；
- `transact_time`：HFT 微秒 → OM 毫秒（÷ 1000）；
- `turnover`：HFT 未乘合约乘数，从系统 fee 缓存获取乘数后自动补乘。

### 10.1 `om_add_fee_info_hft`

**功能**：添加 HFT 合约信息（`cFuCodeInfo` → `cFeeCodeInfo`）至缓存。

```julia
om_add_fee_info_hft(hft_code_info::cFuCodeInfo)::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `hft_code_info` | `cFuCodeInfo` | HFT 合约信息（symbol、乘数、保证金率、手续费等） |

**返回**：`Cint` — `0` 成功；`-1` 参数非法；`-8` 未初始化。

---

### 10.2 `om_handle_order_hft`

**功能**：处理 HFT 委托回报，等价于 `om_handle_order`，但入参为 HFT 结构。

```julia
om_handle_order_hft(hft_order::cFuOrder, hft_code_info::cFuCodeInfo)::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `hft_order` | `cFuOrder` | HFT 委托结构 |
| `hft_code_info` | `cFuCodeInfo` | 与该委托标的对应的 HFT 合约信息 |

**返回**：`Cint` — 错误码同 `om_handle_order`。

---

### 10.3 `om_handle_trade_hft`

**功能**：处理 HFT 成交回报，等价于 `om_handle_trade`。

```julia
om_handle_trade_hft(hft_trade::cFuTrade)::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `hft_trade` | `cFuTrade` | HFT 成交结构 |

**返回**：`Cint`  
错误码同 `om_handle_trade`；另可返回 `-11` `OM_MissingFeeInfo`（缓存中未找到该合约的费率信息）。

> ⚠️ **前置**：须先通过 `om_add_fee_info_hft` 将合约信息缓存，否则报 `OM_MissingFeeInfo`。

---

## 11. 资产校验 API

### 11.1 `om_verify_assets`

**功能**：校验 OM 系统的资金与持仓是否满足外部系统的要求。

1. **资金校验（底限校验）**：OM 系统的 `account_equity ≥ min_required_equity`；
2. **持仓校验（严格相等）**：OM 系统持仓按合约聚合（今+昨、区分多空），须与外部持仓完全一致；无容忍度。

```julia
om_verify_assets(run_id::String, account_id::String, account_type::Integer,
                 min_required_equity::Integer,
                 positions::Vector{cExternalPosition})::Cint
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `run_id` | `String` | 实例ID（作用域标识） |
| `account_id` | `String` | 账户ID（作用域标识） |
| `account_type` | `Integer` | 账户类型 |
| `min_required_equity` | `Integer` | 最低权益要求（扩大 10000 倍） |
| `positions` | `Vector{cExternalPosition}` | 外部持仓数据数组 |

**返回**：`Cint` — `0` 校验通过；其余为错误码：

| 错误码 | 常量 | 含义 |
|--------|------|------|
| -801 | `AssetVerify_InvalidArg` | 参数非法 |
| -802 | `AssetVerify_NotInited` | 服务未初始化 |
| -803 | `AssetVerify_StoreError` | Store 查询失败 |
| -810 | `AssetVerify_FundInsufficient` | 资金不足 |
| -821 | `AssetVerify_PositionMissingInOM` | 持仓缺失：外部有，OM 系统没有 |
| -822 | `AssetVerify_PositionMissingInExternal` | 持仓缺失：OM 系统有，外部没有 |
| -823 | `AssetVerify_PositionLongMismatch` | 多头持仓数量不一致 |
| -824 | `AssetVerify_PositionShortMismatch` | 空头持仓数量不一致 |

**错误码优先级**（同时存在时按此顺序返回）：`-821 > -822 > -823/-824 > -810`。

---

## 12. 错误码

| 错误码 | 常量 | 含义 |
|--------|------|------|
| 0 | `OM_Ok` | 成功 |
| -1 | `OM_InvalidArg` | 参数非法 |
| -2 | `FundtableStore_DupKey` | fundtable 主键重复 |
| -8 | `OM_NotInited` | service 未初始化 |
| -9 | `OM_AlreadyInited` | service 已初始化 |
| -10 | `OM_MissingSettlementPrice` | 日终结算时缺少结算价缓存 |
| -11 | `OM_MissingFeeInfo` | 日终结算时缺少费率缓存 |
| -12 | `OM_SettlementPnlMismatch` | 结算盈亏与盘中浮动盈亏不一致 |
| -13 | `OM_FundCheckFailed` | 交易日初始化账户级资金校验失败 |
| -14 | `OM_PositionCheckFailed` | 交易日初始化账户级持仓校验失败 |

### 分段错误码（负号越大越具体）

| 段区间 | 层级 | 常见错误 |
|--------|------|---------|
| -100 ~ -199 | 委托处理层 OrderProc | -101 参数非法；-102 fee_code_info.code 不匹配；-104 状态流转非法；-105 保证金率选择失败；-106 交易所+平仓方向组合非法；-107 hedge_flag 非法 |
| -200 ~ -299 | 持仓处理层 PositionProc | -202 找不到持仓；-204 可用持仓不足；-205 交易所+平仓方向非法 |
| -300 ~ -399 | 资金处理层 FundtableProc | -303 找不到 Fundtable；-304 可用资金不足；-305 保证金率选取失败 |
| -400 ~ -579 | 数据层 Store | 各 Store 的 InvalidArg/SqlError/NotFound/DupKey |
| -580 ~ -589 | 数据库管理层 | -581 数据库打开失败；-582 事务失败 |
| -600 ~ -699 | 成交处理层 TradeProc | -602 关联委托不存在；-604 主键重复 |
| -700 ~ -799 | QueryKitPool | -701 未初始化；-702 重复初始化 |
| -800 ~ -899 | 资产校验层 | 见 [11.1 表格](#111-om_verify_assets) |

---

## 13. 完整示例：两日流程

```julia
using OrderManager
using FinancialStruct

# 使用 str_to_carray_memcpy 打包字符串字段
function str_to_carray_memcpy(s::AbstractString, ::Val{N}) where {N}
    ref = Libc.malloc(Carray{Int8, N}())
    GC.@preserve s ref begin
        len = min(ncodeunits(s), N)
        unsafe_copyto!(
            Ptr{UInt8}(Base.unsafe_convert(Ptr{Carray{Int8, N}}, ref)),
            pointer(s), len,
        )
    end
    return ref[]
end

# ============ Day 1 ============
om_init("./om_data", 1)

om_set_query_scope("run-001", "acc-001", 2)  # 2 = Futures

# 账户级资金初值 1 亿
account_fund = FinancialStruct.cAccountFundtable(
    run_id             = str_to_carray_memcpy("run-001", Val(64)),
    account_id         = str_to_carray_memcpy("acc-001", Val(64)),
    account_type       = 2,
    currency           = 0,
    account_start_cash   = 100_000_000 * 10_000,
    account_cash         = 100_000_000 * 10_000,
    account_start_equity = 100_000_000 * 10_000,
    account_equity       = 100_000_000 * 10_000,
)
om_set_account_fund_config(account_fund)

# 策略级资金初值 1000 万
strat_fund = FinancialStruct.cFundtable(
    run_id       = str_to_carray_memcpy("run-001", Val(64)),
    account_id   = str_to_carray_memcpy("acc-001", Val(64)),
    account_type = 2,
    strategy_id  = str_to_carray_memcpy("EMA-IC", Val(32)),
    avail_cash   = 10_000_000 * 10_000,
    start_cash   = 10_000_000 * 10_000,
    equity       = 10_000_000 * 10_000,
    start_equity = 10_000_000 * 10_000,
)
om_set_fund_config(strat_fund)

# 交易日更新
om_trading_day_update(20260701)

# 灌入合约费率
fee = FinancialStruct.cFeeCodeInfo(
    code            = str_to_carray_memcpy("CFFEX.IC2609", Val(32)),
    fee_type        = 2,           # 按手数
    open_today      = 30 * 10_000, # 每手 3 元
    close_today     = 30 * 10_000,
    margin_long1    = 1500,        # 15%
    margin_short1   = 1500,
    multiply        = 200,
    price_tick      = 2000,        # 0.2 点
    presettleprice  = 5000 * 10_000,
)
om_add_fee_info(fee)

# 开多仓
open_order = FinancialStruct.cOmOrder(
    order_id     = str_to_carray_memcpy("OD-001", Val(64)),
    oper_date    = 20260701,
    strategy_id  = str_to_carray_memcpy("EMA-IC", Val(32)),
    run_id       = str_to_carray_memcpy("run-001", Val(64)),
    account_id   = str_to_carray_memcpy("acc-001", Val(64)),
    account_type = 2,
    code         = str_to_carray_memcpy("CFFEX.IC2609", Val(32)),
    market       = 4,          # CFFEX
    status       = 4,          # Filled
    side         = 3,          # Long_Open
    volume       = 4,
    price        = 5100 * 10_000,
    contract_multiply = 200,
    filled_volume   = 4,
    filled_turnover = 5100 * 200 * 4 * 10_000,
)
om_handle_order(open_order, fee)

# 更新行情
om_handle_newprice("CFFEX.IC2609", 5150 * 10_000, 5000 * 10_000, 0)

# 查询持仓聚合
pos_records = om_query_position_records("EMA-IC", "OD-001", 0)
@info "开仓后持仓聚合" 记录数=length(pos_records)

# 收盘：传入结算价
om_handle_newprice("CFFEX.IC2609", 5150 * 10_000, 5000 * 10_000, 5140 * 10_000)

# 日终结算
missing = om_trading_day_end(0)
isempty(missing) || @warn "缺失结算价" codes=missing

# ============ Day 2 ============
om_trading_day_update(20260702)

# 遍历持仓 codes，重新灌入费率
for code in om_query_account_position_codes()
    om_add_fee_info(fee)  # 实际应按 code 加载对应的 fee
end

# 查询昨日 position_his 快照
his = om_query_position_his("EMA-IC", 20260701)
@info "Day 1 持仓快照" 记录数=length(his)

# ... 继续处理 ...

om_release()
```

---

## 附录：相关文档

- **底层结构体定义**：见项目根目录 [`structs.md`](../structs.md)（FinancialStruct.jl 的完整说明）
- **头文件**：[`deps/include/*.h`](../deps/include/)（`om_manager_api.h`、`om_query.h`、`om_hft_api.h`、`om_data_types.h`、`om_def.h`、`om_error.h`、`hft_structs.h`）
- **业务领域文档**：见 [`deps/docs/`](../deps/docs/)
- **GitHub 仓库**：<https://github.com/linanisyugioh/OrderManager.jl>
