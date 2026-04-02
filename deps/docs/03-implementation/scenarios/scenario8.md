# 场景8设计文档：平仓委托撤单流程测试

## 1. 场景概述

### 1.1 测试目标
验证 Order Manager 在**平仓委托撤单**场景下的正确性：

1. **平仓委托 PendingNew → CancelFilled**：平仓委托挂单后撤销，释放 ContractStat 冻结量
2. **onCloseOrderCancel 触发路径**：确保仅平仓委托（`!is_open`）撤单时调用该逻辑（开仓撤单不调用）
3. **LIFO 释放规则**：Long_Close/Short_Close 撤单时，按「今仓先放、昨仓后放」正确释放 frozen
4. **指定平今/平昨撤单**：上期所 Today_Long_Close、PreDay_Long_Close 撤单时，直接释放对应桶的 frozen，无需 LIFO 拆分
5. **策略级与账户级一致性**：PositionProcessor 与 AccountPositionProcessor 的 onCloseOrderCancel 行为一致

### 1.2 业务背景
- **触发条件**：`order_processor.cc` 中 `if (ctx.delta_cancel_volume > 0 && !is_open)` 时执行 onCloseOrderCancel
- **与开仓撤单区别**：场景6中的撤单为开仓委托（Long_Open），不会调用 onCloseOrderCancel
- **LIFO 必要性**：冻结时「昨仓优先」，撤单时「今仓先放」——顺序相反，避免 frozen 与 volume 不一致

### 1.3 测试范围
| 验证点 | 说明 |
|--------|------|
| ContractStat | today_long_frozen、yesterday_long_frozen 释放后归零 |
| AccountContractStat | 同上，账户级与策略级一致 |
| 持仓表 | 撤单不改变 PositionUnit，仅释放 frozen |
| 资金表 | 平仓撤单不涉及资金变化（平仓不冻结资金） |
| LIFO 边界 | 当 vol > today_frozen 时，正确拆分今/昨释放量 |

---

## 2. 测试数据设计

### 2.1 合约参数

| 合约 | 交易所 | 乘数 | 保证金率 | 开仓费率 | 平仓费率 | 说明 |
|------|--------|------|----------|----------|----------|------|
| DCE.y2506 | 大商所 | 10吨/手 | 12% | 万1 | 万1 | 豆油期货（Long_Close） |
| SHFE.au2506 | 上期所 | 1000克/手 | 12% | 万1 | 万1 | 黄金期货（Today/PreDay_Long_Close） |

```c
/* DCE 豆油（Part 1：LIFO 撤单） */
#define S8_TEST_CODE         "DCE.y2506"
#define S8_TEST_MULTIPLY     10
#define S8_TEST_MARGIN_RATIO 1200
#define S8_TEST_OPEN_RATE    10
#define S8_TEST_CLOSE_RATE   10

/* SHFE 黄金（Part 2：指定平今/平昨撤单） */
#define S8_SHFE_CODE         "SHFE.au2506"
#define S8_SHFE_MULTIPLY     1000
#define S8_SHFE_MARGIN       1200
#define S8_SHFE_FEE_OPEN     10
#define S8_SHFE_FEE_CLOSE    10
```

### 2.2 交易日期与持仓设计

| 日期 | 用途 | 持仓构成 |
|------|------|----------|
| Day1 20260312 | 昨仓开仓日 | 开 2 手 → 形成昨仓 2 手 |
| Day2 20260313 | 当前交易日 | 开 2 手 → 形成今仓 2 手；合计 2 昨 + 2 今 |

**关键设计**：今仓 + 昨仓共存，以便 Long_Close 冻结时按「昨仓优先」分配，撤单时按「今仓先放」释放，验证 LIFO 拆分逻辑。

### 2.3 价格参数（×10000）

| 用途 | 价格 | 说明 |
|------|------|------|
| Day1 开仓价 | 7500000 | 7500.00 元/吨 |
| Day2 开仓价 | 7520000 | 7520.00 元/吨 |
| 平仓委托价 | 7600000 | 7600.00 元/吨（未成交即撤单，价格仅用于 checkCloseVolume 时的逻辑） |

### 2.4 账户与初始资金
```c
#define S8_INITIAL_CASH   10000000000LL   /* 1000 万 × 10000 */
#define S8_RUN_ID         "RUN_008"
#define S8_ACCOUNT_ID     "ACC_008"
#define S8_STRATEGY_ID    "STRAT_008"
#define S8_ACCOUNT_TYPE   AccountType_Futures
```

### 2.5 冻结/释放规则（复习）

| 阶段 | 规则 | Long_Close 3 手（2 昨 + 2 今） |
|------|------|-------------------------------|
| 冻结（onCloseOrderNew） | 昨仓优先 | ylv_f=2, tlv_f=1 → 昨冻 2，今冻 1 |
| 释放（onCloseOrderCancel） | 今仓先放 | vol=3, today_frozen=1 → tlv_f=-1, ylv_f=-2 |

**LIFO 边界用例**：撤单 3 手时，today_frozen=1 < 3，必须从今仓释放 1、昨仓释放 2，否则会出错（旧实现曾错误地 tlv_f=-3，导致 today_long_frozen 变为负）。

---

## 3. 步骤与期望值

### 步骤 1：系统初始化
| 操作 | `om_init("./test_data_scenario8")` |
|------|-------------------------------------|
| 校验 | init 返回 0 |

### 步骤 2：设置资金账户
| 操作 | `om_set_fund_config` + `om_set_account_fund_config` |
|------|----------------------------------------------------|
| 策略级/账户级 | avail_cash=10000000000, margin=0, frozen=0, fee=0, pnl=0 |
| 校验 | 策略级 Fundtable、账户级 AccountFundtable 初始一致 |

### 步骤 3：Day1 交易日更新
| 操作 | `om_trading_day_update(20260312)` + `om_add_fee_info` |
|------|---------------------------------------------|
| 校验 | 成功 |

### 步骤 4：Day1 开仓 2 手（形成昨仓）
| 操作 | `om_handle_order` side=Long_Open, volume=2, status=Filled, filled_volume=2, price=7500000 |
|------|----------------------------------------------------------------------------------------|
| 资金表 | 扣保证金、手续费，margin=18000000, fee=15000 |
| 持仓表 | 2 条 PositionUnit，open_date=20260312 |
| ContractStat | yesterday_long_volume=0（当日开仓为今仓，日终后变昨）→ 需配合 Day2 初的「昨仓」生成方式 |

**说明**：标准流程下 Day1 开仓为今仓，Day2 经 tradingDayUpdate 后变为昨仓。本场景可简化：
- **方案 A**：Day1 开 2 手成交，Day2 更新交易日后，这 2 手自动变为昨仓；Day2 再开 2 手为今仓。
- **方案 B**：参考场景3，Day1 手动插入 2 手昨仓，Day2 通过 om_handle_order 开 2 手今仓。

本设计采用**方案 A**，与现有 dayinit 逻辑一致。

### 步骤 5：Day1 日终结算（可选）
| 操作 | `om_trading_day_end()` 或 Day2 前的交易日更新 |
|------|---------------------------------------------|
| 说明 | 将 Day1 今仓转为昨仓，yesterday_long_volume=2 |

### 步骤 6：Day2 交易日更新
| 操作 | `om_trading_day_update(20260313)` + `om_add_fee_info` |
|------|---------------------------------------------|
| 校验 | 成功；昨仓=2，今仓=0 |

### 步骤 7：Day2 开仓 2 手（形成今仓）
| 操作 | `om_handle_order` side=Long_Open, volume=2, status=Filled, filled_volume=2, price=7520000 |
|------|---------------------------------------------------------------------------------------|
| 持仓表 | 累计 4 条（2 昨 + 2 今） |
| ContractStat | yesterday_long_volume=2, today_long_volume=2, 冻结均为 0 |
| AccountContractStat | 同上（单策略场景下与策略级一致） |
| 校验 | today_long_frozen=0, yesterday_long_frozen=0 |

### 步骤 8：平仓委托 PendingNew（Long_Close，3 手）
| 操作 | `om_handle_order` side=Long_Close, volume=3, status=PendingNew, filled_volume=0 |
|------|--------------------------------------------------------------------------------|
| 冻结规则 | 昨仓优先：yesterday_avail=2 → ylv_f=2；不足 1 手从今仓 → tlv_f=1 |
| ContractStat | yesterday_long_frozen=2, today_long_frozen=1 |
| AccountContractStat | 同上 |
| 资金表 | 不变（平仓不冻结资金） |
| 校验 | today_long_frozen=1, yesterday_long_frozen=2 |

### 步骤 9：平仓委托撤单 CancelFilled（3 手）
| 操作 | `om_handle_order` status=CancelFilled, cancel_volume=3 |
|------|-------------------------------------------------------|
| 释放规则 | LIFO 今仓先放：today_frozen=1 → tlv_f=-1；剩余 2 从昨仓 → ylv_f=-2 |
| ContractStat | today_long_frozen=0, yesterday_long_frozen=0 |
| AccountContractStat | 同上 |
| 持仓表 | 不变，仍为 4 条（2 昨 + 2 今） |
| 校验 | today_long_frozen=0, yesterday_long_frozen=0；策略级与账户级一致 |

### 步骤 10：平仓成交验证（可选，证明撤单后持仓可再次挂单）
| 操作 | 再次挂平仓委托 PendingNew（2 手）→ Filled |
|------|------------------------------------------|
| 说明 | 验证撤单后 frozen 已正确释放，可重新挂单并成交 |
| 校验 | 成交后持仓减少，资金变化正确 |

---

## 4. 上期所撤单测试（指定平今/平昨）

上期所支持**指定平今**（Today_Long_Close）和**指定平昨**（PreDay_Long_Close），撤单时释放逻辑与 DCE Long_Close 不同：直接释放对应桶的 frozen，无需查 stat 做 LIFO 拆分。本小节验证这两种 side 的撤单计算正确性。

### 4.1 合约与数据

| 合约 | 交易所 | 说明 |
|------|--------|------|
| SHFE.au2506 | 上期所 | 黄金期货，支持 Today_Long_Close / PreDay_Long_Close |

**持仓前提**：与 Part 1 相同，需先形成 2 昨 + 2 今（可用 SHFE.au2506 单独建仓，或复用 Part 1 的 work_dir 追加 SHFE 开仓）。为简化，可独立初始化：Day1 开 2 手 → Day2 更新 → Day2 开 2 手 → 2 昨 + 2 今。

### 4.2 指定平今撤单

| 步骤 | 操作 | 期望 |
|------|------|------|
| 11a | Today_Long_Close 1 手 PendingNew | today_long_frozen=1, yesterday_long_frozen=0 |
| 11b | CancelFilled，cancel_volume=1 | today_long_frozen=0；释放 tlv_f=-1，直接对应今仓桶 |
| 校验 | ContractStat / AccountContractStat | today_long_frozen=0，策略级与账户级一致 |

**验证点**：Today_Long_Close 撤单时，`tlv_f = -delta_cancel_volume`，不涉及昨仓，无需 LIFO 拆分。

### 4.3 指定平昨撤单

| 步骤 | 操作 | 期望 |
|------|------|------|
| 12a | PreDay_Long_Close 1 手 PendingNew | yesterday_long_frozen=1, today_long_frozen=0 |
| 12b | CancelFilled，cancel_volume=1 | yesterday_long_frozen=0；释放 ylv_f=-1，直接对应昨仓桶 |
| 校验 | ContractStat / AccountContractStat | yesterday_long_frozen=0，策略级与账户级一致 |

**验证点**：PreDay_Long_Close 撤单时，`ylv_f = -delta_cancel_volume`，不涉及今仓，无需 LIFO 拆分。

### 4.4 与 DCE Long_Close 的对比

| side | 冻结目标 | 撤单释放 | 是否需查 stat |
|------|----------|----------|---------------|
| Today_Long_Close | 仅今仓 | tlv_f=-vol | 否 |
| PreDay_Long_Close | 仅昨仓 | ylv_f=-vol | 否 |
| Long_Close（DCE） | 昨仓优先，可跨今昨 | LIFO：今仓先放 | 是 |

---

## 5. 计算公式（量纲 ×10000）

```
保证金/手 = price × multiply × margin_ratio / 10000
开仓费/手 = price × multiply × open_rate / 100000
平仓费/手 = price × multiply × close_rate / 100000
```

| 步骤 | 计算示例 |
|------|----------|
| Day1 开仓 2 手 | 保证金=7500000×10×1200/10000=9000000/手；总 margin=18000000 |
| Day2 开仓 2 手 | 保证金=7520000×10×1200/10000=9024000/手；总 margin=18048000 |
| 平仓撤单 | 无资金变化，仅 frozen 释放 |

---

## 6. 边界情况与注意事项

### 6.1 LIFO 边界
- **vol ≤ today_frozen**：全部从今仓释放，如 Today_Long_Close 撤 1 手时 tlv_f=-1
- **vol > today_frozen**：今仓全放，剩余从昨仓放，如 Long_Close 撤 3 手（今冻 1、昨冻 2）时 tlv_f=-1, ylv_f=-2

### 6.2 明确今/昨的 side
- **Today_Long_Close / PreDay_Long_Close**：撤单时直接对应 tlv_f 或 ylv_f，无需查 stat 拆分
- **Long_Close / Short_Close**：必须查 ContractStat，按 LIFO 拆分

### 6.3 与场景 6 的区别
| 场景 | 撤单委托类型 | 是否调用 onCloseOrderCancel |
|------|-------------|---------------------------|
| 场景 6 | Long_Open（开仓） | 否 |
| 场景 8 | Long_Close（平仓） | 是 |

---

## 7. 实现检查清单

- [ ] 步骤 1～2：初始化、资金配置
- [ ] 步骤 3～7：跨日开仓，形成 2 昨 + 2 今
- [ ] 步骤 8：Long_Close 3 手 PendingNew，校验 frozen 分配（昨 2 + 今 1）
- [ ] 步骤 9：CancelFilled，校验 LIFO 释放后 frozen=0
- [ ] 策略级 ContractStat 与账户级 AccountContractStat 每步一致
- [ ] 可选：步骤 10 再次挂单成交，验证撤单后状态正确
- [ ] **上期所**：步骤 11a/11b Today_Long_Close 1 手 PendingNew → CancelFilled，校验 today_long_frozen 归零
- [ ] **上期所**：步骤 12a/12b PreDay_Long_Close 1 手 PendingNew → CancelFilled，校验 yesterday_long_frozen 归零
