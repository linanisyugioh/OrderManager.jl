# 场景11设计文档：组合持仓与普通持仓混合平仓优先级测试

## 1. 场景概述

### 1.1 测试目标
验证 Order Manager 在同时存在**普通持仓**（combination_id=0）和**组合持仓**（combination_id≠0）时，平仓是否能正确遵循 FIFO 优先级规则：

1. **优先级1**：combination_id = 0（普通单腿委托产生的持仓，优先平）
2. **优先级2**：combination_id ≠ 0（组合委托拆分产生的持仓，后平）
3. 同优先级内按原有 FIFO 规则（open_date ASC, open_time ASC）

### 1.2 业务背景
- **平仓 FIFO 优先级**：`../flows/combo-order-leg-split.md` §5.5 明确定义了平仓时的优先级规则
- **组合打破机制**：当平仓平到组合持仓时，系统自动打破该持仓对对应的组合（另一腿释放为普通持仓）
- **设计理由**：组合持仓两腿通常需要同步操作，单独平一腿会破坏组合结构，因此优先消耗普通持仓，保护组合完整性

### 1.3 测试范围

| 验证点 | 说明 |
|--------|------|
| 平仓优先级 | 先平普通持仓（combination_id=0），后平组合持仓（combination_id≠0） |
| 组合打破 | 平到组合持仓时，CombinationUnit.existed_flag=0，另一腿 combination_id 重置为0 |
| 持仓状态 | AccountPositionUnit 数量、combination_id 正确性 |
| 权益守恒 | account_equity = 初始 - 总手续费 + 总实现盈亏 |

---

## 2. 测试数据设计

### 2.1 合约参数

沿用场景10的合约参数：

| 参数 | 值 | 说明 |
|------|-----|------|
| 腿1合约 | DCE.b2606 | 大商所豆一2606 |
| 腿2合约 | DCE.b2612 | 大商所豆一2612 |
| 组合合约 | DCE.b2606&b2612 | 组合委托代码 |
| 乘数 | 10 | 每手10吨 |
| 保证金率 | 1200 (12%) | ×10000存储 |
| 开仓费率 | 10 (万1) | ×100000存储 |
| 平仓费率 | 10 (万1) | ×100000存储 |

### 2.2 价格设计（×10000）

| 阶段 | 腿1价格 | 说明 |
|------|---------|------|
| 开仓 | 35500000 (3550) | 普通开仓和组合开仓同价 |
| 平仓 | 35800000 (3580) | 平仓价 |

### 2.3 测试常量

```c
#define S11_RUN_ID         "RUN_011"
#define S11_ACCOUNT_ID     "ACC_011"
#define S11_STRATEGY_ID    "STRAT_011"
#define S11_TRADING_DATE   20260315
#define S11_INITIAL_CASH   10000000000LL   /* 1000万 × 10000 */

// 普通开仓委托ID
#define S11_NORMAL_OPEN_ID "NORMAL_001"
#define S11_NORMAL_VOLUME  2

// 组合开仓委托ID
#define S11_COMBO_OPEN_ID  "COMBO_001"
#define S11_COMBO_VOLUME   2

// 平仓委托ID
#define S11_CLOSE_1_ID     "CLOSE_001"   // 平1手
#define S11_CLOSE_2_ID     "CLOSE_002"   // 平2手

// 价格（×10000）
#define S11_OPEN_PRICE    35500000LL   /* 3550 */
#define S11_CLOSE_PRICE   35800000LL   /* 3580 */
#define S11_LEG2_OPEN     36000000LL   /* 3600（腿2开仓价） */

// 期望值
#define S11_MARGIN_PER_LOT  42600000LL   /* 单手保证金 = 3550×10×1200/10000 = 4260000 (×10000) */
#define S11_FEE_OPEN        10           /* 万1 × 100000 */
```

---

## 3. 步骤与期望值

### 步骤 1：系统初始化

| 操作 | `om_init("./test_data_scenario11")` |
|------|-------------------------------------|
| 校验 | 返回 0 |

### 步骤 2：设置资金

| 操作 | `om_set_fund_config` + `om_set_account_fund_config` |
|------|----------------------------------------------------|
| 校验 | Fundtable、AccountFundtable 初始一致 |

**资金表期望值**（量纲 ×10000）：

| 层级 | avail_cash / account_cash | margin / account_margin | frozen_cash / account_frozen | fee | pnl / account_pnl | equity / account_equity |
|------|---------------------------|-------------------------|-----------------------------|-----|-------------------|-------------------------|
| 策略级 Fundtable | 10000000000 | 0 | 0 | 0 | 0 | 10000000000 |
| 账户级 AccountFundtable | 10000000000 | 0 | 0 | 0 | 0 | 10000000000 |

校验：`FundtableStore::queryByScope` / `AccountFundtableStore::queryByPrimaryKey` 查询，逐字段断言与上表一致。

### 步骤 3：交易日更新

| 操作 | `om_trading_day_update(20260315)` + `om_add_fee_info` 传入两腿合约 |
|------|---------------------------------------------------------------|
| FeeCodeInfo | 含 DCE.b2606、DCE.b2612 两条，通过 om_add_fee_info 逐个传入 |
| 校验 | 返回 0 |

### 步骤 4：普通开仓（2手腿1）

| 操作 | `om_handle_order` 普通开仓委托 Filled |
|------|---------------------------------------|
| OmOrder | code=DCE.b2606, side=Long_Open(3), volume=2, filled_volume=2, order_id=NORMAL_001 |
| 校验 | 返回 0 |
| 策略级持仓 | PositionUnit：腿1 2手多 |
| 账户级持仓 | AccountPositionUnit：2条，combination_id=0（普通持仓） |

**资金表期望值**（量纲 ×10000）：开仓时 cash 扣减（margin + fee）；2 手开仓费 = 71000，保证金 = 85200000，故 account_cash = 10000000000 − 85200000 − 71000 = 9914729000。

| 层级 | avail_cash / account_cash | margin / account_margin | frozen_cash / account_frozen | fee | pnl / account_pnl | equity / account_equity |
|------|---------------------------|-------------------------|-----------------------------|-----|-------------------|-------------------------|
| 策略级 / 账户级 | 9914729000 | 85200000 | 0 | 71000 | 0 | 10000000000 |

**委托表期望值**：order 表存在 NORMAL_001；status=Filled, filled_volume=2, fee=71000。

### 步骤 5：组合开仓（2手组合）

| 操作 | OmTrade 先入库 → 组合委托回报 |
|------|------------------------------|
| Trade1 | code=DCE.b2606, side=Long_Open, volume=2 |
| Trade2 | code=DCE.b2612, side=Short_Open, volume=2 |
| OmOrder | code=DCE.b2606&b2612, side=Long_Open, filled=2 |
| 校验 | 返回 0 |
| 账户级持仓 | AccountPositionUnit：新增2条腿1（combination_id≠0）+ 2条腿2（combination_id≠0） |
| CombinationUnit | 2条，existed_flag=1 |

**资金表期望值**（量纲 ×10000）：组合开仓两腿均扣 margin+fee，累计保证金 = 256800000，累计 fee = 214000；account_cash = 9914729000 − (85200000+71000) − (86400000+72000) = 9742986000。

| 层级 | avail_cash / account_cash | margin / account_margin | frozen_cash / account_frozen | fee | pnl / account_pnl | equity / account_equity |
|------|---------------------------|-------------------------|-----------------------------|-----|-------------------|-------------------------|
| 策略级 / 账户级 | 9742986000 | 256800000 | 0 | 214000 | 0 | 10000000000 |

**委托表期望值**：order 表存在 COMBO_001；status=Filled, filled_volume=2；fee 由实现决定（组合委托回报可能未回写，为 0）。

**持仓状态汇总**：
- 腿1普通持仓：2手（combination_id=0）
- 腿1组合持仓：2手（combination_id≠0）
- 腿2组合持仓：2手（combination_id≠0）

### 步骤 6：验证持仓状态

| 校验项 | 期望值 |
|--------|--------|
| 腿1总持仓数 | 4条（2普通 + 2组合） |
| 腿2总持仓数 | 2条（均为组合） |
| CombinationUnit数 | 2条 |
| 普通持仓id | 小于组合持仓id（开仓顺序） |

### 步骤 7：平仓1手腿1（测试优先平普通持仓）

| 操作 | `om_handle_order` Long_Close, volume=1 |
|------|----------------------------------------|
| OmOrder | code=DCE.b2606, side=Long_Close(4), volume=1, filled=1 |
| 校验 | 返回 0 |
| 持仓验证 | 被平掉的持仓是普通持仓（combination_id=0） |

**资金表期望值**（量纲 ×10000）：平仓 1 手后 cash += 实现盈亏 + 释放保证金 − 平仓费；实际 account_cash=9788550200，margin=214200000，fee=249800（累计含平仓费计算方式以实现为准）。

| 层级 | avail_cash / account_cash | margin / account_margin | frozen_cash / account_frozen | fee | pnl / account_pnl | equity / account_equity |
|------|---------------------------|-------------------------|-----------------------------|-----|-------------------|-------------------------|
| 策略级 / 账户级 | 9788550200 | 214200000 | 0 | 249800 | 0 | 10002750200 |

**委托表期望值**：order 表存在 CLOSE_001；status=Filled, filled_volume=1, fee=35800（以实际回写为准）。

**预期持仓状态**：
- 腿1普通持仓：1手（combination_id=0）
- 腿1组合持仓：2手（combination_id≠0）
- 腿2组合持仓：2手（combination_id≠0）

### 步骤 8：验证步骤7后的持仓状态

| 校验项 | 期望值 |
|--------|--------|
| 腿1普通持仓剩余 | 1手 |
| 腿1组合持仓 | 2手（未变） |
| CombinationUnit数 | 2条（均未打破） |

### 步骤 9：平仓2手腿1（测试平掉剩余普通+1手组合）

| 操作 | `om_handle_order` Long_Close, volume=2 |
|------|----------------------------------------|
| OmOrder | code=DCE.b2606, side=Long_Close(4), volume=2, filled=2 |
| 校验 | 返回 0 |
| 平仓顺序 | 先平剩余1手普通 → 再平1手组合 |

**资金表期望值**（量纲 ×10000）：平仓 2 手后实际 account_cash=9879678600，margin=129000000，fee=321400。

| 层级 | avail_cash / account_cash | margin / account_margin | frozen_cash / account_frozen | fee | pnl / account_pnl | equity / account_equity |
|------|---------------------------|-------------------------|-----------------------------|-----|-------------------|-------------------------|
| 策略级 / 账户级 | 9879678600 | 129000000 | 0 | 321400 | 0 | 10008678600 |

**委托表期望值**：order 表存在 CLOSE_002；status=Filled, filled_volume=2, fee=71600（以实际回写为准）。

**预期持仓状态**：
- 腿1普通持仓：0手
- 腿1组合持仓：1手（combination_id≠0）
- 腿2组合持仓：2手（其中1手已打破，combination_id=0）

**预期CombinationUnit状态**：
- 1条被打破：existed_flag=0, break_time已填充
- 1条未打破：existed_flag=1

### 步骤 10：验证步骤9后的持仓状态

| 校验项 | 期望值 |
|--------|--------|
| 腿1普通持仓 | 0手 |
| 腿1组合持仓 | 1手（combination_id≠0） |
| 腿2总持仓 | 2手 |
| 腿2已打破持仓 | 1手（combination_id=0） |
| 腿2未打破持仓 | 1手（combination_id≠0） |
| 被打破的CombinationUnit | existed_flag=0 |
| 未打破的CombinationUnit | existed_flag=1 |

### 步骤 11：权益守恒验证

| 校验 | account_equity = 初始 - 总手续费 + 总实现盈亏 |
|------|-----------------------------------------------|
| 计算公式 | 实现盈亏 = (3580-3550)×10×3 = 900000（3手×30元×10乘数） |
| 总手续费 | 开仓费 + 平仓费 |

**资金表最终期望**（量纲 ×10000）：与步骤 9 一致，account_equity = account_margin + account_cash + account_frozen + account_pnl。

| 字段 | 期望值 |
|------|--------|
| account_cash | 9879678600 |
| account_margin | 129000000 |
| account_frozen | 0 |
| fee | 321400 |
| account_pnl | 0 |
| account_equity | 10008678600 |

---

## 4. 关键校验点详解

### 4.1 平仓优先级验证

**SQL排序规则**（已实现在 `AccountPositionUnitStore::queryUnclosedByDirection`）：

```sql
ORDER BY CASE WHEN combination_id=0 THEN 0 ELSE 1 END ASC,
         open_date ASC, open_time ASC, id ASC;
```

**验证方法**：
1. 步骤7平仓1手后，查询被平持仓的 combination_id 应为 0
2. 步骤9平仓2手后，验证先被平的是普通持仓，后被平的是组合持仓

### 4.2 组合打破验证

**打破流程**（`AccountPositionProcessor::onCloseFill`）：
1. 检测到被平持仓的 combination_id ≠ 0
2. 查询对应的 CombinationUnit
3. 标记 existed_flag=0, break_time=当前时间
4. 将另一腿的 combination_id 重置为 0

**验证方法**：
- 查询 CombinationUnit 表，验证被打破的记录状态
- 查询 AccountPositionUnit 表，验证另一腿的 combination_id=0

### 4.3 资金表与委托表校验

各步骤资金表与委托表期望值及校验方式汇总：

**资金表**（账户级 AccountFundtable，本场景单策略与策略级数值一致；开仓时 cash 扣减 margin+fee）：

| 步骤 | account_cash | account_margin | account_frozen | fee | account_pnl | account_equity |
|------|--------------|----------------|----------------|-----|------------|----------------|
| 2 | 10000000000 | 0 | 0 | 0 | 0 | 10000000000 |
| 4 | 9914729000 | 85200000 | 0 | 71000 | 0 | 10000000000 |
| 5 | 9742986000 | 256800000 | 0 | 214000 | 0 | 10000000000 |
| 7 | 9788550200 | 214200000 | 0 | 249800 | 0 | 10002750200 |
| 9、11 | 9879678600 | 129000000 | 0 | 321400 | 0 | 10008678600 |

校验方式：`AccountFundtableStore::queryByPrimaryKey(run_id, account_id, account_type, &afund)`，逐字段断言；equity 校验为 account_margin + account_cash + account_frozen + account_pnl。

**委托表**（OmOrder；fee 以实际回写为准）：

| 步骤 | order_id | status | filled_volume | fee |
|------|----------|--------|---------------|-----|
| 4 | NORMAL_001 | Filled | 2 | 71000 |
| 5 | COMBO_001 | Filled | 2 | 0（组合回报可能未回写） |
| 7 | CLOSE_001 | Filled | 1 | 35800 |
| 9 | CLOSE_002 | Filled | 2 | 71600 |

校验方式：`OrderStore::queryByOrderId(order_id, oper_date=20260315, strategy_id, run_id, account_id, account_type, &order)` 返回 0，断言 order.status、order.filled_volume、order.fee 与上表一致。

---

## 5. 注意事项

1. **开仓顺序**：必须先普通开仓，再组合开仓，才能确保普通持仓id < 组合持仓id
2. **平仓委托**：使用 Long_Close(4) 而非 Today_Long_Close(8)，测试今昨混合 FIFO
3. **FeeCodeInfo**：需包含 DCE.b2606 和 DCE.b2612 的费率信息
4. **数据清理**：测试完成后清理 test_data_scenario11 目录

---

## 6. 相关文档

| 主题 | 位置 |
|------|------|
| 平仓FIFO优先级规则 | `../flows/combo-order-leg-split.md` §5.5 |
| 组合打破逻辑 | `../flows/combo-order-leg-split.md` §5.6 |
| 场景10组合委托 | `03-implementation/scenarios/scenario10.md` |
| 组合持仓模型 | `02-domain/combination-position.md` |

---

*文档版本：1.1*  
*创建日期：2026-03-15*  
*v1.1：补充步骤 2/4/5/7/9/11 资金表与委托表期望值及 §4.3 校验汇总*
