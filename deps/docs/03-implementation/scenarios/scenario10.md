# 场景10设计文档：组合委托开平仓流程测试

## 1. 场景概述

### 1.1 测试目标
验证 Order Manager 在**组合委托（Combo Order）**场景下的完整业务流程正确性：

1. **组合开仓**：OmTrade 先入库 → 组合委托回报 → 拆腿生成单腿委托 → 持仓与 CombinationUnit 配对
2. **组合平仓**：使用两笔单腿平仓委托依次平掉腿1、腿2（因组合委托平仓拆腿未实现）
3. **策略级与账户级**：PositionUnit、AccountPositionUnit、ContractStat、AccountContractStat、CombinationUnit 一致性
4. **资金与权益**：margin、fee、realized_pnl、equity 守恒

### 1.2 业务背景
- **组合委托**：`code` 含 `&`，如 `DCE.b2606&b2612`，做多 = 腿1多 + 腿2空
- **时序**：成交回报必须先于委托回报（`../flows/combo-order-leg-split.md` §2）
- **平仓实现**：当前 `validateAndAggregateTrades` 仅支持 Long_Open/Short_Open，组合平仓拆腿未实现，故用两笔单腿委托平仓

### 1.3 测试范围

| 验证点 | 说明 |
|--------|------|
| OmTrade 入库 | 腿1、腿2 成交先写入 trade 表 |
| 组合委托拆腿 | om_handle_order 触发拆腿，生成 order_id.1、order_id.2 |
| 策略级持仓 | PositionUnit 腿1 5 多、腿2 5 空 |
| 账户级持仓 | AccountPositionUnit 10 条，CombinationUnit 5 条 |
| 单腿平仓 | Long_Close 腿1、Short_Close 腿2 依次平仓 |
| 权益守恒 | account_equity = 初始 - 总手续费 + 总实现盈亏 |

---

## 2. 测试数据设计

### 2.1 组合合约

| 组合 | 腿1 | 腿2 | 交易所 |
|------|-----|-----|--------|
| DCE.b2606&b2612 | DCE.b2606 | DCE.b2612 | DCE |

### 2.2 合约参数（×10000）

| 合约 | 乘数 | 保证金率 | 开仓费率 | 平仓费率 |
|------|------|----------|----------|----------|
| DCE.b2606 | 10 | 1200 (12%) | 10 (万1) | 10 (万1) |
| DCE.b2612 | 10 | 1200 (12%) | 10 (万1) | 10 (万1) |

### 2.3 价格设计（×10000）

| 阶段 | 腿1 价格 | 腿2 价格 | 组合价差 | 说明 |
|------|----------|----------|----------|------|
| 开仓 | 35500000 (3550) | 36000000 (3600) | -50.00 | 做多组合 |
| 平仓 | 35800000 (3580) | 36200000 (3620) | -40.00 | 价差向正轴移动，做多盈利 500 元 |

**做多组合盈亏**：价差从 -50 收窄到 -40（向正轴移动），盈利 = (-40 - (-50)) × 10 × 5 = 50000（5000 元 × 10000）

### 2.4 账户

```c
#define S10_RUN_ID        "RUN_010"
#define S10_ACCOUNT_ID    "ACC_010"
#define S10_STRATEGY_ID   "STRAT_010"
#define S10_TRADING_DATE  20260315
#define S10_INITIAL_CASH  10000000000LL   /* 1000万 × 10000 */
#define S10_COMBO_OPEN_ID "COMBO_001"
#define S10_COMBO_VOLUME  5
```

---

## 3. 步骤与期望值

### 步骤 1：系统初始化

| 操作 | `om_init("./test_data_scenario10")` |
|------|-------------------------------------|
| 校验 | 返回 0 |

### 步骤 2：设置资金

| 操作 | `om_set_fund_config` + `om_set_account_fund_config` |
|------|----------------------------------------------------|
| 策略级 | avail_cash=10000000000, margin=0, frozen=0, fee=0, pnl=0 |
| 账户级 | account_cash=10000000000, account_margin=0, account_frozen=0 |
| 校验 | Fundtable、AccountFundtable 初始一致 |

### 步骤 3：交易日更新

| 操作 | `om_trading_day_update(20260315)` + `om_add_fee_info` 传入两腿合约 |
|------|---------------------------------------------------------------|
| FeeCodeInfo | 含 DCE.b2606、DCE.b2612 两条，通过 om_add_fee_info 逐个传入 |
| 校验 | 返回 0 |

### 步骤 4：组合开仓 - 腿1 成交入库

| 操作 | `om_handle_trade` 腿1 |
|------|----------------------|
| OmTrade | order_id=COMBO_001, match_seqno=M1_L1, code=DCE.b2606, side=Long_Open(3), volume=5, price=35500000 |
| 校验 | 返回 0，trade 表有记录 |

### 步骤 5：组合开仓 - 腿2 成交入库

| 操作 | `om_handle_trade` 腿2 |
|------|----------------------|
| OmTrade | order_id=COMBO_001, match_seqno=M1_L2, code=DCE.b2612, side=Short_Open(5), volume=5, price=36000000 |
| 校验 | 返回 0，trade 表共 2 条（腿1+腿2） |

### 步骤 6：组合开仓 - 委托回报（Filled）

| 操作 | `om_handle_order` 组合委托 |
|------|----------------------------|
| OmOrder | code=DCE.b2606&b2612, side=Long_Open(3), volume=5, filled_volume=5, status=Filled, price=-500000 |
| 校验 | 返回 0 |
| 策略级 | PositionUnit：腿1 5 手多，腿2 5 手空；ContractStat 正确 |
| 账户级 | AccountPositionUnit 10 条（5 多 + 5 空）；CombinationUnit 5 条；account_margin、fee 正确 |
| 资金 | margin=(42600000+43200000)×5=429000000；fee=357500 |

### 步骤 7：组合平仓 - 腿1 平仓委托 Filled

| 操作 | `om_handle_order` 单腿平仓 |
|------|----------------------------|
| OmOrder | code=DCE.b2606, side=Long_Close(4), volume=5, filled_volume=5, status=Filled, price=35800000 |
| 校验 | 返回 0 |
| 持仓 | 腿1 多头减为 0；腿2 空头仍 5 手 |
| 实现盈亏 | 腿1：(3580-3550)×10×5 = 15000000（1500 元 × 10000） |
| 资金 | 释放腿1 保证金 213000000，avail 增加实现盈亏减平仓费 |

### 步骤 8：组合平仓 - 腿2 平仓委托 Filled

| 操作 | `om_handle_order` 单腿平仓 |
|------|----------------------------|
| OmOrder | code=DCE.b2612, side=Short_Close(6), volume=5, filled_volume=5, status=Filled, price=36200000 |
| 校验 | 返回 0 |
| 持仓 | 账户级、策略级均为 0 |
| 实现盈亏 | 腿2：(3600-3620)×10×5 = -10000000（-1000 元 × 10000） |
| 组合总实现盈亏 | 15000000 + (-10000000) = 5000000（500 元 × 10000） |

### 步骤 9：权益守恒

| 校验 | account_equity = 初始 - 总手续费 + 总实现盈亏 |

---

## 4. 计算公式（量纲 ×10000）

```
保证金/手 = price × multiply × margin_ratio / 10000
手续费/手 = price × multiply × rate / 100000
实现盈亏 = (close_price - hold_cost) × multiply × volume × dir_sign
```

| 步骤 | 计算示例 |
|------|----------|
| 步骤 6 | margin_leg1=42600000/手，margin_leg2=43200000/手，总 margin=429000000；fee=357500 |
| 步骤 7 | 腿1 实现盈亏=15000000，平仓费=179000，释放保证金=213000000 |
| 步骤 8 | 腿2 实现盈亏=-10000000，平仓费=181000，释放保证金=216000000 |
| 总手续费 | 357500 + 179000 + 181000 = 717500 |
| 总实现盈亏 | 5000000 |

---

## 5. 注意事项

1. **FeeCodeInfo**：需通过 `om_add_fee_info` 传入两腿的 FeeCodeInfo
2. **combo order 的 FeeCodeInfo**：`om_handle_order` 传组合 code 或任一腿的 FeeCodeInfo 均可
3. **平仓顺序**：必须先平腿1再平腿2（或反之）
4. **order_id 格式**：组合回报用 COMBO_001；单腿平仓用 CLOSE_L1_001、CLOSE_L2_001

---

## 6. 相关文档

| 主题 | 位置 |
|------|------|
| 组合委托定义 | `02-domain/combination-order.md` |
| 组合持仓模型 | `02-domain/combination-position.md` |
| 组合委托拆腿实现 | `../flows/combo-order-leg-split.md` |
| 对外 API | `03-implementation/interfaces/public-apis.md` |

---

*文档版本：1.0*  
*创建日期：2026-03-15*
