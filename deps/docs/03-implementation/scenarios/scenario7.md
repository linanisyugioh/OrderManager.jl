# 场景7设计文档：单账户双策略开平流程测试

## 1. 场景概述

### 1.1 测试目标
验证 Order Manager 在**同一交易日、同一账户、双策略**场景下的完整业务流程正确性：

1. 单日流程：交易日初始化 → 策略A开平 → 策略B开平
2. 双策略：同一账户（ACC_007）下两个策略（STRAT_A、STRAT_B）独立执行开仓、平仓
3. 策略级与账户级双轨校验：每一步同时校验 Fundtable（策略级）与 AccountFundtable（账户级）
4. 持仓与资金：策略级持仓/资金、账户级持仓/资金、合约统计的完整一致性

### 1.2 业务背景
- 实盘场景：一个资金账户下运行多个策略，各策略独立下单，资金池共享
- 策略级：各策略有独立的 Fundtable、PositionUnit、ContractStat
- 账户级：AccountFundtable、AccountPositionUnit、AccountContractStat 汇总全账户
- 校验约束：`account_* >= Σ(strategy_*)`（见 `02-domain/account-fund.md`、`03-implementation/flows/dayinit-flow.md`）

### 1.3 测试范围
| 验证点 | 策略级 | 账户级 |
|--------|--------|--------|
| 资金表 | Fundtable（avail_cash, margin, frozen_cash, fee, pnl, equity） | AccountFundtable（account_cash, account_margin, account_frozen, fee, account_pnl, account_equity） |
| 持仓表 | PositionUnit 数量、hold_cost、margin、pnl、strategy_id | AccountPositionUnit 数量、hold_cost、margin、pnl（无 strategy_id） |
| 合约统计 | ContractStat（含 strategy_id） | AccountContractStat（无 strategy_id，同合约跨策略汇总） |
| 权益守恒 | 各策略独立守恒 | 账户总权益 = 初始 - 总手续费 + 总实现盈亏 |

---

## 2. 测试数据设计

### 2.1 合约参数

| 合约 | 交易所 | 乘数 | 保证金率 | 开仓费率 | 平仓费率 | 说明 |
|------|--------|------|----------|----------|----------|------|
| SHFE.au2506 | 上期所 | 1 | 12% | 万1 | 万1 | 黄金期货（乘数1便于计算验证） |

```c
#define S7_AU_CODE       "SHFE.au2506"
#define S7_AU_MULTIPLY   1
#define S7_AU_MARGIN     1200    /* 12% × 10000 */
#define S7_AU_FEE_OPEN   10      /* 万1 × 100000 */
#define S7_AU_FEE_CLOSE  10
```

### 2.2 价格参数（×10000）

| 用途 | 价格 | 说明 |
|------|------|------|
| 策略A 开仓价 | 5000000 | 500.00元/克 |
| 策略B 开仓价 | 5020000 | 502.00元/克 |
| 策略A 平仓价 | 5050000 | 505.00元/克（+5元盈利） |
| 策略B 平仓价 | 5060000 | 506.00元/克（+4元盈利） |
| 行情价（可选） | 5040000 | 盘中刷新浮动盈亏用 |

### 2.3 账户与策略配置

```c
#define S7_RUN_ID         "RUN_007"
#define S7_ACCOUNT_ID     "ACC_007"
#define S7_STRAT_A        "STRAT_A"
#define S7_STRAT_B        "STRAT_B"
#define S7_TRADING_DATE   20260314
#define S7_VOLUME         2       /* 每策略各2手 */
#define S7_INITIAL_PER_STRAT  5000000000LL   /* 每策略50万 × 10000 */
#define S7_ACCOUNT_TOTAL     10000000000LL   /* 账户总100万 × 10000 */
```

### 2.4 计算公式（统一量纲 ×10000）

```
保证金/手 = price × multiply × margin_ratio / 10000
开仓费/手 = price × multiply × open_rate / 100000
平仓费/手 = price × multiply × close_rate / 100000
浮动盈亏/手 = (current_price - hold_cost) × multiply × dir_sign
实现盈亏/手 = (close_price - hold_cost) × multiply × dir_sign
equity = margin + avail_cash + frozen_cash + pnl
```

---

## 3. 步骤与期望值（每一步均含策略级+账户级校验）

### 步骤 1：系统初始化
| 操作 | `om_init("test_scenario7_work")` |
|------|----------------------------------|
| 策略级 | 无 Fundtable 记录 |
| 账户级 | 无 AccountFundtable 记录 |
| 持仓表 | 空（策略级+账户级） |
| **校验** | init 返回 0，策略级/账户级持仓数均为 0 |

---

### 步骤 2：设置策略A资金配置
| 操作 | `om_set_fund_config(&fund)`，strategy_id=STRAT_A，avail_cash=5000000000 |
|------|---------------------------------------------------------------------------|
| 策略级 | Fundtable(STRAT_A): avail_cash=5000000000, margin=0, frozen=0, fee=0, pnl=0, equity=5000000000 |
| 账户级 | 尚未配置 |
| **校验** | 查询 Fundtable(run_id, account_id, STRAT_A)，avail_cash=5000000000 |

---

### 步骤 3：设置策略B资金配置
| 操作 | `om_set_fund_config(&fund)`，strategy_id=STRAT_B，avail_cash=5000000000 |
|------|---------------------------------------------------------------------------|
| 策略级 | Fundtable(STRAT_B): avail_cash=5000000000, margin=0, frozen=0, fee=0, pnl=0, equity=5000000000 |
| 账户级 | 尚未配置 |
| **校验** | 查询 Fundtable(run_id, account_id, STRAT_B)，avail_cash=5000000000 |

---

### 步骤 4：设置账户级资金配置
| 操作 | `om_set_account_fund_config(&acct_fund)`，account_cash=10000000000 |
|------|---------------------------------------------------------------------|
| 账户级 | AccountFundtable: account_cash=10000000000, account_margin=0, account_frozen=0, fee=0, account_pnl=0, account_equity=10000000000 |
| **校验** | 查询 AccountFundtable，account_cash=10000000000，account_equity=10000000000 |

---

### 步骤 5：交易日初始化
| 操作 | `om_trading_day_update(20260314)` + `om_add_fee_info` |
|------|---------------------------------------------|
| 策略级 | Fundtable 不变 |
| 账户级 | AccountFundtable 不变 |
| 校验约束 | 内部执行 account_* >= Σ(strategy_*)，1000万 >= 500万+500万，通过 |
| **校验** | 返回 0，trading_date=20260314 |

---

### 步骤 6：策略A 开仓委托 PendingNew（2手 @500）
| 操作 | `om_handle_order` strategy_id=STRAT_A, side=Long_Open, status=PendingNew, volume=2, filled=0, price=5000000 |
|------|---------------------------------------------------------------------------------------------------------------|
| 单手冻结 | 5000000×1×1200/10000 + 5000000×1×10/100000 = 600000 + 500 = 600500 |
| 总冻结 | 600500 × 2 = 1201000 |
| **策略级 STRAT_A** | avail_cash=4998799000, frozen_cash=1201000, margin=0, fee=0, pnl=0, equity=5000000000 |
| **策略级 STRAT_B** | 不变，avail_cash=5000000000, frozen_cash=0 |
| **账户级** | account_cash=9998799000, account_frozen=1201000, account_margin=0, fee=0, account_pnl=0, account_equity=10000000000 |
| 持仓表 | 策略级 0 条，账户级 0 条（PendingNew 不创建持仓） |
| **校验** | STRAT_A frozen_cash=1201000；STRAT_B 不变；account_frozen=1201000；持仓数=0 |

---

### 步骤 7：策略A 开仓成交 Filled（2手 @500）
| 操作 | `om_handle_order` status=Filled, filled_volume=2 |
|------|--------------------------------------------------|
| 单手保证金 | 600000 | 单手手续费 | 500 |
| **策略级 STRAT_A** | avail_cash=4998799000, frozen_cash=0, margin=1200000, fee=1000, pnl=0, equity=4999999000 |
| **策略级 STRAT_B** | 不变 |
| **账户级** | account_cash=9998799000, account_frozen=0, account_margin=1200000, fee=1000, account_pnl=0, account_equity=9999999000 |
| 策略级持仓 | STRAT_A: 2 条 PositionUnit，hold_cost=5000000, margin=600000/条 |
| 账户级持仓 | 2 条 AccountPositionUnit，hold_cost=5000000 |
| ContractStat | STRAT_A: today_long_volume=2；STRAT_B: 无记录 |
| AccountContractStat | today_long_volume=2 |
| **校验** | STRAT_A margin=1200000, fee=1000, 持仓数=2；账户级 account_margin=1200000；ContractStat/AccountContractStat today_long=2 |

---

### 步骤 8：策略B 开仓委托 PendingNew（2手 @502）
| 操作 | `om_handle_order` strategy_id=STRAT_B, side=Long_Open, status=PendingNew, volume=2, filled=0, price=5020000 |
|------|---------------------------------------------------------------------------------------------------------------|
| 单手冻结 | 5020000×1×1200/10000 + 5020000×1×10/100000 = 602400 + 502 = 602902 |
| 总冻结 | 602902 × 2 = 1205804 |
| **策略级 STRAT_A** | 不变 |
| **策略级 STRAT_B** | avail_cash=4998794196, frozen_cash=1205804, margin=0, fee=0, pnl=0 |
| **账户级** | account_cash=9997593196, account_frozen=1205804, account_margin=1200000, fee=1000 |
| **校验** | STRAT_B frozen_cash=1205804；account_frozen=1205804；account_margin 仍为 1200000（STRAT_B 尚未成交） |

---

### 步骤 9：策略B 开仓成交 Filled（2手 @502）
| 操作 | `om_handle_order` status=Filled, filled_volume=2 |
|------|--------------------------------------------------|
| 单手保证金 | 602400 | 单手手续费 | 502 |
| **策略级 STRAT_A** | 不变，margin=1200000, fee=1000 |
| **策略级 STRAT_B** | avail_cash=4998794196, frozen_cash=0, margin=1204800, fee=1004, pnl=0 |
| **账户级** | account_cash=9997593196, account_frozen=0, account_margin=2404800, fee=2004, account_pnl=0 |
| 策略级持仓 | STRAT_A 2条 + STRAT_B 2条，共 4 条 |
| 账户级持仓 | 4 条 AccountPositionUnit（2条来自STRAT_A@500，2条来自STRAT_B@502） |
| ContractStat | STRAT_A: today_long=2；STRAT_B: today_long=2 |
| AccountContractStat | today_long_volume=4 |
| **校验** | STRAT_B margin=1204800, fee=1004；account_margin=2404800, fee=2004；策略级持仓共4条，账户级4条；AccountContractStat today_long=4 |

---

### 步骤 10：行情更新（可选，用于浮动盈亏校验）
| 操作 | `om_handle_newprice("SHFE.au2506", 5040000)` |
|------|---------------------------------------------|
| STRAT_A 浮盈 | (5040000-5000000)×1×2 = 80000 |
| STRAT_B 浮盈 | (5040000-5020000)×1×2 = 40000 |
| **策略级 STRAT_A** | pnl=80000, equity=5000079000 |
| **策略级 STRAT_B** | pnl=40000, equity=4999994196 |
| **账户级** | account_cash=9997593196, account_pnl=120000, account_equity=10000117596 |
| **校验** | STRAT_A pnl=80000；STRAT_B pnl=40000；account_pnl=120000 |

---

### 步骤 11：策略A 平仓委托 PendingNew（2手 Today_Long_Close）
| 操作 | `om_handle_order` strategy_id=STRAT_A, side=Today_Long_Close, status=PendingNew, volume=2 |
|------|---------------------------------------------------------------------------------------------|
| 资金表 | 不变 |
| ContractStat | STRAT_A: today_long=2, today_long_frozen=2 |
| AccountContractStat | today_long_volume=4, today_long_frozen=2 |
| **校验** | STRAT_A ContractStat today_long_frozen=2；AccountContractStat today_long_frozen=2 |

---

### 步骤 12：策略A 平仓成交 Filled（2手 @505）
| 操作 | `om_handle_order` status=Filled, filled_volume=2 |
|------|--------------------------------------------------|
| 单手平仓费 | 5050000×1×10/100000 = 505 |
| 单手实现盈亏 | (5050000-5000000)×1 = 50000 |
| 公式 | avail 增加 = **实现盈亏 + 释放保证金 - 平仓费**（实现中 onCloseFill 写入 delta_avail） |
| **策略级 STRAT_A** | avail_cash=4998799000+100000+1200000-1010=5000097990, margin=0, fee=2010, pnl=0, equity=5000097990 |
| **策略级 STRAT_B** | 不变，margin=1204800, 2条持仓 |
| **账户级** | account_cash 增加 = +1200000(释放保证金)+100000(实现盈亏)-1010(平仓费)=+1298990 → 9997593196+1298990=9998892186；account_margin=1204800, fee=3014, account_pnl=40000（仅STRAT_B） |
| 策略级持仓 | STRAT_A: 0 条；STRAT_B: 2 条 |
| 账户级持仓 | 2 条（来自 STRAT_B） |
| ContractStat | STRAT_A: today_long=0；STRAT_B: today_long=2 |
| AccountContractStat | today_long_volume=2 |
| **校验** | STRAT_A margin=0, fee=2010, 持仓数=0；STRAT_B 不变；account_margin=1204800, fee=3014, account_pnl=40000；账户级持仓2条 |

---

### 步骤 13：策略B 平仓委托 PendingNew（2手 Today_Long_Close）
| 操作 | `om_handle_order` strategy_id=STRAT_B, side=Today_Long_Close, status=PendingNew, volume=2 |
|------|---------------------------------------------------------------------------------------------|
| 资金表 | 不变 |
| ContractStat | STRAT_B: today_long=2, today_long_frozen=2 |
| AccountContractStat | today_long_volume=2, today_long_frozen=2 |
| **校验** | STRAT_B today_long_frozen=2；AccountContractStat today_long_frozen=2 |

---

### 步骤 14：策略B 平仓成交 Filled（2手 @506）
| 操作 | `om_handle_order` status=Filled, filled_volume=2 |
|------|--------------------------------------------------|
| 单手平仓费 | 5060000×1×10/100000 = 506 |
| 单手实现盈亏 | (5060000-5020000)×1 = 40000 |
| **策略级 STRAT_A** | 不变 |
| **策略级 STRAT_B** | avail_cash=4998794196+1204800(释放)+80000-1012=5000077984, margin=0, fee=2016, pnl=0, equity=5000077984 |
| **账户级** | account_cash=10000175974, account_margin=0, fee=4026, account_pnl=0；account_equity=10000175974（守恒：10000000000-4026+180000） |
| 策略级持仓 | STRAT_A: 0 条；STRAT_B: 0 条 |
| 账户级持仓 | 0 条 |
| ContractStat | STRAT_A: 无记录；STRAT_B: today_long=0 |
| AccountContractStat | today_long_volume=0 |
| **校验** | 两策略 margin=0, 持仓数=0；account_margin=0, account_pnl=0；账户级持仓0条 |

---

### 步骤 15：权益守恒验证
| 校验项 | 策略级 | 账户级 |
|--------|--------|--------|
| 最终权益 | STRAT_A equity=5000097990, STRAT_B equity=5000077984 | account_equity=10000175974（由守恒式验证） |
| 守恒式 | 策略A: 5000097990 = 5000000000 - 2010 + 100000 ✓ | 账户: 10000175974 = 10000000000 - 4026 + 180000 ✓ |
| 策略B: 5000077984 = 5000000000 - 2016 + 80000 ✓ | |
| 总手续费 | 2010 + 2016 = 4026 | 4026 |
| 总实现盈亏 | 100000 + 80000 = 180000 | 180000 |
| **校验** | 各策略权益守恒；账户权益 = 初始 - 总手续费 + 总实现盈亏；account_equity 应与 10000175974 一致 |

---

## 4. 校验点汇总

### 4.1 每步必校验（策略级）
| 步骤 | 校验内容 |
|------|----------|
| 6~9 | Fundtable(STRAT_A)、Fundtable(STRAT_B)：avail_cash, margin, frozen_cash, fee, pnl, equity |
| 6~14 | PositionUnit：未平仓数量、hold_cost、strategy_id；ContractStat：today_long、today_long_frozen |

### 4.2 每步必校验（账户级）
| 步骤 | 校验内容 |
|------|----------|
| 4, 6~14 | AccountFundtable：account_cash, account_margin, account_frozen, fee, account_pnl, account_equity |
| 7~14 | AccountPositionUnit：未平仓数量、hold_cost；AccountContractStat：today_long、today_long_frozen |

### 4.3 一致性校验
| 校验项 | 说明 |
|--------|------|
| 账户资金 ≥ 策略聚合 | tradingDayUpdate 时 account_cash ≥ Σ(avail_cash)、account_margin ≥ Σ(margin)、account_equity ≥ Σ(equity) |
| 账户持仓汇总 | 账户级未平仓数 = 各策略未平仓数之和（同合约下） |
| 账户合约统计 | AccountContractStat.today_long = Σ ContractStat.today_long（同合约） |

### 4.4 最终校验
| 校验项 | 说明 |
|--------|------|
| 持仓清空 | 策略级、账户级持仓均为 0 |
| 权益守恒 | 各策略独立守恒；账户总权益 = 初始 - 总手续费 + 总实现盈亏 |

---

## 5. 委托状态与交易所覆盖

| 状态 | 出现场景 |
|------|----------|
| PendingNew | 策略A/B 开仓、平仓各一次 |
| Filled | 策略A/B 开仓、平仓各一次 |
| 交易所 | SHFE（平今 Today_Long_Close） |

---

## 6. 实现注意事项

1. **FeeCodeInfo**：om_add_fee_info 传入 SHFE.au2506 的 FeeCodeInfo；handleOrder 时也需传入
2. **OmOrder 作用域**：每笔委托必须指定正确的 strategy_id
3. **资金配置时序**：om_set_fund_config(STRAT_A) → om_set_fund_config(STRAT_B) → om_set_account_fund_config
4. **账户资金约束**：account 初值 ≥ 两策略初值之和
5. **辅助函数**：
   - `s7_verifyFundtable(run_id, account_id, strategy_id, expected)`：策略级资金表校验
   - `s7_verifyAccountFundtable(run_id, account_id, expected)`：账户级资金表校验
   - `s7_verifyPositionCount(run_id, account_id, strategy_id, expected)`：策略级持仓数
   - `s7_verifyAccountPositionCount(run_id, account_id, expected)`：账户级持仓数
   - `s7_verifyContractStat(run_id, account_id, strategy_id, code, expected)`：策略级合约统计
   - `s7_verifyAccountContractStat(run_id, account_id, code, expected)`：账户级合约统计

---

## 7. 测试实现说明

### 7.1 测试文件
- **实现文件**：`test/test_scenario7.h`
- **入口**：`run_scenario7_tests()`，由 `test/test.cc` 调用

### 7.2 运行方式
```bash
cd build && cmake --build . --config Debug
./bin/Debug/om-test.exe    # Windows: build\bin\Debug\om-test.exe
```

### 7.3 工作目录
- 使用独立目录 `test_scenario7_work`，测试前后自动创建/清理

### 7.4 步骤与校验矩阵
| 阶段 | 步骤数 | 策略级校验 | 账户级校验 |
|------|--------|------------|------------|
| 初始化 | 1~5 | Fundtable×2 | AccountFundtable |
| 策略A开仓 | 6~7 | STRAT_A 资金+持仓+ContractStat | account 资金+持仓+AccountContractStat |
| 策略B开仓 | 8~9 | STRAT_B 资金+持仓+ContractStat | account 资金+持仓+AccountContractStat |
| 行情（可选） | 10 | 两策略 pnl | account_pnl |
| 策略A平仓 | 11~12 | STRAT_A 资金+持仓 | account 资金+持仓 |
| 策略B平仓 | 13~14 | STRAT_B 资金+持仓 | account 资金+持仓 |
| 终验 | 15 | 两策略权益守恒 | 账户权益守恒 |

---

## 8. 附录：步骤12账户级资金计算详解

策略A平仓 2 手 @505 后：
- 释放保证金：1200000（STRAT_A 的 margin）
- 实现盈亏：100000
- 平仓手续费：1010
- account_cash 变化 = +1200000 + 100000 - 1010 = +1298990
- account_margin 变化 = -1200000
- 原 account_cash = 9997593196（步骤10 后，步骤8/9 基数正确值），account_margin = 2404800
- 新 account_cash = 9997593196 + 1298990 = 9998892186
- 新 account_margin = 2404800 - 1200000 = 1204800
- account_pnl 仅剩 STRAT_B 的 2 手持仓：(5040000-5020000)×2 = 40000（若步骤10执行了行情）

**实现要点**：平仓成交时 `delta_avail_cash += 实现盈亏 + 释放保证金 - 平仓费`（见 position_processor.cc / account_position_processor.cc）。释放的保证金必须归还可用资金，不能漏算。

若跳过步骤10，步骤12时无行情则 account_pnl 按 STRAT_B 持仓的 hold_cost 与 last_price 计算；若从未调用 newprice，last_price 可能为 0 或开仓价，具体以实现为准。建议执行步骤10以确保 pnl 可验证。

---

*文档版本：1.0*
*创建日期：2026-03-14*
