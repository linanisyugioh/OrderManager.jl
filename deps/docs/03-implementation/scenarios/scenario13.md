# 场景13设计文档：查询接口完整测试

## 1. 场景概述

### 1.1 测试目标

验证 `om_query.h` 中定义的所有查询接口是否正常工作：

1. **作用域设置接口**：`om_set_query_scope`, `om_get_query_run_id`, `om_get_query_account_id`, `om_get_query_account_type`
2. **委托查询接口**：`om_query_order`, `om_query_order_ids`
3. **持仓查询接口**：`om_query_contract_stat`, `om_query_account_contract_stat`, `om_query_position_codes`
4. **资金查询接口**：`om_query_fund`, `om_query_account_fund`

### 1.2 业务背景

- 交易所：SHFE（上期所）
- 合约：黄金期货 au2506
- 测试重点：验证查询接口在以下场景的正确性
  - 未设置作用域时的错误处理
  - 设置作用域后的数据查询
  - 开仓成交后的数据查询
  - 平仓成交后的数据查询

### 1.3 测试范围

| 验证点 | 说明 |
|--------|------|
| 作用域接口 | 设置前后状态验证，获取接口返回值校验 |
| 委托查询 | 单条委托查询、委托ID列表查询（终态/未终态） |
| 持仓查询 | 策略级合约统计、账户级合约统计、持仓 code 列表 |
| 资金查询 | 策略级资金、账户级资金 |
| 数据一致性 | 开平仓前后数据变化验证 |

---

## 2. 测试数据设计

### 2.1 合约参数

| 合约 | 交易所 | 乘数 | 保证金率 | 开仓费率 | 平仓费率 | 说明 |
|------|--------|------|----------|----------|----------|------|
| SHFE.au2506 | 上期所 | 1 | 12% | 万1 | 万1 | 黄金期货（乘数1便于计算） |

```c
#define TEST13_CODE            "SHFE.au2506"
#define TEST13_MULTIPLY        1
#define TEST13_MARGIN_RATIO    1200    /* 12% × 10000 */
#define TEST13_OPEN_RATE       10      /* 万1 × 100000 */
#define TEST13_CLOSE_RATE      10
```

### 2.2 价格参数（×10000）

| 用途 | 价格 | 说明 |
|------|------|------|
| 开仓价 | 5000000 | 500.00元/克 |
| 平仓价 | 5050000 | 505.00元/克（上涨5元） |

### 2.3 账户与初始资金

```c
#define TEST13_INITIAL_CASH    10000000000LL   /* 100万元 × 10000 */
#define TEST13_RUN_ID          "RUN_013"
#define TEST13_ACCOUNT_ID      "ACC_013"
#define TEST13_STRATEGY_ID     "STRAT_013"
#define TEST13_ACCOUNT_TYPE    AccountType_Futures
#define TEST13_TRADING_DATE    20260313
#define TEST13_VOLUME          2
```

### 2.4 计算公式（统一量纲 ×10000）

```
保证金/手 = price × multiply × margin_ratio / 10000
开仓费/手 = price × multiply × open_rate / 100000
平仓费/手 = price × multiply × close_rate / 100000
单手保证金 = 5000000 × 1 × 1200 / 10000 = 600000
单手开仓费 = 5000000 × 1 × 10 / 100000 = 500
单手平仓费 = 5050000 × 1 × 10 / 100000 = 505
```

---

## 3. 步骤与期望值

### 步骤 1：系统初始化

| 操作 | `om_init("./test_data_scenario13")` |
|------|-------------------------------------|
| 系统状态 | 数据库连接建立，存储层就绪 |
| 校验 | init 返回 0 |

### 步骤 2：设置资金账户

| 操作 | `om_set_fund_config` + `om_set_account_fund_config` |
|------|-----------------------------------------------------|
| 策略级资金表 | avail_cash=10000000000, margin=0, fee=0, pnl=0 |
| 账户级资金表 | account_cash=10000000000, account_margin=0, fee=0 |
| 校验 | 设置返回 0 |

### 步骤 3：交易日更新

| 操作 | `om_trading_day_update(20260313)` + `om_add_fee_info` |
|------|-----------------------------------------------|
| 资金表 | 不变 |
| 校验 | 返回 0 |

### 步骤 4：验证未设置作用域时的查询接口

| 操作 | 调用 `om_get_query_run_id`, `om_get_query_account_id`, `om_get_query_account_type` |
|------|-------------------------------------------------------------------------------------|
| 预期行为 | run_id 可能为 NULL，account_id 可能为 NULL，account_type 为 0 |
| 资金查询 | `om_query_fund` 应返回错误（作用域未设置） |
| 校验 | 查询接口返回错误码（非0） |

### 步骤 5：设置查询作用域

| 操作 | `om_set_query_scope(TEST13_RUN_ID, TEST13_ACCOUNT_ID, TEST13_ACCOUNT_TYPE)` |
|------|-----------------------------------------------------------------------------|
| 系统状态 | 系统缓存 run_id="RUN_013", account_id="ACC_013", account_type=Futures |
| 校验 | 返回 0 |

### 步骤 6：验证获取作用域接口

| 操作 | 调用 `om_get_query_run_id`, `om_get_query_account_id`, `om_get_query_account_type` |
|------|-------------------------------------------------------------------------------------|
| 预期返回值 | run_id="RUN_013", account_id="ACC_013", account_type=1(Futures) |
| 校验 | 字符串值匹配，account_type 匹配 |

### 步骤 7：开仓委托提交 (PendingNew → Filled)

| 操作 | 提交开仓委托 PendingNew，状态推进到 Filled |
|------|-------------------------------------------|
| 单手保证金 | 600000 |
| 单手手续费 | 500 |
| 总保证金 | 600000 × 2 = 1200000 |
| 总开仓费 | 500 × 2 = 1000 |
| 策略级资金 | avail_cash=9987990000, margin=1200000, fee=1000 |
| 账户级资金 | account_cash=9987990000, account_margin=1200000, fee=1000 |
| 持仓 | PositionUnit × 2, ContractStat.today_long=2 |
| 校验 | 资金、持仓数据正确 |

### 步骤 8：验证委托查询接口

| 操作 | `om_query_order(TEST13_OPEN_ORDER_ID, ...)` |
|------|-----------------------------------------------|
| 返回数据 | order_id="OPEN_013", status=Filled, filled_volume=2 |
| 校验 | 字段值匹配 |

| 操作 | `om_query_order_ids(TEST13_STRATEGY_ID, 2, ...)`（status=2 所有状态） |
|------|------------------------------------------------------------------------|
| 返回数据 | 字符串包含 "OPEN_013" |
| 校验 | 委托ID列表包含开仓委托 |

| 操作 | `om_query_order_ids(TEST13_STRATEGY_ID, 0, ...)`（status=0 未终态） |
|------|----------------------------------------------------------------------|
| 返回数据 | 长度为 0（无未终态委托） |
| 校验 | 无未终态委托 |

### 步骤 9：验证持仓查询接口

| 操作 | `om_query_contract_stat(TEST13_STRATEGY_ID, TEST13_CODE, ...)` |
|------|---------------------------------------------------------------|
| 返回数据 | code="SHFE.au2506", today_long_volume=2, today_short_volume=0 |
| 校验 | 策略级持仓统计正确 |

| 操作 | `om_query_account_contract_stat(TEST13_CODE, ...)` |
|------|-----------------------------------------------------|
| 返回数据 | code="SHFE.au2506", today_long_volume=2, today_short_volume=0 |
| 校验 | 账户级持仓统计正确，与策略级一致 |

| 操作 | `om_query_position_codes(TEST13_STRATEGY_ID, status, period, side, ...)` |
|------|---------------------------------------------------------------------------|
| status=2, period=2, side=2（全部） | 返回 "SHFE.au2506" |
| status=1, period=1, side=1（可用/今仓/多） | 返回 "SHFE.au2506" |
| status=0, period=2, side=2（冻结/全部） | 返回空字符串（开仓后无挂平仓单） |
| status=2, period=0, side=2（全部/昨仓） | 返回空字符串（当日开仓无昨仓） |
| 校验 | 各过滤组合返回预期结果 |

### 步骤 10：验证资金查询接口

| 操作 | `om_query_fund(TEST13_STRATEGY_ID, ...)` |
|------|------------------------------------------|
| 预期 avail_cash | 10000000000 - 1200000 - 1000 = 9987990000 |
| 预期 margin | 1200000 |
| 预期 fee | 1000 |
| 校验 | 策略级资金数据正确 |

| 操作 | `om_query_account_fund(...)` |
|------|------------------------------|
| 预期 account_cash | 9987990000 |
| 预期 account_margin | 1200000 |
| 预期 fee | 1000 |
| 校验 | 账户级资金数据正确，与策略级一致 |

### 步骤 11：平仓委托提交 (PendingNew → Filled)

| 操作 | 提交平仓委托 PendingNew，状态推进到 Filled |
|------|-------------------------------------------|
| 单手平仓费 | 505 |
| 总平仓费 | 505 × 2 = 1010 |
| 总手续费 | 1000 + 1010 = 2010 |
| 单手实现盈亏 | (5050000 - 5000000) × 1 = 50000 |
| 总实现盈亏 | 50000 × 2 = 100000 |
| 策略级资金 | avail_cash=10000097990, margin=0, fee=2010 |
| 账户级资金 | account_cash=10000097990, account_margin=0, fee=2010 |
| 持仓 | PositionUnit 清空, ContractStat.today_long=0 |
| 校验 | 平仓后持仓清零，资金更新正确 |

### 步骤 12：平仓后再次验证各查询接口

| 操作 | `om_query_order(TEST13_CLOSE_ORDER_ID, ...)` |
|------|-----------------------------------------------|
| 返回数据 | order_id="CLOSE_013", status=Filled, filled_volume=2 |
| 校验 | 平仓委托可查询，数据正确 |

| 操作 | `om_query_order_ids(TEST13_STRATEGY_ID, 2, ...)` |
|------|---------------------------------------------------|
| 返回数据 | 委托ID列表长度 > 0，包含两条委托 |
| 校验 | 可查询到两条委托记录 |

| 操作 | `om_query_position_codes(TEST13_STRATEGY_ID, 2, 2, 2, ...)` |
|------|------------------------------------------------------------|
| 返回数据 | 空字符串（平仓后无持仓） |
| 校验 | 持仓 code 列表为空 |

| 操作 | `om_query_fund(TEST13_STRATEGY_ID, ...)` |
|------|------------------------------------------|
| 预期 margin | 0（平仓后保证金释放） |
| 预期 fee | 2010（开仓费+平仓费） |
| 校验 | 保证金为0，手续费累加正确 |

### 步骤 13：清理资源

| 操作 | `om_release()` |
|------|----------------|
| 系统状态 | 数据库关闭，资源释放 |
| 校验 | 无内存泄漏，正常退出 |

---

## 4. 校验点汇总

### 4.1 作用域接口验证

| 接口 | 验证点 |
|------|--------|
| `om_set_query_scope` | 设置后获取接口能正确返回 |
| `om_get_query_run_id` | 返回值与设置值匹配 |
| `om_get_query_account_id` | 返回值与设置值匹配 |
| `om_get_query_account_type` | 返回值与设置值匹配 |

### 4.2 委托查询验证

| 接口 | 验证点 |
|------|--------|
| `om_query_order` | 按主键查询，字段值正确 |
| `om_query_order_ids` (status=2) | 返回所有委托ID列表 |
| `om_query_order_ids` (status=0) | 返回空字符串（本场景无未终态委托） |

### 4.3 持仓查询验证

| 接口 | 验证点 |
|------|--------|
| `om_query_contract_stat` | 策略级持仓统计正确 |
| `om_query_account_contract_stat` | 账户级持仓统计正确，与策略级一致 |
| `om_query_position_codes` | 开仓后返回 "SHFE.au2506"（全部/可用今仓多）；冻结/昨仓返回空；平仓后返回空 |

### 4.4 资金查询验证

| 接口 | 验证点 |
|------|--------|
| `om_query_fund` | 策略级资金字段正确（avail_cash, margin, fee） |
| `om_query_account_fund` | 账户级资金字段正确，与策略级一致 |

### 4.5 数据一致性验证

| 校验项 | 公式 |
|--------|------|
| 开仓后保证金 | margin = price × multiply × margin_ratio / 10000 × volume |
| 开仓后手续费 | fee = price × multiply × open_rate / 100000 × volume |
| 平仓后保证金 | margin = 0（持仓清零） |
| 平仓后手续费 | fee = 开仓费 + 平仓费 |
| 平仓后可用资金 | avail_cash = 初始 - 总手续费 + 实现盈亏 |

---

## 5. 测试实现说明

### 5.1 测试文件

- **实现文件**：`test/test_scenario13.h`
- **入口**：`run_scenario13_tests()`
- **实现特点**：使用内联函数定义各测试步骤，使用 ASSERT_* 宏进行断言验证

### 5.2 工作目录

- `./test_data_scenario13`

### 5.3 辅助函数

| 函数 | 用途 |
|------|------|
| `createFeeCodeInfo13` | 创建费率信息结构体 |
| `createFundtable13` | 创建策略级资金结构体 |
| `createAccountFundtable13` | 创建账户级资金结构体 |
| `createOpenOrder13` | 创建开仓委托结构体 |
| `createCloseOrder13` | 创建平仓委托结构体 |
| `calcMarginPerLot13` | 计算单手保证金 |
| `calcOpenFeePerLot13` | 计算单手开仓手续费 |

---

*文档版本：1.0*
*创建日期：2026-03-16*
