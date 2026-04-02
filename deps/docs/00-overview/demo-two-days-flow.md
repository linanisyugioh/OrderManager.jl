# Order Manager 两日完整业务流程演示

> 示例程序：`test/test_two_days_flow.h` → `run_two_days_flow()`  
> 本文档演示跨两个交易日的完整业务流程，重点说明**持仓跨日**、**逐日盯市结算**、**今昨仓转换**等关键概念。

---

## 1. 场景概述

### 1.1 业务场景

本示例模拟一个真实的两日期货交易场景：

**Day1 (20260313)**
- 上午：系统初始化，开仓买入 2手 au2506
- 盘中：价格从 500 上涨到 505，浮动盈亏 100元
- 收盘：结算价 502，日终结算后盈亏兑现 40元，持仓成本更新为 502

**Day2 (20260314)**
- 开盘：昨仓自动识别（Day1持仓变为昨仓）
- 上午：开仓买入 2手（今仓），平仓卖出 2手昨仓
- 收盘：结算价 510，日终结算

### 1.2 关键演示点

| 概念 | 说明 |
|------|------|
| **持仓跨日** | Day1的今仓在Day2自动转为昨仓 |
| **逐日盯市(MTM)** | 日终结算盈亏兑现到 avail_cash，持仓价更新为结算价 |
| **今昨仓区分** | SHFE/INE 必须指定平今/平昨（Today_* / PreDay_*） |
| **资金分层** | 策略级资金独立管理，账户级资金用于风控校验 |

---

## 2. 完整流程图

```
系统启动
    │
    ├─ om_init                     【一次性】初始化数据库
    ├─ om_set_fund_config          【一次性】策略级资金配置
    ├─ om_set_account_fund_config  【一次性】账户级资金配置
    │
    │  ╔═══════════════════════════════════════════════════════╗
    │  ║  Day1 (20260313)                                      ║
    │  ╠═══════════════════════════════════════════════════════╣
    │  ║  om_trading_day_update(20260313)   交易日初始化       ║
    │  ║  om_add_fee_info                    传入合约费率       ║
    │  ║                                                       ║
    │  ║  【盘中交易】                                          ║
    │  ║  om_handle_order(开仓 PendingNew)  委托报入           ║
    │  ║  om_handle_order(开仓 Filled)      委托成交→创建持仓   ║
    │  ║  om_handle_newprice(505)           盘中行情           ║
    │  ║  om_query_fund/contract_stat         查询状态           ║
    │  ║                                                       ║
    │  ║  【日终结算】                                          ║
    │  ║  om_handle_newprice(502)           传入结算价         ║
    │  ║  om_trading_day_end                盈亏兑现，持仓过夜  ║
    │  ╚═══════════════════════════════════════════════════════╝
    │
    │  ╔═══════════════════════════════════════════════════════╗
    │  ║  Day2 (20260314)                                      ║
    │  ╠═══════════════════════════════════════════════════════╣
    │  ║  om_trading_day_update(20260314)   新交易日开始       ║
    │  ║  ↳ 昨仓自动识别（Day1持仓转为昨仓）                   ║
    │  ║                                                       ║
    │  ║  【盘中交易】                                          ║
    │  ║  om_handle_order(开新仓 Filled)    Day2今仓           ║
    │  ║  om_handle_order(平昨仓 Filled)    平掉昨仓            ║
    │  ║  om_query_fund/contract_stat       查询验证            ║
    │  ║                                                       ║
    │  ║  【日终结算】                                          ║
    │  ║  om_handle_newprice(510)           传入结算价         ║
    │  ║  om_trading_day_end                盈亏兑现           ║
    │  ╚═══════════════════════════════════════════════════════╝
    │
    ├─ om_release                    【一次性】释放资源
    │
系统结束
```

---

## 3. 详细步骤说明

### 阶段一：系统初始化（一次性）

#### 【Init-1】om_init(work_dir)

| 项目 | 说明 |
|------|------|
| 调用时机 | 程序启动时，整个系统运行期间仅执行一次 |
| 作用 | 建立数据库连接、创建表结构、启动日志系统 |
| 注意 | 与 `om_release` 配对，程序退出前必须调用 |

#### 【Init-2】om_set_fund_config(&fund)

| 项目 | 说明 |
|------|------|
| 调用时机 | `om_trading_day_update` 之前，系统初始化时 |
| 作用 | 初始化策略级资金，主键：`run_id+account_id+account_type+strategy_id` |
| 注意 | 同主键已存在则返回 `FundtableStore_DupKey`，不覆盖 |

**示例配置**：
```cpp
Fundtable fund;
strncpy(fund.run_id, "RUN_DEMO", ...);
strncpy(fund.account_id, "ACC_DEMO", ...);
fund.account_type = AccountType_Futures;
strncpy(fund.strategy_id, "STRAT_DEMO", ...);
fund.avail_cash = 10000000000LL;  // 100万 × 10000
```

#### 【Init-3】om_set_account_fund_config(&acct_fund)

| 项目 | 说明 |
|------|------|
| 调用时机 | `om_set_fund_config` 之后 |
| 作用 | 初始化账户级资金，用于风控校验和总权益汇总 |
| 约束 | 账户级资金必须 ≥ 该账户下所有策略级资金之和 |

---

### 阶段二：Day1 交易流程

#### 【Day1-1】om_trading_day_update(20260313) + om_add_fee_info

| 项目 | 说明 |
|------|------|
| 调用时机 | Day1 开盘前 |
| 作用 | 清空 order/trade 表，删除已平仓持仓，重建 ContractStat，校验账户资金 |
| 注意 | 这是「每日数据库生命周期」的起点，必须在事务中执行 |

**数据变更**：
- 委托表(order)、成交表(trade) → 清空
- 持仓单元表 → 删除 `close_date > 0` 的记录
- 合约统计表(contract_stat) → 清空后重建

#### 【Day1-2】om_set_query_scope

设置查询作用域，后续 `om_query_*` 接口使用此作用域自动查询。

#### 【Day1-3/4】开仓流程

| 步骤 | 委托状态 | 系统行为 |
|------|----------|----------|
| 报入 | `PendingNew` | 计算冻结资金（保证金+手续费），写入委托表 |
| 成交 | `Filled` | 释放冻结、扣减 avail_cash、增加 margin、创建持仓单元 |

**持仓创建**：开仓成交后，系统创建 2手 PositionUnit：
- `open_date = 20260313`（今仓）
- `hold_cost = 5000000`（开仓价）
- `direction = PositionSide_Long`

#### 【Day1-5】om_handle_newprice(行情)

盘中行情推送，刷新浮动盈亏：
```
pnl = (last_price - hold_cost) × multiply × volume × dir_sign
    = (505 - 500) × 1 × 2 × (+1)
    = 100元
```

#### 【Day1-6】om_handle_newprice(结算价)

收盘前传入结算价 502，供日终结算使用。

#### 【Day1-7】om_trading_day_end 日终结算

| 结算项 | 计算 |
|--------|------|
| 盈亏兑现 | `avail_cash += 40元`（结算盈亏） |
| 持仓价更新 | `hold_cost = 5020000`（结算价） |
| 保证金重算 | 按结算价重新计算 |
| 资金快照 | 生成 fundtable_his 历史记录 |

**持仓状态**：2手多头，成本价=502，昨仓（过夜到Day2）

---

### 阶段三：Day2 交易流程

#### 【Day2-1】om_trading_day_update(20260314)

| 项目 | 说明 |
|------|------|
| 调用时机 | Day2 开盘前 |
| 关键变化 | Day1的今仓自动转为昨仓 |

**ContractStat 重建后状态**：
```
today_long_volume = 0          // Day2暂无今仓
yesterday_long_volume = 2    // Day1持仓转为昨仓
```

#### 【Day2-2】查询验证

调用 `om_query_contract_stat` 验证昨仓识别正确。

#### 【Day2-3】开新仓（Day2今仓）

开仓成交后，持仓结构：
```
昨仓：2手（成本价 502）
今仓：2手（成本价 508，Day2开仓价）
```

#### 【Day2-4】平仓（平昨仓）

**关键：使用 PreDay_Long_Close**

```cpp
// SHFE必须指定平今/平昨
order.side = OrderSide_PreDay_Long_Close;  // 平昨多头
order.volume = 2;
```

**平仓盈亏计算**：
```
realized_pnl = (close_price - hold_cost) × multiply × volume × dir_sign
             = (509 - 502) × 1 × 2 × (+1)
             = 140元
```

平仓后：`avail_cash += 140元`（实现盈亏转入可用资金）

**持仓结构变化**：
```
平仓前：昨仓2手 + 今仓2手 = 4手
平仓后：昨仓0手 + 今仓2手 = 2手（仅剩余Day2今仓）
```

#### 【Day2-5/6/7】行情推送 + 结算

- 行情推送刷新剩余持仓盈亏
- 传入结算价 510
- 日终结算，盈亏兑现

---

## 4. 关键概念详解

### 4.1 今仓 vs 昨仓

| 类型 | 定义 | 识别方式 |
|------|------|----------|
| 今仓 | 当日开仓的持仓 | `open_date == trading_date` |
| 昨仓 | 历史开仓、隔日保留的持仓 | `open_date < trading_date` |

**交易日初始化时的转换**：
```cpp
// Day1持仓
open_date = 20260313, trading_date = 20260313  → 今仓

// Day2交易日初始化后（同一持仓）
open_date = 20260313, trading_date = 20260314  → 昨仓
```

### 4.2 逐日盯市结算(MTM)

**等价理解**：以 0 手续费将所有持仓按结算价平仓，再按结算价重新开仓。

| 结算步骤 | 操作 |
|----------|------|
| 平仓 | 盈亏兑现进入 `avail_cash` |
| 重新开仓 | `hold_cost = settlement_price`，`pnl = 0` |

**资金变化**：
```
Day1收盘前：
  avail_cash = X
  pnl = 40元（浮动盈亏）

Day1结算后：
  avail_cash = X + 40  （盈亏兑现）
  pnl = 0              （归零）
  hold_cost = 502      （更新为结算价）
```

### 4.3 平仓方向选择

| 交易所 | 平仓要求 | 示例 |
|--------|----------|------|
| SHFE/INE | 必须指定平今/平昨 | `Today_Long_Close` / `PreDay_Long_Close` |
| DCE/CZCE/CFFEX/GFEX | 不区分今昨 | `Long_Close` / `Short_Close` |

**错误选择后果**：
- SHFE 使用 `Long_Close`（非指定）会导致平仓失败或错误匹配
- 必须使用 `Today_*_Close` 或 `PreDay_*_Close` 明确指定

---

## 5. 代码示例

### 5.1 示例程序入口

```cpp
// test/test_two_days_flow.h
inline int run_two_days_flow(void) {
    // 1. 系统初始化（仅一次）
    om_init(DEMO_WORK_DIR);
    om_set_fund_config(&fund);
    om_set_account_fund_config(&acct_fund);
    
    // 2. Day1 交易
    run_day1_trading(&fee);
    
    // 3. Day2 交易
    run_day2_trading(&fee);
    
    // 4. 系统释放
    om_release();
}
```

### 5.2 Day1 开仓示例

```cpp
// Step 1: 委托报入（PendingNew）
demo_create_order(&order, "DAY1_OPEN", DAY1_DATE, 
                  OrderSide_Long_Open, OrderStatus_PendingNew, 
                  0, DAY1_OPEN_PRICE);
om_handle_order(order, fee);

// Step 2: 委托成交（Filled）
demo_create_order(&order, "DAY1_OPEN", DAY1_DATE,
                  OrderSide_Long_Open, OrderStatus_Filled,
                  DEMO_VOLUME, DAY1_OPEN_PRICE);
order.filled_turnover = DAY1_OPEN_PRICE * DEMO_MULTIPLY * DEMO_VOLUME;
om_handle_order(order, fee);
```

### 5.3 Day2 平昨仓示例

```cpp
// 平昨多头（SHFE必须指定 PreDay_*）
demo_create_order(&order, "DAY2_CLOSE", DAY2_DATE,
                  OrderSide_PreDay_Long_Close, OrderStatus_Filled,
                  DEMO_VOLUME, close_price);
om_handle_order(order, fee);
```

---

## 6. 数据流追踪

### Day1 资金变化追踪

| 时点 | avail_cash | margin | pnl | equity | 说明 |
|------|------------|--------|-----|--------|------|
| 初始 | 1000000 | 0 | 0 | 1000000 | 初始资金 |
| 开仓 | 999880 | 120 | 0 | 1000000 | 冻结120元保证金 |
| 盘中505 | 999880 | 120 | 100 | 1000100 | 浮动盈亏100元 |
| 结算后 | 999920 | 120.48 | 0 | 1000040 | 盈亏兑现40元 |

*注：数值单位为元，实际存储扩大10000倍*

### Day2 持仓变化追踪

| 时点 | 昨仓 | 今仓 | 总持仓 | 说明 |
|------|------|------|--------|------|
| Day1收盘 | - | 2 | 2 | Day1今仓 |
| Day2开盘 | 2 | 0 | 2 | 转为昨仓 |
| 开新仓后 | 2 | 2 | 4 | 增加今仓 |
| 平昨仓后 | 0 | 2 | 2 | 平掉昨仓 |
| Day2收盘 | 0 | 2 | 2 | 仅余今仓 |

---

## 7. 相关文档

| 主题 | 位置 |
|------|------|
| 交易日初始化 | `../03-implementation/flows/dayinit-flow.md` |
| 日终结算 | `../03-implementation/flows/settlement-flow.md` |
| 委托处理流程 | `../03-implementation/flows/order-flow.md` |
| 计算公式 | `../02-domain/calc-formulas.md` |
| 快速参考 | `quick-reference.md` |

---

## 8. 修订记录

| 日期 | 版本 | 说明 |
|------|------|------|
| 2026-03-16 | v1.0 | 初始版本，创建两日示例文档 |
