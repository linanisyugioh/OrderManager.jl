# 场景9设计文档：OmTrade成交数据正常处理测试

## 1. 场景概述

### 1.1 测试目标
验证 Order Manager 对 **OmTrade（成交回报）数据** 的正常处理能力：

1. **OmTrade字段校验**：主键字段（7字段联合主键）和业务字段的合法性校验
2. **OmTrade入库**：通过 `om_handle_trade` 接口将成交数据正确写入 trade 表
3. **重复Trade处理**：相同主键的成交数据再次入库时应返回重复错误
4. **多Trade记录**：同一委托的多条成交分别入库，各自独立存储
5. **查询验证**：按主键、按作用域+委托ID、按作用域+合约查询成交记录

### 1.2 业务背景
- **OmTrade来源**：从交易所/柜台接收成交回报，与OmOrder处理分离
- **当前版本定位**：OmTrade仅作为成交记录存储，**不参与持仓和资金计算**（区别于OmOrder驱动的方式）
- **使用场景**：
  - 需要精确记录每笔成交明细的系统
  - 委托与成交分离的架构（成交回报直接入库）
- **与OmOrder的区别**：OmTrade无状态流转，成交即确认，不支持撤单

### 1.3 测试范围

| 验证点 | 说明 |
|--------|------|
| 字段校验 | 主键字段（7字段）和业务字段（4字段）的必填校验 |
| 入库操作 | OmTrade数据正确写入trade表，字段映射正确 |
| 主键冲突 | 重复主键返回 `TradeProc_DuplicateKey` |
| 多成交记录 | 同一委托的多条成交各自独立存储 |
| 查询接口 | TradeStore的查询功能正确性 |

---

## 2. 测试数据设计

### 2.1 合约参数

| 合约 | 交易所 | 乘数 | 保证金率 | 开仓费率 | 平仓费率 | 说明 |
|------|--------|------|----------|----------|----------|------|
| SHFE.au2506 | 上期所 | 1000 | 12% | 万1 | 万1 | 黄金期货（便于验证字段存储） |
| DCE.b2509 | 大商所 | 10 | 12% | 万1 | 万1 | 豆一期货（多合约场景） |

```c
#define S9_AU_CODE         "SHFE.au2506"
#define S9_AU_MULTIPLY     1000
#define S9_AU_MARGIN       1200    /* 12% × 10000 */
#define S9_AU_FEE_OPEN     10      /* 万1 × 100000 */
#define S9_AU_FEE_CLOSE    10

#define S9_B_CODE          "DCE.b2509"
#define S9_B_MULTIPLY      10
#define S9_B_MARGIN        1200
#define S9_B_FEE_OPEN      10
#define S9_B_FEE_CLOSE     10
```

### 2.2 价格参数（×10000）

| 用途 | 价格 | 说明 |
|------|------|------|
| 策略A开仓价 | 5000000 | 500.00元/克 |
| 策略B开仓价 | 3500000 | 3500.00元/吨 |
| 平仓价 | 5050000 | 505.00元/克 |

### 2.3 账户与策略配置

```c
#define S9_RUN_ID         "RUN_009"
#define S9_ACCOUNT_ID     "ACC_009"
#define S9_ACCOUNT_TYPE   1           /* AccountType_Futures */
#define S9_STRAT_A        "STRAT_A"
#define S9_STRAT_B        "STRAT_B"
#define S9_TRADING_DATE   20260314
```

### 2.4 Trade主键设计

| 字段 | 策略A第一条成交 | 策略A第二条成交 | 策略B第一条成交 | 说明 |
|------|-----------------|-----------------|-----------------|------|
| order_id | "ORD_001" | "ORD_001" | "ORD_002" | 策略A两条成交对应同一委托 |
| trade_date | 20260314 | 20260314 | 20260314 | 交易归属日 |
| strategy_id | "STRAT_A" | "STRAT_A" | "STRAT_B" | 策略区分 |
| run_id | "RUN_009" | "RUN_009" | "RUN_009" | 实例ID |
| account_id | "ACC_009" | "ACC_009" | "ACC_009" | 账户ID |
| account_type | 1 | 1 | 1 | 账户类型 |
| match_seqno | "MATCH_001" | "MATCH_002" | "MATCH_003" | 成交序号（唯一） |

---

## 3. 步骤与期望值

### 步骤 1：系统初始化
| 操作 | `om_init("./test_data_scenario9")` |
|------|-------------------------------------|
| **校验** | init 返回 0，系统初始化成功 |

---

### 步骤 2：设置资金账户
| 操作 | `om_set_fund_config` + `om_set_account_fund_config` |
|------|----------------------------------------------------|
| 策略级/账户级 | avail_cash=5000000000, margin=0, frozen=0, fee=0, pnl=0 |
| **校验** | Fundtable、AccountFundtable 初始配置正确 |

---

### 步骤 3：交易日初始化
| 操作 | `om_trading_day_update(20260314)` + `om_add_fee_info` |
|------|---------------------------------------------|
| **校验** | 成功，trading_date=20260314 |

---

### 步骤 4：Trade字段校验测试 - 空order_id
| 操作 | `om_handle_trade` 传入 order_id=""（空字符串） |
|------|-------------------------------------------------|
| 预期结果 | 返回 `OM_InvalidArg` (-1)（API 早期校验） |
| **校验** | 错误码为 -1，trade表无记录 |

---

### 步骤 5：Trade字段校验测试 - 空match_seqno
| 操作 | `om_handle_trade` 传入 match_seqno=""（空字符串） |
|------|--------------------------------------------------|
| 预期结果 | 返回 `TradeProc_InvalidArg` (-601) |
| **校验** | 错误码为 -601，trade表无记录 |

---

### 步骤 6：Trade字段校验测试 - 空code
| 操作 | `om_handle_trade` 传入 code=""（空字符串） |
|------|-------------------------------------------|
| 预期结果 | 返回 `OM_InvalidArg` (-1)（API 早期校验） |
| **校验** | 错误码为 -1，trade表无记录 |

---

### 步骤 7：Trade字段校验测试 - 非法side
| 操作 | `om_handle_trade` 传入 side=99（无效值） |
|------|-----------------------------------------|
| 预期结果 | 返回 `TradeProc_InvalidArg` (-601) |
| **校验** | 错误码为 -601，trade表无记录 |

---

### 步骤 8：Trade字段校验测试 - volume=0
| 操作 | `om_handle_trade` 传入 volume=0 |
|------|--------------------------------|
| 预期结果 | 返回 `TradeProc_InvalidArg` (-601) |
| **校验** | 错误码为 -601，trade表无记录 |

---

### 步骤 9：Trade字段校验测试 - price=0
| 操作 | `om_handle_trade` 传入 price=0 |
|------|-------------------------------|
| 预期结果 | 返回 `TradeProc_InvalidArg` (-601) |
| **校验** | 错误码为 -601，trade表无记录 |

---

### 步骤 10：策略A第一条成交入库（2手 @500）
| 操作 | `om_handle_trade` 填入完整Trade数据 |
|------|-------------------------------------|
| Trade数据 | order_id="ORD_001", match_seqno="MATCH_001", strategy_id="STRAT_A", code="SHFE.au2506", side=Long_Open(3), volume=2, price=5000000 |
| 预期结果 | 返回 `OM_Ok` (0) |
| **校验** | 查询trade表，存在一条记录，各字段值与输入一致 |

**Trade字段详细值**：
```c
OmTrade trade1 = {
    .order_id = "ORD_001",
    .trade_date = 20260314,
    .strategy_id = "STRAT_A",
    .run_id = "RUN_009",
    .account_id = "ACC_009",
    .account_type = 1,
    .match_seqno = "MATCH_001",
    .match_type = 1,              /* TradeReportType_Normal */
    .code = "SHFE.au2506",
    .product = "au",
    .market = 1,                  /* SHFE */
    .cl_order_id = "CL_001",
    .side = 3,                    /* Long_Open */
    .volume = 2,
    .price = 5000000,             /* 500.00 × 10000 */
    .filled_turnover = 100000000, /* 5000000 × 2 × 1000 / 10000 = 1000000 (×10000) */
    .fee = 1000,                  /* 5000000 × 2 × 1000 × 10 / 100000 = 1000 (×10000) */
    .order_volume = 5,
    .order_price = 5000000,
    .slippage = 0,
    .date = 20260314,
    .transact_time = 143025500
};
```

---

### 步骤 11：策略A第二条成交入库（3手 @502）
| 操作 | `om_handle_trade` 同一委托的另一条成交 |
|------|----------------------------------------|
| Trade数据 | order_id="ORD_001", match_seqno="MATCH_002", volume=3, price=5020000 |
| 预期结果 | 返回 `OM_Ok` (0) |
| **校验** | trade表现在有两条记录，ORD_001对应MATCH_001和MATCH_002 |

---

### 步骤 12：策略B成交入库（1手 @3500）
| 操作 | `om_handle_trade` 不同委托的成交 |
|------|----------------------------------|
| Trade数据 | order_id="ORD_002", match_seqno="MATCH_003", strategy_id="STRAT_B", code="DCE.b2509", side=Long_Open(3), volume=1, price=3500000 |
| 预期结果 | 返回 `OM_Ok` (0) |
| **校验** | trade表共3条记录，各字段值正确 |

---

### 步骤 13：重复Trade主键冲突测试
| 操作 | `om_handle_trade` 再次发送MATCH_001（重复主键） |
|------|-------------------------------------------------|
| 预期结果 | 返回 `TradeProc_DuplicateKey` (-604) 或数据库错误 |
| **校验** | trade表仍为3条记录，第一条数据未被覆盖或重复 |

---

### 步骤 14：按主键查询Trade
| 操作 | `TradeStore::get(order_id, trade_date, strategy_id, run_id, account_id, account_type, match_seqno)` |
|------|---------------------------------------------------------------------------------------------------|
| 查询参数 | ORD_001, 20260314, STRAT_A, RUN_009, ACC_009, 1, MATCH_001 |
| 预期结果 | 返回 OmTrade 结构体，字段值与步骤10一致 |
| **校验** | 查询成功，volume=2, price=5000000 |

---

### 步骤 15：按作用域+order_id查询某委托的所有成交
| 操作 | `TradeStore::get_by_scope_order(...)` 查询ORD_001的所有成交 |
|------|-------------------------------------------------------------|
| 查询参数 | run_id="RUN_009", account_id="ACC_009", account_type=1, strategy_id="STRAT_A", order_id="ORD_001" |
| 预期结果 | 返回2条Trade记录（MATCH_001, MATCH_002） |
| **校验** | 记录数为2，总volume=5 |

---

### 步骤 16：按作用域+合约查询某合约的成交历史
| 操作 | `TradeStore::get_by_scope_code(...)` 查询SHFE.au2506的成交 |
|------|------------------------------------------------------------|
| 查询参数 | run_id="RUN_009", account_id="ACC_009", account_type=1, strategy_id="STRAT_A", code="SHFE.au2506" |
| 预期结果 | 返回2条Trade记录（MATCH_001, MATCH_002） |
| **校验** | 记录数为2，均为au2506合约 |

---

### 步骤 17：按作用域查询某策略的所有成交
| 操作 | `TradeStore::get_by_scope(...)` 查询STRAT_B的所有成交 |
|------|--------------------------------------------------------|
| 查询参数 | run_id="RUN_009", account_id="ACC_009", account_type=1, strategy_id="STRAT_B" |
| 预期结果 | 返回1条Trade记录（MATCH_003） |
| **校验** | 记录数为1，code=DCE.b2509 |

---

### 步骤 18：平仓Trade入库测试
| 操作 | `om_handle_trade` 平仓成交 |
|------|----------------------------|
| Trade数据 | order_id="ORD_003", match_seqno="MATCH_004", strategy_id="STRAT_A", code="SHFE.au2506", side=Today_Long_Close(8), volume=2, price=5050000 |
| 预期结果 | 返回 `OM_Ok` (0) |
| **校验** | trade表新增1条记录，side=8 |

---

## 4. 校验点汇总

### 4.1 字段校验清单

| 校验项 | 非法值 | 期望错误码 | 说明 |
|--------|--------|-----------|------|
| order_id为空 | "" | OM_InvalidArg (-1) | API 早期校验 |
| code为空 | "" | OM_InvalidArg (-1) | API 早期校验 |
| match_seqno为空 | "" | TradeProc_InvalidArg (-601) | TradeProcessor 校验 |
| strategy_id为空 | "" | TradeProc_InvalidArg (-601) | TradeProcessor 校验 |
| run_id为空 | "" | TradeProc_InvalidArg (-601) | TradeProcessor 校验 |
| account_id为空 | "" | TradeProc_InvalidArg (-601) | TradeProcessor 校验 |
| side非法 | 99 | TradeProc_InvalidArg (-601) |
| volume=0 | 0 | TradeProc_InvalidArg (-601) |
| price=0 | 0 | TradeProc_InvalidArg (-601) |

### 4.2 入库校验清单

| 校验项 | 说明 |
|--------|------|
| 主键7字段 | order_id, trade_date, strategy_id, run_id, account_id, account_type, match_seqno 组合唯一 |
| 业务字段 | code, side, volume, price 正确存储 |
| 辅助字段 | filled_turnover, fee, order_volume, order_price, slippage, date, transact_time 正确存储 |

### 4.3 查询校验清单

| 校验项 | 查询条件 | 期望结果 |
|--------|----------|----------|
| 按主键查询 | 7字段主键 | 单条记录 |
| 按委托查询 | scope + order_id | 某委托的所有成交 |
| 按合约查询 | scope + code | 某合约的所有成交 |
| 按策略查询 | scope | 某策略的所有成交 |

---

## 5. 错误码汇总

| 错误码 | 值 | 触发场景 |
|--------|-----|----------|
| OM_Ok | 0 | 成功 |
| OM_NotInited | -8 | 系统未初始化 |
| OM_InvalidArg | -1 | order_id 或 code 为空（API 早期校验） |
| TradeProc_InvalidArg | -601 | 字段缺失或无效 |
| TradeProc_StoreError | -603 | 数据库操作失败 |
| TradeProc_DuplicateKey | -604 | 主键冲突（重复成交） |

---

## 6. 边界情况与注意事项

### 6.1 主键冲突边界
- **场景**：交易所重复发送同一成交回报
- **处理**：返回 `TradeProc_DuplicateKey`，不覆盖已有记录
- **验证**：步骤13验证此行为

### 6.2 部分成交场景
- **场景**：一个委托分多笔成交（如5手委托，先成交2手，再成交3手）
- **验证**：步骤10和11模拟此场景，验证多笔成交独立存储

### 6.3 7字段主键边界
- **说明**：trade表主键为7字段联合主键，缺一不可
- **风险**：任意主键字段缺失可能导致数据覆盖或查询错误
- **验证**：步骤4-9验证各主键字段的必填性

### 6.4 Trade与Order的关系
- **当前版本**：Trade入库**不校验**关联Order是否存在
- **未来扩展**：如需Order校验，需补充 `TradeProc_NotFound` 场景
- **本场景暂不测试**：Order不存在时Trade入库（当前允许）

---

## 7. 测试实现说明

### 7.1 测试文件
- **实现文件**：`test/test_scenario9.h`
- **入口**：`run_scenario9_tests()`，由 `test/test.cc` 调用

### 7.2 运行方式
```bash
cd build && cmake --build . --config Debug
./bin/Debug/om-test.exe    # Windows: build\bin\Debug\om-test.exe
```

### 7.3 工作目录
- 使用独立目录 `test_data_scenario9`，测试前后自动创建/清理

### 7.4 辅助函数说明

| 函数 | 用途 |
|------|------|
| `s9_createTrade1_StratA()` | 创建策略A第一条成交（2手 @500） |
| `s9_createTrade2_StratA()` | 创建策略A第二条成交（3手 @502，同委托） |
| `s9_createTrade_StratB()` | 创建策略B成交（1手 @3500，不同合约） |
| `s9_createCloseTrade()` | 创建平仓成交（平今多头） |
| `s9_createAuFeeCodeInfo()` | 创建黄金合约费率信息 |
| `s9_createBFeeCodeInfo()` | 创建豆一合约费率信息 |

### 7.5 测试步骤汇总

| 阶段 | 步骤 | 测试内容 |
|------|------|----------|
| 基础设置 | 1~3 | 初始化、资金配置、交易日更新 |
| 字段校验 | 4~9 | 空order_id、空match_seqno、空code、非法side、volume=0、price=0 |
| 正常入库 | 10~12 | 3条Trade记录入库验证 |
| 主键冲突 | 13 | 重复主键插入失败验证 |
| 查询测试 | 14~17 | 按主键、委托、合约、策略查询 |
| 平仓测试 | 18 | 平仓Trade入库 |

---

## 8. 实现检查清单

- [x] 步骤 1~3：初始化、资金配置、交易日更新
- [x] 步骤 4~9：Trade字段校验（空值、非法值）
- [x] 步骤 10~12：正常Trade入库（4条记录，含平仓）
- [x] 步骤 13：重复主键冲突测试
- [x] 步骤 14~17：Trade查询功能验证（按主键、委托、合约、策略）
- [x] 步骤 18：平仓Trade入库
- [x] 数据库验证：trade表字段映射正确

---

## 9. 相关文档

| 主题 | 位置 |
|------|------|
| Trade字段定义 | `02-domain/trade-lifecycle.md` |
| Trade处理流程 | `03-implementation/flows/trade-flow.md` |
| TradeStore接口 | `03-implementation/interfaces/store-apis.md` §2 |
| 对外API定义 | `03-implementation/interfaces/public-apis.md` §3.3 |
| 错误码定义 | `include/om_error.h` |

---

*文档版本：1.0*  
*创建日期：2026-03-15*  
*作者：AI Assistant*
