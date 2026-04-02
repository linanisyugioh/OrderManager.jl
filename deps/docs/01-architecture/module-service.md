# 模块：service（业务层）

> 从old_docs/modules/mod_service.md迁移精简  
> 职责：基于 core 层的计算能力进行业务级编排，实现对外 API 接口

---

## 1. 模块概述

### 1.1 职责

- 基于 core 层的计算能力进行**业务级编排**
- 实现对外 API 接口（`om_handle_order`, `om_handle_newprice` 等的业务实现）
- 处理跨作用域批量操作、交易日管理、系统生命周期管理
- **事务控制中心**：所有数据库事务在 service 层控制

### 1.2 设计原则

1. **编排而非计算**：service 层只做业务流程编排，不实现计算逻辑，全部计算委托给 core 层
2. **单例管理**：使用单例模式管理 service 组件生命周期（`OmService::instance()`）
3. **状态保持**：service 层保持当前交易日状态（`trading_date_`），供 core 层使用
4. **批量操作**：对于跨作用域操作（如行情刷新），service 层负责遍历或聚合
5. **错误传播**：将 core 层和 data 层的错误码向上传播

---

## 2. 文件清单

| 文件路径 | 类型 | 职责说明 |
|---------|------|---------|
| `service/om_service.h/.cc` | 头/实现 | OmService 主服务类：对外 API 的业务实现，编排 core 层调用；位于 `namespace om` |
| `service/trading_day_init_service.h/.cc` | 头/实现 | TradingDayInitService：交易日初始化服务，负责清理数据、重建合约统计、资金校验 |
| `service/trading_day_end_service.h/.cc` | 头/实现 | TradingDayEndService：交易日结束服务，负责未终态委托处理、资金快照、结算、历史归档 |
| `service/query_kit_service.h/.cc` | 头/实现 | QueryKitService：查询服务，负责查询作用域管理、并发查询接口封装 |
| `service/combo_order_service.h/.cc` | 头/实现 | ComboOrderService：组合委托服务，负责组合委托解析、拆腿、配对持仓 |

---

## 3. OmService 核心接口

```cpp
class OmService {
public:
    static OmService& instance();
    
    // 系统生命周期
    int init(const char* work_dir);
    void release();
    bool isInited() const;
    
    // 交易日管理
    int32_t getTradingDate() const;
    int tradingDayUpdate(int32_t trading_date);
    int addFeeInfo(const FeeCodeInfo& fee_info);
    int tradingDayEnd();
    
    // 业务处理
    int handleOrder(const OmOrder& order, const FeeCodeInfo& fee_info);
    int handleNewPrice(const char* code, int64_t last_price);
    int handleTrade(const OmTrade& trade);
    int setFundConfig(const Fundtable& fund);
    int setAccountFundConfig(const AccountFundtable& fund);
};
```

---

## 4. 业务流程编排

### 4.1 系统初始化（om_init）

```
OmService::init(work_dir)
├── LogManager::instance().init(work_dir/logs, ...)
├── DbManager::instance().open(work_dir/om.db)
├── 创建 Store 实例（unique_ptr 自动管理）
│   ├── OrderStore, PositionUnitStore, ContractStatStore
│   ├── FundtableStore, FundtableHisStore
│   └── AccountFundtableStore, AccountPositionUnitStore, ...
├── Store::createTable() - 各表建表
└── 创建 Processor 实例（传入 Store 裸指针）
    ├── PositionProcessor, AccountPositionProcessor
    ├── FundtableProcessor, AccountFundtableProcessor
    └── OrderProcessor（编排各 Processor）
```

### 4.2 委托处理（om_handle_order）

```
OmService::handleOrder(order, fee_info)
├── 参数校验（纯内存，事务外）
│   ├── order 主键字段非空
│   ├── fee_info.code 与 order.code 匹配
│   └── order.oper_date == trading_date_
├── fee_info_cache_[fee_info.code] = fee_info  // 费率缓存
├── 【事务开始】DbManager::instance().beginTransaction()
├── order_proc_->process(order, fee_info)  // 调用 core 层
│   └── 内部涉及 order / position_unit / contract_stat / fundtable 更新
├── 【事务提交】DbManager::instance().commit()
└── 返回结果
```

**事务控制**：`handleOrder` 必须包裹事务，涉及多表更新。

### 4.3 行情刷新（om_handle_newprice）

```
OmService::handleNewPrice(code, last_price)
├── 参数校验
├── 【策略级】pos_proc_->updateFloatingPnl(code, last_price, deltas)
│   └── 批量更新 PositionUnit.pnl
├── fund_proc_->batchUpdatePnl(fund_deltas)  // 批量更新 Fundtable.pnl
├── 【账户级】acct_pos_proc_->updateFloatingPnl(code, last_price, acct_deltas)
│   └── 批量更新 AccountPositionUnit.pnl
├── for each d in acct_deltas:
│   └── acct_fund_proc_->updatePnl(...)  // 更新 AccountFundtable.pnl
├── price_cache_[code] = last_price  // 缓存最新价
└── 返回结果
```

**事务控制**：`handleNewPrice` 暂不包裹事务（幂等，可重复执行）。

### 4.4 交易日初始化（om_trading_day_update）

```
OmService::tradingDayUpdate(trading_date)
├── 参数校验
├── 【缓存更新】fee_info_cache_.clear()
├── trading_date_ = trading_date
├── 【事务开始】
├── order_store_->deleteAll()           // 清空当日委托
├── pu_store_->deleteClosedUnits()      // 移除已平仓持仓
├── cs_store_->deleteAll()              // 清空合约统计
├── 【重建 ContractStat】
│   ├── queryAllUnclosed() 取全部未平仓
│   └── 按 (scope + code) 聚合 → upsert
├── 【事务提交】
└── 返回结果
```

### 4.5 日终结算（om_trading_day_end）

```
OmService::tradingDayEnd()
├── 【前置检查】缓存完整性验证
│   ├── 检查 price_cache_ 含所有持仓合约的结算价
│   └── 检查 fee_info_cache_ 含所有持仓合约的费率
├── 【创建快照】
│   ├── fundtable_snapshot_handler_->createSnapshot(trading_date_)
│   └── account_fundtable_processor_->createSnapshot(trading_date_)
├── 【结算流程】（按作用域遍历）
│   ├── queryAllUnclosedByScope() 取全部未平仓
│   ├── 逐手重新计算结算盈亏
│   ├── 对比验证 fund.pnl
│   ├── fund.avail_cash += recalculated_pnl
│   ├── 逐手更新 hold_cost, margin, pnl
│   └── fund_store_->update()
└── 返回结果
```

---

## 5. 事务分层约定

| 接口 | 是否包裹事务 | 理由 |
|------|------------|------|
| `handleOrder` | **是** | 涉及 4+ 张表，任意步骤失败须回滚 |
| `tradingDayUpdate` | **是** | 先删后重建，中间失败会破坏数据 |
| `handleNewPrice` | 否 | 幂等，可重复执行 |
| `tradingDayEnd` | 否（可加强） | 按作用域逐条结算，相对独立 |

**重要约定**：SQLite 不支持原生嵌套事务，core/data 层不得自行调用 `beginTransaction/commit/rollback`。

---

## 6. 费率缓存策略

```cpp
// OmService 成员
std::unordered_map<std::string, FeeCodeInfo> fee_info_cache_;
std::unordered_map<std::string, int64_t> price_cache_;
```

| 缓存内容 | 来源 | 用途 |
|---------|------|------|
| fee_info_cache_ | `addFeeInfo` 逐个传入 + `handleOrder` 补充 | 日终结算重算保证金 |
| price_cache_ | `handleNewPrice` 传入 | 日终结算获取结算价 |

**缓存更新规则**：
- `tradingDayUpdate`：清空 fee_info_cache_（合约信息需通过 `addFeeInfo` / `om_add_fee_info` 逐个传入）
- `handleOrder`：补充缺失的合约信息到缓存

---

## 7. 错误码

| 错误码 | 值 | 触发条件 |
|--------|-----|---------|
| OM_NotInited | -8 | service 未初始化时调用接口 |
| OM_AlreadyInited | -9 | 重复调用 init |
| OM_InvalidArg | -1 | 参数校验失败 |
| OM_MissingSettlementPrice | -10 | 日终结算时缺少结算价 |
| OM_MissingFeeInfo | -11 | 日终结算时缺少费率信息 |

---

## 8. 服务类详解

### 8.1 设计原则：依赖注入

service 层引入依赖注入设计模式，将复杂业务逻辑从 OmService 解耦到专用服务类：

- **构造函数注入**：各服务类通过构造函数接收所需依赖（Processor、Store 指针）
- **依赖结构体**：`TradingDayInitDeps`、`TradingDayEndDeps` 封装服务所需的全部依赖
- **无单例依赖**：服务类不直接访问 `DbManager::instance()`，依赖全部由调用方传入

### 8.2 TradingDayInitService（交易日初始化服务）

**职责**：`om_trading_day_update` 的业务实现，负责新交易日开始时的数据准备工作。

**核心流程**（在事务内执行）：
1. **cleanupDailyData()** - 清理当日数据
   - 清空当日委托（order_store->deleteAll）
   - 删除已平仓持仓（pu_store/acct_pu_store->deleteClosedUnits）
   - 清空合约统计（cs_store/acct_cs_store->deleteAll）
   - 清空当日成交（trade_store->deleteAll）
   - 删除已拆解组合单元（combo_store->deleteByExistedFlag(0)）

2. **rebuildStrategyContractStats()** - 重建策略级合约统计
   - 查询所有未平仓 PositionUnit
   - 按 (run_id + account_id + account_type + strategy_id + code) 聚合
   - upsert ContractStat 记录

3. **rebuildAccountContractStats()** - 重建账户级合约统计
   - 类似策略级，针对 AccountPositionUnit 聚合生成 AccountContractStat

4. **validateAccountFunds()** - 账户资金校验
   - 聚合策略级资金到账户级
   - 校验：account_cash >= sum(avail_cash)
   - 校验：account_margin >= sum(margin)
   - 校验：account_equity >= sum(equity)

**错误码**：OM_FundCheckFailed(-13) - 资金校验失败

### 8.3 TradingDayEndService（交易日结束服务）

**职责**：`om_trading_day_end` 的业务实现，负责日终结算全流程。

**核心流程**（在事务内执行）：

1. **Step 0: processNonTerminalOrders()** - 未终态委托处理
   - 查询所有非终态委托（status != Filled/CancelFilled/PartiallyCanceled/Rejected）
   - 未成交委托转为 CancelFilled，部成委托转为 PartiallyCanceled
   - 组合委托直接 upsert，普通委托调用 OrderProcessor 处理以释放冻结

2. **Step 0.5: checkFrozenAssets()** - 冻结资产检测（日志告警）
   - 检查策略级/账户级资金 frozen_cash/account_frozen
   - 检查合约统计 today_long_frozen/yesterday_long_frozen/today_short_frozen/yesterday_short_frozen
   - 仅记录 ERROR 日志，不阻断结算流程

3. **Step 1: createFundSnapshots()** - 结算前资金快照
   - 策略级：FundtableSnapshotHandler->createSnapshot()
   - 账户级：AccountFundtableProcessor->createSnapshot()

4. **Step 2: validateCacheCompleteness()** - 缓存完整性校验
   - 校验 price_cache 包含所有持仓合约的结算价
   - 校验 fee_info_cache 包含所有持仓合约的费率信息
   - 缺失时返回 OM_MissingSettlementPrice 或 OM_MissingFeeInfo

5. **Step 3: settleStrategyFunds()** - 策略级持仓结算
   - 逐作用域查询未平仓 PositionUnit
   - 计算结算盈亏：CalcHelper::calcPnl()
   - 盈亏核对（与 fund.pnl 对比，阈值 1）
   - 转移盈亏到 avail_cash，清空 pnl
   - 逐手结算更新保证金：CalcHelper::settleOneUnit()
   - 更新持仓单元 hold_cost/margin/pnl，更新 fund 记录

6. **Step 4: settleAccountFunds()** - 账户级持仓结算
   - 类似策略级流程，针对 AccountPositionUnit/AccountFundtable

7. **Step 5: archiveHistory()** - 历史归档
   - 委托历史：order_store->queryAll → order_his_store->batchInsert
   - 成交历史：trade_store->queryAllByDate → trade_his_store->batchInsert
   - 组合单元历史：combo_store->queryByExistedFlag(0) → combo_his_store->batchInsert

### 8.4 QueryKitService（查询服务）

**职责**：对外查询接口的统一封装，管理 QueryKitPool 生命周期。

**设计特点**：
- **独立套件池**：查询使用独立的 SQLite 连接，与写入操作隔离
- **作用域缓存**：缓存 run_id/account_id/account_type，支持简化版接口
- **查询套件池生命周期**：
  - `init()` - OmService::tradingDayUpdate() 中初始化
  - `release()` - OmService::tradingDayEnd()/release() 时释放

**接口分类**：
1. **完整参数查询**：queryOrderById, queryOrdersByScope, queryContractStat, queryAccountContractStat, queryFund, queryAccountFund
2. **简化版查询**（使用缓存的作用域）：queryOrderByIdSimple, queryOrderIdsSimple, queryPositionCodesSimple, queryAccountPositionCodesSimple, queryContractStatSimple, queryAccountContractStatSimple, queryFundSimple, queryAccountFundSimple

### 8.5 ComboOrderService（组合委托服务）

**职责**：组合委托的专项处理，包括解析、拆腿、配对持仓。

**与普通委托的区别**：
- 组合委托 code 含 '&'（如 "DCE.b2606&b2612"）
- 需要拆分为两条单腿委托分别处理
- 成交后需配对两腿持仓创建 CombinationUnit

**核心流程**（handleInTx）：
1. **parseComboLegs()** - 解析两腿合约代码
   - 格式：`EXCHANGE.LEG1&LEG2` → leg1=`EXCHANGE.LEG1`, leg2=`EXCHANGE.LEG2`

2. **calcComboDeltaVolume()** - 计算本次成交增量
   - 查询已存储委托的 filled_volume
   - delta_volume = order.filled_volume - stored.filled_volume

3. **validateAndAggregateTrades()** - 校验并聚合成交
   - 查询该委托的所有 OmTrade
   - 校验两腿成交量相等、与组合委托 filled_volume 一致
   - 聚合两腿 turnover/fee

4. **generateLegOrder()** - 生成单腿委托
   - 单腿 order_id = combo_order_id + ".1" / ".2"
   - 复制组合委托的基础字段
   - 从 fee_cache 获取合约乘数、保证金率

5. **order_proc_->process()** - 处理两腿委托
   - 分别调用 OrderProcessor 处理两腿开仓委托

6. **pairComboLegPositions()** - 配对持仓
   - 查询该委托创建的两腿 AccountPositionUnit（unpaired）
   - 逐手创建 CombinationUnit 记录
   - 更新持仓单元的 combination_id

---

## 9. 依赖关系

```
service/ 依赖：
  ├── core/（所有 Processor 类）
  ├── data/（所有 Store 类）
  ├── include/（数据类型）
  ├── utils/（LogManager）
  └── kit/（QueryKitPool，仅 QueryKitService 依赖）

service/ 内部依赖关系：
  ┌─────────────────────────────────────────────────────────┐
  │                    OmService (主入口)                     │
  │  - 持有并管理所有 Processor 生命周期                        │
  │  - 持有 TradingDayInitService/TradingDayEndService      │
  │  - 调用 QueryKitService::init/release                     │
  │  - 持有 ComboOrderService                                │
  ├─────────────────────────────────────────────────────────┤
  │  TradingDayInitService ← TradingDayInitDeps (依赖注入)    │
  │  TradingDayEndService ← TradingDayEndDeps (依赖注入)      │
  │  ComboOrderService ← OrderProcessor/OrderStore/...        │
  │  QueryKitService (独立单例)                               │
  └─────────────────────────────────────────────────────────┘

service/ 被依赖：
  └── api/（对外接口层调用 OmService、QueryKitService）
```

---

## 10. 相关文档

| 主题 | 位置 |
|------|------|
| 详细流程说明 | `03-implementation/flows/*.md` |
| 对外 API 接口 | `03-implementation/interfaces/public-apis.md` |
| 事务控制最佳实践 | `01-architecture/conventions.md` |
