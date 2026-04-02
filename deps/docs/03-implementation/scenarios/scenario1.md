# 场景1设计文档：开仓→平仓完整流程（上期所 - 平今仓）

## 1. 场景概述

### 1.1 测试目标
验证 Order Manager 在**上期所(SHFE)**合约下的完整开仓→平仓流程：
1. 开仓委托冻结资金
2. 开仓成交创建持仓
3. 行情更新刷新浮动盈亏
4. 平仓委托冻结持仓
5. 平仓成交释放持仓、兑现盈亏
6. 权益守恒

### 1.2 业务背景
- 交易所：SHFE（上期所）
- 平仓方式：OrderSide_Today_Long_Close（指定平今仓）
- 合约乘数设为 1，便于验证计算

### 1.3 测试范围
| 验证点 | 说明 |
|--------|------|
| 资金表 | avail_cash, margin, frozen_cash, fee, pnl 每步校验 |
| 持仓表 | PositionUnit 数量、hold_cost、pnl |
| 合约统计 | ContractStat today_long、today_long_frozen |
| 委托表 | order 状态、filled_volume、fee |
| 权益守恒 | 最终权益 = 初始 - 总手续费 + 实现盈亏 |

---

## 2. 测试数据设计

### 2.1 合约参数

| 合约 | 交易所 | 乘数 | 保证金率 | 开仓费率 | 平仓费率 | 说明 |
|------|--------|------|----------|----------|----------|------|
| SHFE.au2506 | 上期所 | 1 | 12% | 万1 | 万1 | 黄金期货（乘数1便于计算） |

```c
#define TEST_CODE       "SHFE.au2506"
#define TEST_MULTIPLY    1
#define TEST_MARGIN_RATIO 1200    /* 12% × 10000 */
#define TEST_OPEN_RATE   10       /* 万1 × 100000 */
#define TEST_CLOSE_RATE  10
```

### 2.2 价格参数（×10000）

| 用途 | 价格 | 说明 |
|------|------|------|
| 开仓价 | 5000000 | 500.00元/克 |
| 平仓价/行情价 | 5050000 | 505.00元/克（上涨5元） |

### 2.3 账户与初始资金
```c
#define TEST_INITIAL_CASH  10000000000LL   /* 100万元 × 10000 */
#define TEST_RUN_ID         "RUN_001"
#define TEST_ACCOUNT_ID     "ACC_001"
#define TEST_STRATEGY_ID    "STRAT_001"
#define TEST_TRADING_DATE  20260313
#define TEST_VOLUME        2
```

### 2.4 计算公式（统一量纲 ×10000）
```
保证金/手 = price × multiply × margin_ratio / 10000
开仓费/手 = price × multiply × open_rate / 100000
平仓费/手 = price × multiply × close_rate / 100000
浮动盈亏/手 = (current_price - hold_cost) × multiply
实现盈亏/手 = (close_price - hold_cost) × multiply
equity = margin + avail_cash + frozen_cash + pnl
```

---

## 3. 步骤与期望值

### 步骤 1：系统初始化
| 操作 | `om_init("./test_data_scenario1")` |
|------|-------------------------------------|
| 资金表 | 无记录（尚未设置） |
| 持仓表 | 空 |
| 校验 | init 返回 0，持仓数量=0 |

### 步骤 2：设置资金账户
| 操作 | `om_set_fund_config` |
|------|----------------------|
| 资金表 | avail_cash=10000000000, margin=0, frozen_cash=0, fee=0, pnl=0, equity=10000000000 |
| 持仓表 | 空 |
| 校验 | 查询资金表，avail_cash=10000000000，持仓数=0 |

### 步骤 3：交易日更新
| 操作 | `om_trading_day_update(20260313)` + `om_add_fee_info` 按持仓 codes 传入 |
|------|---------------------------------------------|
| 资金表 | 不变 |
| 持仓表 | 空 |
| 校验 | 与步骤2相同 |

### 步骤 4：开仓新委托 PendingNew（2手 au @500）
| 操作 | `om_handle_order` side=Long_Open, status=PendingNew, volume=2, filled=0, price=5000000 |
|------|-------------------------------------------------------------------------|
| 单手冻结 | 保证金 5000000×1×1200/10000=600000 + 手续费 5000000×1×10/100000=500 = 600500 |
| 总冻结 | 600500 × 2 = 1201000 |
| 资金表 | avail_cash=9998799000, frozen_cash=1201000, margin=0, fee=0, pnl=0 |
| 持仓表 | 0 条（PendingNew 不创建持仓） |
| 校验 | frozen_cash=1201000, 持仓数=0 |

### 步骤 5：开仓成交 Filled（2手 au @500）
| 操作 | `om_handle_order` status=Filled, filled_volume=2 |
|------|--------------------------------------------------|
| 单手保证金 | 600000 |
| 单手手续费 | 500 |
| 资金表 | avail_cash=9998799000, frozen_cash=0, margin=1200000, fee=1000, pnl=0 |
| 持仓表 | 2 条，hold_cost=5000000 |
| 校验 | margin=1200000, fee=1000, 持仓数=2, ContractStat today_long=2 |

### 步骤 6：行情更新（盘中价 505）
| 操作 | `om_handle_newprice("SHFE.au2506", 5050000)` |
|------|---------------------------------------------|
| 单手浮盈 | (5050000-5000000)×1 = 50000 |
| 总 pnl | 100000 |
| 资金表 | avail_cash=9998799000, margin=1200000, fee=1000, pnl=100000 |
| 持仓表 | 每手 pnl=50000, hold_cost=5000000 不变 |
| 校验 | pnl=100000 |

### 步骤 7：平仓新委托 PendingNew（2手 au @505）
| 操作 | `om_handle_order` side=Today_Long_Close, status=PendingNew, volume=2 |
|------|---------------------------------------------------------------------|
| 资金表 | 与步骤6相同 |
| 持仓表 | 2 条 |
| ContractStat | today_long=2, today_long_frozen=2 |
| 校验 | 资金不变，持仓冻结=2 |

### 步骤 8：平仓成交 Filled（2手 au @505）
| 操作 | `om_handle_order` status=Filled, filled_volume=2 |
|------|--------------------------------------------------|
| 单手平仓费 | 5050000×1×10/100000=505 |
| 总手续费 | 1000(开仓) + 1010(平仓) = 2010 |
| 单手实现盈亏 | (5050000-5000000)×1 = 50000 |
| 总实现盈亏 | 100000 |
| 资金表 | avail_cash=10000097990, margin=0, frozen=0, fee=2010, pnl=0 |
| 持仓表 | 0 条 |
| 校验 | avail_cash=10000097990, margin=0, 持仓数=0 |

### 步骤 9：权益守恒验证
| 校验项 | 公式 |
|--------|------|
| 最终权益 | avail_cash + margin + frozen_cash + pnl = 10000097990 |
| 守恒式 | 10000097990 = 10000000000 - 2010 + 100000 |
| 总手续费 | 2010 |
| 总实现盈亏 | 100000 |

---

## 4. 校验点汇总

### 4.1 每步必校验
- 资金表：avail_cash, margin, frozen_cash, fee, pnl
- 持仓表：未平仓数量
- ContractStat：today_long、today_long_frozen（平仓挂单时）

### 4.2 最终校验
- 持仓表空
- 权益守恒：最终权益 = 初始 - 总手续费 + 总实现盈亏

---

## 5. 测试实现说明

### 5.1 测试文件
- **实现文件**：`test/test_scenario1.h`
- **入口**：`run_scenario1_tests()`

### 5.2 工作目录
- `./test_data_scenario1`

---

*文档版本：1.0*
*创建日期：2026-03-14*
