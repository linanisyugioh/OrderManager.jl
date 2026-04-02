# 场景2设计文档：开仓→平仓完整流程（大商所 - 普通平仓）

## 1. 场景概述

### 1.1 测试目标
验证 Order Manager 在**大商所(DCE)**合约下的完整开仓→平仓流程：
1. 开仓委托冻结资金
2. 开仓成交创建持仓
3. 行情更新刷新浮动盈亏
4. 平仓委托（Long_Close 自动平昨优先）
5. 平仓成交释放持仓、兑现盈亏
6. 权益守恒

### 1.2 业务背景
- 交易所：DCE（大商所）
- 平仓方式：OrderSide_Long_Close（普通平仓，系统自动平昨优先）
- 豆油期货 y2506

### 1.3 测试范围
| 验证点 | 说明 |
|--------|------|
| 资金表 | avail_cash, margin, frozen_cash, fee, pnl 每步校验 |
| 持仓表 | PositionUnit 数量 |
| 合约统计 | ContractStat today_long、today_long_frozen |
| 委托表 | order 状态、filled_volume、fee |
| 权益守恒 | 最终权益 = 初始 - 总手续费 + 实现盈亏 |

---

## 2. 测试数据设计

### 2.1 合约参数

| 合约 | 交易所 | 乘数 | 保证金率 | 开仓费率 | 平仓费率 | 说明 |
|------|--------|------|----------|----------|----------|------|
| DCE.y2506 | 大商所 | 10吨/手 | 12% | 万1 | 万1 | 豆油期货 |

```c
#define S2_TEST_CODE       "DCE.y2506"
#define S2_TEST_MULTIPLY    10
#define S2_TEST_MARGIN_RATIO 1200
#define S2_TEST_OPEN_RATE   10
#define S2_TEST_CLOSE_RATE  10
```

### 2.2 价格参数（×10000）

| 用途 | 价格 | 说明 |
|------|------|------|
| 开仓价 | 7500000 | 7500.00元/吨 |
| 平仓价/行情价 | 7600000 | 7600.00元/吨（上涨100元） |

### 2.3 账户与初始资金
```c
#define S2_TEST_INITIAL_CASH  10000000000LL
#define S2_TEST_RUN_ID         "RUN_002"
#define S2_TEST_ACCOUNT_ID    "ACC_002"
#define S2_TEST_STRATEGY_ID   "STRAT_002"
#define S2_TEST_TRADING_DATE  20260313
#define S2_TEST_VOLUME        2
```

### 2.4 计算公式（统一量纲 ×10000）
```
保证金/手 = price × multiply × margin_ratio / 10000
开仓费/手 = price × multiply × open_rate / 100000
平仓费/手 = price × multiply × close_rate / 100000
浮动盈亏/手 = (current_price - hold_cost) × multiply
实现盈亏/手 = (close_price - hold_cost) × multiply
```

---

## 3. 步骤与期望值

### 步骤 1：系统初始化
| 操作 | `om_init("./test_data_scenario2")` |
|------|-------------------------------------|
| 校验 | init 返回 0 |

### 步骤 2：设置资金账户
| 操作 | `om_set_fund_config` |
|------|----------------------|
| 资金表 | avail_cash=10000000000, margin=0, frozen_cash=0, fee=0, pnl=0 |
| 校验 | avail_cash=10000000000 |

### 步骤 3：交易日更新
| 操作 | `om_trading_day_update(20260313)` + `om_add_fee_info` 按持仓 codes 传入 |
|------|---------------------------------------------|
| 校验 | 成功 |

### 步骤 4：开仓新委托 PendingNew（2手 y @7500）
| 操作 | `om_handle_order` side=Long_Open, status=PendingNew, volume=2, price=7500000 |
|------|-------------------------------------------------------------------------|
| 单手保证金 | 7500000×10×1200/10000 = 9000000 |
| 单手手续费 | 7500000×10×10/100000 = 7500 |
| 单手冻结 | 9007500 |
| 总冻结 | 18015000 |
| 资金表 | avail_cash=9981985000, frozen_cash=18015000, margin=0, fee=0, pnl=0 |
| 持仓表 | 0 条 |
| 校验 | frozen_cash=18015000, 持仓数=0 |

### 步骤 5：开仓成交 Filled（2手 y @7500）
| 操作 | `om_handle_order` status=Filled, filled_volume=2 |
|------|--------------------------------------------------|
| 单手保证金 | 9000000 |
| 单手手续费 | 7500 |
| 资金表 | avail_cash=9981985000, frozen_cash=0, margin=18000000, fee=15000, pnl=0 |
| 持仓表 | 2 条 |
| 校验 | margin=18000000, fee=15000, 持仓数=2, ContractStat today_long=2 |

### 步骤 6：行情更新（盘中价 7600）
| 操作 | `om_handle_newprice("DCE.y2506", 7600000)` |
|------|-------------------------------------------|
| 单手浮盈 | (7600000-7500000)×10 = 100000 |
| 总 pnl | 200000 |
| 资金表 | avail_cash=9981985000, margin=18000000, fee=15000, pnl=200000 |
| 校验 | pnl=200000 |

### 步骤 7：平仓新委托 PendingNew（Long_Close，2手 y @7600）
| 操作 | `om_handle_order` side=Long_Close, status=PendingNew, volume=2 |
|------|----------------------------------------------------------------|
| 资金表 | 与步骤6相同 |
| 持仓表 | 2 条 |
| ContractStat | today_long=2, today_long_frozen=2 |
| 校验 | 资金不变，today_long_frozen=2 |

### 步骤 8：平仓成交 Filled（2手 y @7600）
| 操作 | `om_handle_order` status=Filled, filled_volume=2 |
|------|--------------------------------------------------|
| 单手平仓费 | 7600000×10×10/100000 = 7600 |
| 总手续费 | 15000(开仓) + 15200(平仓) = 30200 |
| 单手实现盈亏 | (7600000-7500000)×10 = 100000 |
| 总实现盈亏 | 200000 |
| 资金表 | avail_cash=10001698000, margin=0, frozen=0, fee=30200, pnl=0 |
| 持仓表 | 0 条 |
| 校验 | avail_cash=10001698000, margin=0, 持仓数=0 |

### 步骤 9：权益守恒验证
| 校验项 | 公式 |
|--------|------|
| 最终权益 | 10001698000 |
| 守恒式 | 10001698000 = 10000000000 - 30200 + 200000 |
| 总手续费 | 30200 |
| 总实现盈亏 | 200000 |

---

## 4. 校验点汇总

### 4.1 每步必校验
- 资金表：avail_cash, margin, frozen_cash, fee, pnl
- 持仓表：未平仓数量
- ContractStat：today_long、today_long_frozen

### 4.2 最终校验
- 持仓表空
- 权益守恒

---

## 5. 测试实现说明

### 5.1 测试文件
- **实现文件**：`test/test_scenario2.h`
- **入口**：`run_scenario2_tests()`

### 5.2 工作目录
- `./test_data_scenario2`

### 5.3 与场景1差异
- 交易所：DCE vs SHFE
- 平仓方式：Long_Close（自动平昨）vs Today_Long_Close（指定平今）
- 合约乘数：10 vs 1

---

*文档版本：1.0*
*创建日期：2026-03-14*
