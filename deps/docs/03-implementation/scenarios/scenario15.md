# 场景15设计文档：行情更新接口性能测试（om_handle_newprice）

## 1. 场景概述

### 1.1 测试目标
验证 **om_handle_newprice** 接口在「多合约、多手数」持仓下的性能：
- 持仓规模：100 个合约，每个合约 100 手（共 10000 条 PositionUnit）
- 行情更新：每个合约更新 10 次行情，共 1000 次 `om_handle_newprice` 调用
- 统计总耗时、平均单次耗时、每秒调用数（QPS）

### 1.2 业务背景
- 行情刷新为高频路径，需评估在典型持仓规模下的耗时（参见 `03-implementation/flows/newprice-flow.md`）
- 本场景为**性能测试**，不校验资金/持仓具体数值，仅校验每次 `om_handle_newprice` 返回 `OM_Ok` 及最终耗时输出

### 1.3 测试范围
| 验证点 | 说明 |
|--------|------|
| 接口返回值 | 每次 `om_handle_newprice` 返回 OM_Ok |
| 耗时统计 | 总耗时（ms/s）、平均每次调用（ms）、QPS |

---

## 2. 测试数据设计

### 2.1 规模与合约代码

| 项 | 值 | 说明 |
|----|-----|------|
| 合约数量 | 100 | 100 个不同 code |
| 每合约手数 | 100 | 每合约 100 手持仓 |
| 行情更新轮数 | 10 | 每个合约更新 10 次行情 |
| 总 om_handle_newprice 调用次数 | 1000 | 100 × 10 |

合约代码命名：`NEWPRICE.C001` ～ `NEWPRICE.C100`（长度 < LEN_CODE=32，保证不重复）

### 2.2 统一合约参数（×10000 等量纲）

| 参数 | 值 | 说明 |
|------|-----|------|
| 交易所 | DCE | 大商所，不区分今昨 |
| 乘数 multiply | 1 | 便于计算 |
| 保证金率 margin_ratio | 1200 | 12% × 10000 |
| 开/平仓费率 | 10 | 万1 × 100000 |
| 开仓价/成本价 | 5000000 | 500.00 元 |
| 行情价 last_price | 5050000 | 505.00 元（可与开仓价不同以产生浮盈刷新） |

### 2.3 账户与初始资金

| 参数 | 值 | 说明 |
|------|-----|------|
| run_id | RUN_NEWPRICE_BENCH |  |
| account_id | ACC_NEWPRICE_BENCH |  |
| account_type | 2 | 期货账户 |
| strategy_id | STRAT_NEWPRICE_BENCH |  |
| trading_date | 20260316 |  |
| 初始资金 | 足够覆盖 100×100 手保证金 | 如 10000000000LL（100万×10000） |

### 2.4 量纲约定（与 quick-reference 一致）
- 价格、金额：扩大 10000 倍
- 保证金率：扩大 10000 倍（12% → 1200）
- 手续费率：扩大 100000 倍（万1 → 10）

---

## 3. 前置条件（持仓构建）

按以下顺序执行，**不计入性能计时**：

1. **om_init(work_dir)**  
   工作目录例如 `test_newprice_bench_work`，需先清理并创建。

2. **资金配置**  
   - `om_set_fund_config`：策略级资金（run_id, account_id, account_type, strategy_id, 初始资金）  
   - `om_set_account_fund_config`：账户级资金（同 run_id, account_id, account_type）

3. **om_trading_day_update(trading_date)**  
   进入交易日。

4. **对 100 个合约依次**  
   - 构造该合约的 `FeeCodeInfo`（code、multiply、margin_ratio、open/close rate 等）  
   - `om_add_fee_info(&fee)`  
   - 构造开仓委托（同一 code，volume=100，side=Long_Open）  
   - `om_handle_order(order, fee)`：先 status=PendingNew、filled_volume=0；再 status=Filled、filled_volume=100、filled_turnover=100×price×multiply  
   形成 100 个合约 × 100 手 = 10000 条 PositionUnit。

---

## 4. 性能测试步骤

1. 在完成上述前置条件后，**开始计时**（如 `std::chrono::steady_clock::now()`）。
2. 双重循环：  
   - 外层：10 轮  
   - 内层：对 100 个合约依次调用 `om_handle_newprice(code, last_price)`  
   - `last_price` 可每轮或每合约略作变化以模拟行情，或固定为同一值（如 5050000）；文档约定固定即可。
3. 每次调用后校验返回值为 `OM_Ok`；若失败则打印并退出非 0。
4. 循环结束后**结束计时**。
5. 计算并输出统计指标（见 §5）。

**注意**：计时区间仅包含 1000 次 `om_handle_newprice`，不包含建仓阶段。

---

## 5. 统计指标与输出

| 指标 | 说明 |
|------|------|
| 总耗时 | ms 与 s（保留 3 位小数） |
| 平均每次调用 | ms（总耗时 / 1000） |
| 每秒调用数（QPS） | 1000 / 总耗时(s) |

输出格式参考 `test/test_handle_order_bench.cc`，例如：

```
========== om_handle_newprice 效率测试结果 ==========
合约数:           100
每合约手数:       100
行情更新轮数:     10
总调用次数:       1000
总耗时:           xxx ms (x.xxx s)
平均每次:         x.xx ms
每秒调用数:       xxx.x
=================================================
```

---

## 6. 相关文档

| 主题 | 位置 |
|------|------|
| 行情刷新流程 | `03-implementation/flows/newprice-flow.md` |
| 对外 API | `03-implementation/interfaces/public-apis.md` |
| 快速参考 | `00-overview/quick-reference.md` |

---

## 7. 运行方式

本场景由独立可执行程序 **test_newprice_bench** 实现，不纳入 om-test 主流程。  
编译后单独运行：`build\bin\Debug\test_newprice_bench.exe`（或 Release 目录）。
