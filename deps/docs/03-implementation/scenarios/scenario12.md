# 场景12设计文档：日终结算未终态委托处理测试

## 1. 场景概述

### 1.1 测试目标
验证 Order Manager 在**日终结算**时，`processNonTerminalOrders`（Step 0）对**未终态委托**的自动处理逻辑正确性：

1. **普通开仓委托**：PendingNew/PartiallyFilled 日终自动变为 CancelFilled/PartiallyCanceled，释放 frozen_cash
2. **普通平仓委托**：PendingNew/PartiallyFilled 日终自动撤单，释放 ContractStat/AccountContractStat 的 frozen 字段
3. **组合开仓委托**：PartiallyFilled 时腿委托有冻结，日终释放后 account_frozen=0
4. **checkFrozenAssets**：日终后无剩余冻结资产，不出现 "DETECTED remaining frozen assets" ERROR 日志

### 1.2 业务背景
根据 `03-implementation/flows/settlement-flow.md` §Step 0：

- **未终态**：PendingNew(1)、New(2)、PartiallyFilled(3)、PendingCancel(5)、Canceling(6)
- **处理规则**：filled_volume==0 → CancelFilled；0<filled<volume → PartiallyCanceled
- **效果**：开仓委托释放 frozen_cash；平仓委托释放 ContractStat.frozen

与场景6、8 的区别：场景6/8 为**盘中显式撤单**，本场景为**日终结算时自动处理**（不调用 om_handle_order 撤单，直接 om_trading_day_end）。

### 1.3 测试范围

| 验证点 | 说明 |
|--------|------|
| 策略级 frozen_cash | Fundtable 日终后 = 0 |
| 账户级 account_frozen | AccountFundtable 日终后 = 0 |
| 策略级 ContractStat frozen | today/yesterday_long_frozen = 0 |
| 账户级 AccountContractStat frozen | 同上 |
| 委托状态 | 原未终态变为 CancelFilled/PartiallyCanceled |
| 权益守恒 | 日终前后 equity 一致或符合结算公式 |

---

## 2. 测试数据设计

### 2.1 合约参数

| 合约 | 用途 | 乘数 | 保证金率 | 开仓费率 | 平仓费率 |
|------|------|------|----------|----------|----------|
| DCE.m2505 | 普通委托（Part A/B/C） | 10 | 1000 (10%) | 10 (万1) | 10 (万1) |
| DCE.b2606 | 组合腿1（Part D） | 10 | 1200 (12%) | 10 (万1) | 10 (万1) |
| DCE.b2612 | 组合腿2（Part D） | 10 | 1200 (12%) | 10 (万1) | 10 (万1) |

```c
/* 普通委托用 DCE.m2505（Part A/B/C 共用） */
#define S12_M_CODE         "DCE.m2505"
#define S12_M_MULTIPLY     10
#define S12_M_MARGIN       1000
#define S12_M_FEE_OPEN     10
#define S12_M_FEE_CLOSE    10

/* 组合委托（沿用场景10） */
#define S12_LEG1_CODE      "DCE.b2606"
#define S12_LEG2_CODE      "DCE.b2612"
#define S12_COMBO_CODE     "DCE.b2606&b2612"
#define S12_COMBO_MULTIPLY 10
#define S12_COMBO_MARGIN  1200
#define S12_COMBO_FEE     10
```

### 2.2 价格参数（×10000）

| 用途 | 价格 |
|------|------|
| DCE.m2505 开仓 | 32000000 (3200元/吨) |
| 腿1 开仓 | 35500000 (3550元/吨) |
| 腿2 开仓 | 36000000 (3600元/吨) |
| 结算价 m | 31980000 (3198元/吨) |
| 结算价 leg1 | 35600000 (3560元/吨) |
| 结算价 leg2 | 36100000 (3610元/吨) |

### 2.3 账户与工作目录

```c
#define S12_INITIAL_CASH  10000000000LL   /* 1000万 × 10000 */
#define S12_RUN_ID        "RUN_012"
#define S12_ACCOUNT_ID    "ACC_012"
#define S12_STRATEGY_ID   "STRAT_012"
#define S12_ACCOUNT_TYPE  AccountType_Futures
#define S12_TRADING_DATE  20260315
#define S12_DAY1         20260312
#define S12_DAY2         20260313
#define S12_WORK_DIR     "test_data_scenario12"
```

---

## 3. 分 Part 步骤与期望值

### Part A：普通开仓委托 PendingNew（filled=0）

**目标**：日终前存在未成交开仓挂单，日终自动 CancelFilled，释放 frozen_cash。

| 步骤 | 操作 | 期望 |
|------|------|------|
| A1 | om_init + om_set_fund_config + om_set_account_fund_config + om_trading_day_update | 初始化成功 |
| A2 | om_handle_order Long_Open m2505, 3手, PendingNew, filled=0, price=32000000 | 冻结 3×32032000≈96096000 |
| A3 | 校验 frozen_cash > 0、Fundtable、AccountFundtable | 有冻结 |
| A4 | om_handle_newprice(m, 31980000); om_trading_day_end() | 返回 0 |
| A5 | 校验 frozen_cash=0、account_frozen=0、order status=CancelFilled、cancel_volume=3、equity=初始值 | 冻结已释放、委托已撤、权益守恒 |

**期望值**：单手冻结 = 32000000×10×1000/10000 + 32000000×10×10/100000 = 32032000；3手总冻结 = 96096000。

### Part B：普通开仓 PartiallyFilled

**目标**：5手开仓部分成交2手，剩余3手冻结，日终自动 PartiallyCanceled，释放剩余冻结。

| 步骤 | 操作 | 期望 |
|------|------|------|
| B1 | 独立 init（或清理后）建立干净环境 |
| B2 | om_handle_order Long_Open m2505, 5手, PartiallyFilled, filled=2 | 成交2手扣保证金，剩余3手冻结 |
| B3 | 校验 frozen_cash > 0（约 3×32032000） | 有剩余冻结 |
| B4 | om_handle_newprice + om_trading_day_end() | 返回 0 |
| B5 | 校验 frozen_cash=0、account_frozen=0、order status=PartiallyCanceled、cancel_volume=3、pnl=0、equity=margin+avail_cash | 剩余冻结已释放、权益守恒 |

### Part C：普通平仓委托 PendingNew

**目标**：先建立 2昨+2今 持仓，挂平仓 3手 PendingNew，日终自动 CancelFilled，释放 ContractStat frozen。

| 步骤 | 操作 | 期望 |
|------|------|------|
| C1 | 独立 init，Day1 开 2手 m2505 → Day1 日终 → Day2 tradingDayUpdate → Day2 开 2手 | 2昨+2今 |
| C2 | om_handle_order Long_Close m2505, 3手, PendingNew, filled=0 | yesterday_long_frozen=2, today_long_frozen=1 |
| C3 | 校验 ContractStat、AccountContractStat frozen > 0 | 有冻结持仓 |
| C4 | om_handle_newprice(m, 31980000) + om_trading_day_end() | 返回 0 |
| C5 | 校验 today_long_frozen=0、yesterday_long_frozen=0、order status=CancelFilled | 冻结已释放、委托已撤 |

### Part D：组合开仓 PartiallyFilled

**目标**：组合委托 5手 PartiallyFilled 2手，腿委托 COMBO_001.1/.2 有剩余冻结，日终释放。

| 步骤 | 操作 | 期望 |
|------|------|------|
| D1 | 独立 init，含 DCE.b2606、DCE.b2612 FeeCodeInfo |
| D2 | om_handle_trade 腿1、腿2 各2手 |
| D3 | om_handle_order 组合委托 code=DCE.b2606&b2612, volume=5, filled=2, status=PartiallyFilled | 腿委托入库，剩余3手冻结 |
| D4 | 校验 account_frozen > 0 或 frozen_cash > 0、CombinationUnit 2条 | 有冻结 |
| D5 | om_handle_newprice(leg1/leg2 结算价) + om_trading_day_end() | 返回 0 |
| D6 | 校验 account_frozen=0、frozen_cash=0、CombinationUnit 保留2条、腿委托 PartiallyCanceled、组合主单 PartiallyCanceled | 冻结已释放、委托状态正确 |

**组合委托实现注意**：主组合单 code 含 `&`，若 processNonTerminalOrders 直接调用 OrderProcessor::process 可能失败。腿委托（order_id.1/.2）单独处理可正确释放冻结。若 Part D 失败，需在 om_service.cc 中跳过主组合单，仅处理腿委托。

### Part E（可选）：主组合单跳过逻辑

若已实现主组合单跳过：构造主组合单+腿委托均未终态，验证主组合单仅做 status 更新、腿委托被 process，日终后无剩余冻结。

---

## 4. 校验点汇总

| 校验项 | 数据源 | 断言 |
|--------|--------|------|
| 策略级 frozen_cash | FundtableStore | 日终后 = 0 |
| 账户级 account_frozen | AccountFundtableStore | 日终后 = 0 |
| 策略级 ContractStat frozen | ContractStatStore | today/yesterday_long_frozen = 0 |
| 账户级 AccountContractStat frozen | AccountContractStatStore | 同上 |
| 日志 | checkFrozenAssets | 不出现 "DETECTED remaining frozen assets" |
| 委托状态 | OrderStore | 原未终态变为 CancelFilled/PartiallyCanceled |
| 权益守恒 | 资金表 | 日终前后 equity 一致（无持仓时）或符合结算公式 |

---

## 5. 计算公式（量纲 ×10000）

```
保证金/手 = price × multiply × margin_ratio / 10000
手续费/手 = price × multiply × rate / 100000
冻结/手 = 保证金/手 + 开仓费/手（保守估算）
```

| Part | 计算示例 |
|------|----------|
| Part A | m 单手冻结 = 32000000×10×1000/10000 + 32000000×10×10/100000 = 32032000 |
| Part B | 同 Part A，剩余3手冻结 = 96096000 |
| Part C | 平仓不涉及 frozen_cash，仅 ContractStat frozen |
| Part D |  leg1/leg2 各单手冻结，组合剩余3手×两腿 |

---

## 6. 相关文档

| 主题 | 位置 |
|------|------|
| 日终结算流程 | `03-implementation/flows/settlement-flow.md` |
| 组合委托拆腿 | `../flows/combo-order-leg-split.md` |
| 场景6 | `03-implementation/scenarios/scenario6.md` |
| 场景8 | `03-implementation/scenarios/scenario8.md` |
| 场景10 | `03-implementation/scenarios/scenario10.md` |

---

*文档版本：1.1*  
*创建日期：2026-03-15*  
*修订日期：2026-03-15（Part C 合约统一为 m2505，Part D 结算价修正，补充委托状态与权益校验点）*
