# 场景5设计文档：交易日初始化（tradingDayUpdate）流程测试

## 1. 场景概述

### 1.1 测试目标
验证 Order Manager **tradingDayUpdate**（交易日初始化）流程的正确性：
1. order 表已清空
2. 已平仓持仓已删除
3. ContractStat 从未平仓持仓正确重建（今/昨区分）

### 1.2 业务背景
- 实盘场景：每日开盘前调用 tradingDayUpdate 进入新交易日
- 跨日时：open_date < trading_date 的持仓变为昨仓

### 1.3 测试范围
| 验证点 | 说明 |
|--------|------|
| order 表 | tradingDayUpdate 后清空 |
| 持仓表 | 已平仓 position_unit 删除，未平仓保留 |
| ContractStat | 从 queryAllUnclosed 正确重建，yesterday_long/today_long |

---

## 2. 测试数据设计

### 2.1 合约参数

| 合约 | 交易所 | 乘数 | 保证金率 | 费率 | 说明 |
|------|--------|------|----------|------|------|
| SHFE.au2506 | 上期所 | 1000克/手 | 12% | 万1 | 黄金期货 |

```c
#define S5_TEST_CODE        "SHFE.au2506"
#define S5_TEST_MULTIPLY    1000
#define S5_TEST_MARGIN_RATIO 1200
#define S5_TEST_RATE_OPEN   10
#define S5_TEST_RATE_CLOSE  10
```

### 2.2 价格参数（×10000）

| 用途 | 价格 |
|------|------|
| 开仓价 | 5000000 |
| 平仓价 | 5050000 |

### 2.3 账户与日期
```c
#define S5_TEST_INITIAL_CASH  10000000000LL
#define S5_TEST_RUN_ID         "RUN_005"
#define S5_TEST_ACCOUNT_ID     "ACC_005"
#define S5_TEST_STRATEGY_ID    "STRAT_005"
#define S5_DAY1                20260312     /* 第一天：开仓、平仓 */
#define S5_DAY2                20260313     /* 第二天：tradingDayUpdate */
#define S5_TEST_OPEN_VOLUME    3            /* 开仓3手 */
#define S5_TEST_CLOSE_VOLUME   1            /* 平仓1手 */
#define S5_TEST_UNCLOSED_COUNT 2            /* 剩余未平仓2手 */
```

---

## 3. 步骤与期望值

### 步骤 1：系统初始化
| 操作 | `om_init("test_scenario5_work")` |
|------|----------------------------------|
| 校验 | init 返回 0 |

### 步骤 2：设置资金账户
| 操作 | `om_set_fund_config` |
|------|----------------------|
| 校验 | 成功 |

### 步骤 3：Day1 交易日初始化
| 操作 | `om_trading_day_update(20260312)` + `om_add_fee_info` 按需传入 |
|------|---------------------------------------------|
| 校验 | 成功 |

### 步骤 4：创建持仓（开仓3手，平仓1手）
| 子步 | 操作 | 期望 |
|------|------|------|
| 4a | 开仓委托 PendingNew 3手 | 冻结资金 |
| 4b | 开仓成交 Filled 3手 | 创建3条 PositionUnit |
| 4c | 平仓委托 PendingNew 1手（Today_Long_Close） | 冻结1手持仓 |
| 4d | 平仓成交 Filled 1手 | 1条 PositionUnit 平仓，2条未平仓 |
| 校验 | queryAllUnclosedByScope | 返回 2 条未平仓 |
| 校验 | order 表 | 开仓、平仓订单各1条 |

### 步骤 5：调用 tradingDayUpdate（进入 Day2）
| 操作 | `om_trading_day_update(20260313)` + `om_add_fee_info` 按需传入 |
|------|---------------------------------------------|
| 效果 | order 表清空，已平仓删除，ContractStat 重建 |
| 校验 | 成功 |

### 步骤 6：验证 order 表已清空
| 操作 | `OrderStore::queryByOrderId` 查询开仓订单、平仓订单 |
|------|---------------------------------------------------|
| 期望 | 均返回 1（未找到） |
| 校验 | 开仓订单 OPEN_005 查不到，平仓订单 CLOSE_005 查不到 |

### 步骤 7：验证已平仓持仓已删除
| 操作 | `PositionUnitStore::queryAllUnclosedByScope` |
|------|--------------------------------------------|
| 期望 | 返回 2 条（仅未平仓） |
| 校验 | 未平仓数量=2，已平仓1手对应的 PositionUnit 已删除 |

### 步骤 8：验证 ContractStat 正确重建
| 操作 | `ContractStatStore::queryByScope` |
|------|----------------------------------|
| 背景 | Day1(20260312) 开仓，tradingDayUpdate 进入 Day2(20260313)，open_date(20260312) < trading_date(20260313) → 昨仓 |
| 期望 | today_long_volume=0, yesterday_long_volume=2 |
| 期望 | today_long_frozen=0, yesterday_long_frozen=0 |
| 校验 | yesterday_long_volume=2, 所有 frozen=0 |

---

## 4. 校验点汇总

### 4.1 tradingDayUpdate 效果
| 项目 | 期望 |
|------|------|
| order 表 | 清空（queryByOrderId 返回未找到） |
| 已平仓 PositionUnit | 删除 |
| 未平仓 PositionUnit | 保留 |
| ContractStat | 从未平仓正确重建 |

### 4.2 ContractStat 今/昨规则
| 条件 | 结果 |
|------|------|
| open_date == trading_date | 今仓 |
| open_date < trading_date | 昨仓 |
| 本场景 | 2手在 Day1 开仓，Day2 进入 → 2手昨仓 |

---

## 5. 测试实现说明

### 5.1 测试文件
- **实现文件**：`test/test_scenario5.h`
- **入口**：`run_scenario5_tests()`

### 5.2 工作目录
- `test_scenario5_work`

---

*文档版本：1.0*
*创建日期：2026-03-14*
