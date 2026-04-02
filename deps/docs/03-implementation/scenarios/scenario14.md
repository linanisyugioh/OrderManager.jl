# 场景14设计文档：HFT 适配接口使用与期望值验证

## 1. 场景概述

### 1.1 测试目标

验证 HFT 适配接口（`om_hft_api.h`）与 OM 原生接口行为一致，并通过查询接口校验期望值：

1. **费率接口**：`om_add_fee_info_hft`（HftCodeInfo → FeeCodeInfo 后写入缓存）
2. **委托接口**：`om_handle_order_hft`（HftOrder + HftCodeInfo → OmOrder + FeeCodeInfo，驱动 Order → Position → Fundtable）
3. **成交接口**：`om_handle_trade_hft`（HftTrade → OmTrade，写入成交表）

### 1.2 业务背景

- 交易所：SHFE（上期所）
- 合约：黄金期货 au2506（symbol 格式 `SHFE.au2506`）
- 测试重点：全程使用 HFT 结构体（HftOrder、HftTrade、HftCodeInfo）调用适配接口，用 `om_query.h` 查询委托、持仓、资金并校验与公式一致

### 1.3 测试范围

| 验证点 | 说明 |
|--------|------|
| om_add_fee_info_hft | 费率缓存成功，后续 om_handle_order_hft 能正确使用 |
| om_handle_order_hft | 开仓/平仓委托状态推进，持仓与资金变化与原生 om_handle_order 一致 |
| om_handle_trade_hft | 成交入库成功，不改变持仓与资金（当前实现仅入库） |
| 期望值校验 | 开仓后 margin、fee、avail_cash；平仓后 margin=0、fee 累加、avail_cash=初始−手续费+盈亏 |

---

## 2. 测试数据设计

### 2.1 合约参数（与 HftCodeInfo 对应）

| 合约 | 交易所 | 乘数 | 保证金率 | 开仓费率 | 平仓费率 | 说明 |
|------|--------|------|----------|----------|----------|------|
| SHFE.au2506 | 上期所 | 1 | 12% | 万1 | 万1 | symbol 带交易所前缀，适配层解析为 code |

```c
#define TEST14_CODE            "SHFE.au2506"
#define TEST14_MULTIPLY        1
#define TEST14_MARGIN_RATIO    1200    /* 12% × 10000 */
#define TEST14_OPEN_RATE       10      /* 万1 × 100000 */
#define TEST14_CLOSE_RATE      10
#define TEST14_PRICE_TICK      10
```

### 2.2 价格参数（×10000）

| 用途 | 价格 | 说明 |
|------|------|------|
| 开仓价 | 5000000 | 500.00元/克 |
| 平仓价 | 5050000 | 505.00元/克（上涨5元） |

### 2.3 账户与初始资金

```c
#define TEST14_INITIAL_CASH    10000000000LL   /* 100万元 × 10000 */
#define TEST14_RUN_ID          "RUN_014"
#define TEST14_ACCOUNT_ID      "ACC_014"
#define TEST14_STRATEGY_ID     "STRAT_014"
#define TEST14_ACCOUNT_TYPE    AccountType_Futures
#define TEST14_TRADING_DATE    20260314
#define TEST14_VOLUME          2
#define TEST14_OPEN_ORDER_ID   "HFT_OPEN_014"
#define TEST14_CLOSE_ORDER_ID  "HFT_CLOSE_014"
#define TEST14_MATCH_SEQNO     "EXEC_014_001"
```

### 2.4 计算公式（统一量纲 ×10000）

与 quick-reference 一致：

```
保证金/手 = price × multiply × margin_ratio / 10000
开仓费/手 = price × multiply × open_rate / 100000
平仓费/手 = price × multiply × close_rate / 100000
实现盈亏/手 = (close_price - hold_cost) × multiply × dir_sign（多头 +1）

单手保证金(开) = 5000000 × 1 × 1200 / 10000 = 600000
单手开仓费     = 5000000 × 1 × 10 / 100000 = 500
单手平仓费     = 5050000 × 1 × 10 / 100000 = 505
单手实现盈亏   = (5050000 - 5000000) × 1 = 50000
```

---

## 3. 步骤与期望值

### 步骤 1：系统初始化

| 操作 | `om_init("./test_data_scenario14")` |
|------|-------------------------------------|
| 系统状态 | 数据库与存储层就绪 |
| 校验 | 返回 0 |

### 步骤 2：设置资金账户

| 操作 | `om_set_fund_config` + `om_set_account_fund_config`（使用 OM 原生结构体） |
|------|----------------------------------------------------------------------------|
| 策略级/账户级资金 | 初始 avail_cash = account_cash = 10000000000，margin=0, fee=0 |
| 校验 | 返回 0 |

### 步骤 3：交易日更新

| 操作 | `om_trading_day_update(20260314)`（不在此步调用 om_add_fee_info，费率由步骤4 HFT 接口添加） |
|------|-----------------------------------------------------------------------------------------------|
| 校验 | 返回 0 |

### 步骤 4：HFT 添加费率

| 操作 | `om_add_fee_info_hft(&hft_code_info)` |
|------|-------------------------------------|
| 入参 | HftCodeInfo：symbol="SHFE.au2506", multiplier=1, margin_ratio_param1/2=1200, open_commission_ratio=10, close_today/pre_commission_ratio=10 |
| 系统状态 | 费率缓存中 code=SHFE.au2506 可用 |
| 校验 | 返回 0 |

### 步骤 5：设置查询作用域

| 操作 | `om_set_query_scope(TEST14_RUN_ID, TEST14_ACCOUNT_ID, TEST14_ACCOUNT_TYPE)` |
|------|-----------------------------------------------------------------------------|
| 校验 | 返回 0，后续查询使用该作用域 |

### 步骤 6：HFT 开仓委托 (PendingNew → Filled)

| 操作 | `om_handle_order_hft(&hft_order, &hft_code_info)` 两次：先 PendingNew，再 Filled |
|------|----------------------------------------------------------------------------------|
| 入参 | HftOrder：order_id=HFT_OPEN_014, symbol=SHFE.au2506, side=Long_Open(3), volume=2, price=5000000, order_status 从 PendingNew(1) 到 Filled(4), filled_volume 从 0 到 2 |
| 单手保证金 | 600000 |
| 单手开仓费 | 500 |
| 总保证金 | 1200000 |
| 总开仓费 | 1000 |
| 策略级资金 | avail_cash=9998799000, margin=1200000, fee=1000 |
| 持仓 | ContractStat.today_long_volume=2 |
| 校验 | 返回 0 |

### 步骤 7：验证开仓后委托、持仓、资金期望值

| 操作 | `om_query_order` / `om_query_contract_stat` / `om_query_fund` |
|------|----------------------------------------------------------------|
| 委托 | order_id=HFT_OPEN_014, status=Filled, filled_volume=2, code=SHFE.au2506 |
| 持仓 | today_long_volume=2 |
| 资金 | avail_cash=10000000000−1200000−1000=9998799000, margin=1200000, fee=1000 |
| 校验 | 上述字段与公式计算一致 |

### 步骤 8：HFT 成交入库

| 操作 | `om_handle_trade_hft(&hft_trade)` |
|------|----------------------------------|
| 入参 | HftTrade：order_id=HFT_OPEN_014, exec_id=EXEC_014_001, symbol=SHFE.au2506, volume=2, price=5000000, turnover 等 |
| 系统状态 | 成交表写入一条记录；当前实现不驱动持仓与资金 |
| 校验 | 返回 0 |

### 步骤 9：HFT 平仓委托 (PendingNew → Filled)

| 操作 | `om_handle_order_hft(&hft_order, &hft_code_info)` 两次：先 PendingNew，再 Filled |
|------|----------------------------------------------------------------------------------|
| 入参 | HftOrder：order_id=HFT_CLOSE_014, symbol=SHFE.au2506, side=Today_Long_Close(8), volume=2, price=5050000, order_status 从 PendingNew 到 Filled, filled_volume=2 |
| 总平仓费 | 505×2=1010 |
| 总手续费 | 1000+1010=2010 |
| 总实现盈亏 | 50000×2=100000 |
| 策略级资金 | margin=0, fee=2010, avail_cash=10000000000−2010+100000=10000097990 |
| 持仓 | ContractStat.today_long_volume=0 |
| 校验 | 返回 0 |

### 步骤 10：验证平仓后期望值

| 操作 | `om_query_contract_stat` / `om_query_fund` / `om_query_position_codes` |
|------|-------------------------------------------------------------------------|
| 持仓 | today_long_volume=0；om_query_position_codes(全部) 返回空字符串 |
| 资金 | margin=0, fee=2010, avail_cash=10000097990 |
| 校验 | 与公式一致：avail_cash = 初始 − 总手续费 + 实现盈亏 |

### 步骤 11：清理资源

| 操作 | `om_release()` |
|------|---------------|
| 校验 | 正常退出 |

---

## 4. 校验点汇总

### 4.1 HFT 适配接口验证

| 接口 | 验证点 |
|------|--------|
| `om_add_fee_info_hft` | 传入 HftCodeInfo 后费率缓存可用，om_handle_order_hft 能正确取到 multiply、保证金率、手续费率 |
| `om_handle_order_hft` | 开仓/平仓两阶段（PendingNew→Filled）与原生 om_handle_order 效果一致；symbol 解析为 code，side/order_status 等枚举一致 |
| `om_handle_trade_hft` | 成交入库成功；exec_id→match_seqno，transact_time 微秒转毫秒等转换正确 |

### 4.2 期望值公式验证

| 阶段 | 公式/期望 |
|------|-----------|
| 开仓后 | margin = 1200000；fee = 1000；avail_cash = 10000000000 − 1200000 − 1000 = 9998799000 |
| 平仓后 | margin = 0；fee = 2010；avail_cash = 10000000000 − 2010 + 100000 = 10000097990 |
| 持仓 | 开仓后 today_long_volume=2；平仓后 today_long_volume=0，持仓 code 列表为空 |

### 4.3 相关文档

- HFT 接口定义：`include/om_hft_api.h`、`include/hft_structs.h`
- 适配实现：`api/om_hft_adapter.cc`
- 计算公式：`docs/00-overview/quick-reference.md`、`02-domain/calc-formulas.md`

---

## 5. 测试实现说明

### 5.1 测试文件

- **实现文件**：`test/test_scenario14.h`
- **入口**：`run_scenario14_tests()`
- **特点**：仅使用对外 API（om_init、om_set_fund_config、om_set_account_fund_config、om_trading_day_update、om_add_fee_info_hft、om_set_query_scope、om_handle_order_hft、om_handle_trade_hft、om_query_*、om_release），不依赖 data/core 内部头文件

### 5.2 工作目录

- `./test_data_scenario14`

### 5.3 辅助函数

| 函数 | 用途 |
|------|------|
| `createHftCodeInfo14` | 填充 HftCodeInfo（symbol、multiplier、margin_ratio_param1/2、open/close 费率） |
| `createHftOrder14` | 填充 HftOrder（order_id、symbol、side、order_status、volume、filled_volume、price 等） |
| `createHftTrade14` | 填充 HftTrade（order_id、exec_id、symbol、volume、price、turnover、transact_time 等） |
| `calcMarginPerLot14` | 计算单手保证金 |
| `calcOpenFeePerLot14` | 计算单手开仓手续费（按金额费率） |

---

*文档版本：1.0*
*创建日期：2026-03-16*
