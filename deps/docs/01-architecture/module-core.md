# 模块：core（核心计算层）

> 职责：委托增量状态处理、按手 FIFO 持仓开平匹配、资金冻结/保证金/手续费/盈亏计算

---

## 1. 模块概述

### 1.1 职责

- **委托增量状态处理**：解析委托状态变化，计算增量（delta_filled_volume, delta_cancel_volume）
- **FIFO 持仓开平匹配**：按先进先出顺序匹配平仓与持仓单元
- **资金计算**：冻结资金、保证金、手续费、盈亏的实时计算
- **双维度处理**：同时处理策略级和账户级持仓/资金

### 1.2 设计原则

1. **无状态**：core 层不在内存中缓存任何数据，所有历史状态均通过 data 层查询获取
2. **增量处理**：`om_handle_order` 每次只处理与上次调用的增量
3. **OrderContext 流水线**：单次 `process()` 调用内，所有中间计算结果写入 `OrderContext`
4. **错误码返回**：所有方法返回 `int`，0 成功，负数错误码，不抛异常
5. **日志规范**：开平仓、资金冻结/解冻等关键操作记录 `LOG_INFO`

---

## 2. 文件清单

core 层所有类位于 `namespace om`。


| 文件路径                                     | 类型   | 职责说明                                                 |
| ---------------------------------------- | ---- | ---------------------------------------------------- |
| `core/order_context.h`                   | 头文件  | OrderContext 结构体：单次 process() 调用的中间计算上下文             |
| `core/order_processor.h/.cc`             | 头/实现 | OrderProcessor 类：`om_handle_order` 主入口，编排各 Processor |
| `core/position_processor.h/.cc`          | 头/实现 | PositionProcessor 类：策略级持仓开/平仓、FIFO 匹配                |
| `core/account_position_processor.h/.cc`  | 头/实现 | AccountPositionProcessor 类：账户级持仓管理                   |
| `core/fundtable_processor.h/.cc`         | 头/实现 | FundtableProcessor 类：策略级资金计算                         |
| `core/account_fundtable_processor.h/.cc` | 头/实现 | AccountFundtableProcessor 类：账户级资金计算                  |
| `core/fundtable_snapshot_handler.h/.cc`  | 头/实现 | FundtableSnapshotHandler 类：资金快照处理                    |
| `core/trade_processor.h/.cc`             | 头/实现 | TradeProcessor 类：成交校验与入库（om_handle_trade）            |
| `core/calc_helper.h`                     | 头文件  | CalcHelper 纯静态辅助类：费率/保证金/盈亏/冻结估算公式                   |


---

## 3. 核心类职责

### 3.1 OrderProcessor（委托处理编排）

```cpp
class OrderProcessor {
public:
    int process(const OmOrder& order, const FeeCodeInfo& fee_info);
    // 构建 OrderContext → 增量计算 → 调用各 Processor → 回填 OmOrder 字段
};
```

**核心流程**：

1. 构建 OrderContext，查询历史状态，计算 delta
2. 早失败校验（平仓量检查、交易所合法性检查）
3. 调用 PositionProcessor 处理持仓变动
4. 调用 FundtableProcessor 应用资金变动
5. 回填 OmOrder 计算字段后 upsert

### 3.2 PositionProcessor（策略级持仓）

```cpp
class PositionProcessor {
public:
    int checkCloseVolume(const OrderContext& ctx);      // 平仓量早失败校验
    int onCloseOrderNew(const OrderContext& ctx);        // 新平仓委托：冻结持仓
    int onCloseOrderCancel(const OrderContext& ctx);     // 撤单：释放冻结
    int onOpenFill(OrderContext& ctx);                   // 开仓成交：创建持仓
    int onCloseFill(OrderContext& ctx);                  // 平仓成交：FIFO匹配平仓
    int updateFloatingPnl(const char* code, int64_t last_price, 
                          std::vector<ScopePnlDelta>& out_deltas);  // 行情刷新
};
```

**关键策略**：

- **冻结**：FIFO（先进先出），优先冻结昨仓
- **成交**：FIFO，按开仓时间匹配
- **释放**：LIFO（后进先出），优先释放今仓

### 3.3 AccountPositionProcessor（账户级持仓）

与 PositionProcessor 完全一致，仅作用域不同：

- PositionProcessor 作用域：run_id + account_id + account_type + strategy_id
- AccountPositionProcessor 作用域：run_id + account_id + account_type

### 3.4 FundtableProcessor（策略级资金）

```cpp
class FundtableProcessor {
public:
    int apply(const OrderContext& ctx);   // 统一应用 OrderContext 中累积的所有 delta
    int updatePnl(const char* run_id, const char* account_id,
                  int32_t account_type, const char* strategy_id,
                  int64_t delta_pnl);     // 行情路径更新盈亏
};
```

**apply 逻辑**：

1. 若 5 个 delta 字段全为 0，提前返回（无 DB 操作）
2. 查询 Fundtable 记录
3. 累加各 delta 字段
4. 重算 equity，更新 minimum_cash / minimum_equity
5. 写回数据库

### 3.5 AccountFundtableProcessor（账户级资金）

与 FundtableProcessor 逻辑一致，仅：

- 使用 AccountFundtableStore
- 操作 delta_acct_* 字段
- 不含 strategy_id 维度

---

## 4. OrderContext 结构

```cpp
typedef struct t_OrderContext {
    /* ---- 输入字段 ---- */
    OmOrder            new_order;           // 本次委托最新状态
    const FeeCodeInfo* fee_code_info;       // 费率与合约参数
    int32_t            has_old;             // 1=有历史记录
    int32_t            direction;           // 持仓方向（PositionSide）
    
    /* ---- 增量字段 ---- */
    int32_t            delta_filled_volume;     // 本次新增成交手数
    int32_t            delta_cancel_volume;     // 本次新增撤单手数
    int64_t            delta_filled_turnover;   // 本次新增成交额
    int64_t            trade_price;             // 本次成交均价
    
    /* ---- 策略级 delta 字段 ---- */
    int64_t            delta_frozen_cash;   // 冻结资金变化
    int64_t            delta_avail_cash;    // 可用资金变化
    int64_t            delta_margin;        // 保证金变化
    int64_t            delta_fee;           // 手续费变化
    int64_t            delta_pnl;           // 盈亏变化
    
    /* ---- 账户级 delta 字段 ---- */
    int64_t            delta_acct_frozen;   // 账户级冻结变化
    int64_t            delta_acct_cash;     // 账户级可用变化
    int64_t            delta_acct_margin;   // 账户级保证金变化
    int64_t            delta_acct_fee;      // 账户级手续费变化
    int64_t            delta_acct_pnl;      // 账户级盈亏变化
    int64_t            delta_acct_bouns;    // 账户级现金红利
} OrderContext;
```

---

## 5. CalcHelper 计算辅助

```cpp
class CalcHelper {
public:
    // 冻结估算
    static int64_t estimateFrozen(int32_t volume, int64_t price, 
                                  int32_t multiply, int32_t margin_ratio,
                                  const FeeCodeInfo& fee);
    
    // 保证金计算
    static int64_t calcMargin(int64_t price, int32_t multiply, int32_t margin_ratio);
    
    // 手续费计算（按金额/按手数）
    static int64_t calcFee(const FeeCodeInfo& fee, int64_t price, 
                          bool is_open, bool is_today);
    
    // 盈亏计算
    static int64_t calcClosePnl(int64_t hold_cost, int64_t close_price,
                                int32_t multiply, int32_t direction);
    static int64_t calcFloatPnl(int64_t hold_cost, int64_t last_price,
                                int32_t multiply, int32_t direction);
    
    // 方向推导与判断
    static int32_t inferDirection(int32_t side);
    static bool isOpenSide(int32_t order_side);
    static bool isTodayCloseSide(int32_t order_side);
    static int32_t validateCloseSide(int32_t market, int32_t order_side);
};
```

**计算公式**详见：`02-domain/calc-formulas.md`

---

## 6. 错误码（-100 ~ -399）


| 错误码                               | 值    | 所属类                | 触发条件       |
| --------------------------------- | ---- | ------------------ | ---------- |
| OrderProc_InvalidArg              | -101 | OrderProcessor     | 参数为空       |
| OrderProc_FeeCodeInvalid          | -102 | OrderProcessor     | 费率信息无效     |
| OrderProc_InvalidState            | -104 | OrderProcessor     | 增量计算异常     |
| OrderProc_InvalidMarginRatio      | -105 | OrderProcessor     | 保证金率无效     |
| PositionProc_InsufficientPosition | -204 | PositionProcessor  | 持仓不足       |
| PositionProc_InvalidSideForMarket | -205 | PositionProcessor  | 交易所与方向组合非法 |
| FundtableProc_NotFound            | -303 | FundtableProcessor | 资金记录不存在    |


---

## 7. 依赖关系

```
core/ 依赖：
  ├── data/（所有 Store 类）
  ├── include/（数据类型）
  └── utils/（LogManager）

core/ 被依赖：
  └── service/（OmService 调用各 Processor）
```

---

## 8. 相关文档


| 主题               | 文档位置                                                                                                  | 层级  | 说明                           |
| ---------------- | ----------------------------------------------------------------------------------------------------- | --- | ---------------------------- |
| **计算公式**         | `[02-domain/calc-formulas.md](../02-domain/calc-formulas.md)`                                         | L2  | 保证金/盈亏/手续费公式                 |
| **委托处理流程**       | `[03-implementation/flows/order-flow.md](../03-implementation/flows/order-flow.md)`                   | L3  | 委托处理详细步骤                     |
| **Processor 接口** | `[03-implementation/interfaces/processor-apis.md](../03-implementation/interfaces/processor-apis.md)` | L3  | 各类 Processor 方法签名            |
| **持仓模型**         | `[02-domain/position-model.md](../02-domain/position-model.md)`                                       | L2  | PositionUnit/ContractStat 定义 |
| **资金模型**         | `[02-domain/fund-model.md](../02-domain/fund-model.md)`                                               | L2  | Fundtable 字段与结算模型            |
| **订单生命周期**       | `[02-domain/order-lifecycle.md](../02-domain/order-lifecycle.md)`                                     | L2  | OmOrder 状态机定义                |
| **系统架构**         | `[01-architecture/system-overview.md](./system-overview.md)`                                          | L1  | 模块依赖关系总览                     |


