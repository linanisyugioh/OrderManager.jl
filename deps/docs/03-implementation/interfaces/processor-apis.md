# 接口：Processor 层 API 汇总

> 从 old_docs/modules/mod_core.md §3.x 迁移  
> core 层所有 Processor 类的接口定义

---

## 1. OrderProcessor

```cpp
class OrderProcessor {
public:
    OrderProcessor(OrderStore*          order_store,
                   PositionProcessor*   pos_proc,
                   FundtableProcessor*  fund_proc,
                   AccountPositionProcessor* acct_pos_proc,  // 【新增】
                   AccountFundtableProcessor* acct_fund_proc); // 【新增】

    /**
     * @brief om_handle_order 主入口
     * @param order      委托数据（全量传入），主键字段必须有效
     * @param fee_info   费率与合约参数
     * @param last_price 【新增】缓存的最新价（×10000，默认0），用于开仓时计算初始浮动盈亏
     * @return 0 成功；负数错误码
     *
     * 统一入口检查（平仓委托）：
     *   1. 交易所与平仓方向组合合法性检查
     *   2. 可用持仓量检查
     *
     * 开仓成交逻辑：
     *   - 若 last_price > 0 且与成交价不同，计算初始盈亏填入 ctx.delta_pnl
     *   - 公式：(last_price - hold_cost) × multiply × dir_sign
     */
    int process(const OmOrder& order, const FeeCodeInfo& fee_info, int64_t last_price = 0);

private:
    OrderStore*                order_store_;
    PositionProcessor*         pos_proc_;
    FundtableProcessor*        fund_proc_;
    AccountPositionProcessor*  acct_pos_proc_;   // 【新增】
    AccountFundtableProcessor* acct_fund_proc_;  // 【新增】
};
```

---

## 2. PositionProcessor（策略级持仓）

```cpp
class PositionProcessor {
public:
    PositionProcessor(PositionUnitStore* pu_store,
                      PositionUnitHisStore* pu_his_store,
                      ContractStatStore* cs_store);

    /**
     * @brief 平仓量早失败校验（只读，无副作用）
     * @return 0 充足；PositionProc_InsufficientPosition 不足；PositionProc_StoreError 查询失败
     */
    int checkCloseVolume(const OrderContext& ctx);

    /**
     * @brief 开仓新委托到达：在 ContractStat 中冻结对应开仓量（当前为空操作，预留扩展）
     */
    int onOpenOrderFrozen(OrderContext& ctx);

    /**
     * @brief 开仓成交：创建 PositionUnit，更新 ContractStat，写 ctx delta
     *
     * 【新增】初始浮动盈亏计算：
     *   若 ctx.last_price > 0 且与成交价不同，计算 (last_price - hold_cost) × multiply × dir_sign
     *   结果累加到 ctx.delta_pnl，并设置 PositionUnit.pnl 初始值
     */
    int onOpenFill(OrderContext& ctx);

    /**
     * @brief 开仓委托撤单：当前为空操作
     */
    int onOpenOrderCancel(OrderContext& ctx);

    /**
     * @brief 新平仓委托到达：冻结对应持仓量
     * @note 不写 ctx 的 delta 字段（平仓新委托无资金变化）
     */
    int onCloseOrderNew(const OrderContext& ctx);

    /**
     * @brief 平仓成交：FIFO 匹配 PositionUnit，写 ctx delta
     * @return PositionProc_InsufficientPosition 持仓不足
     */
    int onCloseFill(OrderContext& ctx);

    /**
     * @brief 平仓委托撤单：释放冻结持仓量（LIFO顺序）
     * @note 不写 ctx 的 delta 字段
     */
    int onCloseOrderCancel(const OrderContext& ctx);

    /**
     * @brief 【已废弃】行情刷新：批量更新指定合约全部未平仓持仓的浮动盈亏
     * @deprecated 改用 calcPnlDeltaByContractStat 以提高性能
     * @param out_deltas 各作用域的浮盈变化量列表
     */
    int updateFloatingPnl(const char* code, int64_t last_price,
                          std::vector<ScopePnlDelta>& out_deltas);

    /**
     * @brief 【性能优化版】行情刷新：基于ContractStat计算盈亏变化
     * 
     * 核心优化：
     * - 不再逐手查询PositionUnit，而是从ContractStat获取总持仓量
     * - 基于价差计算盈亏变化：delta_pnl = 净持仓量 × 乘数 × 价差
     * - 不更新PositionUnit.pnl（平仓时回填）
     * - 支持结算价计算场景（settlement_price > 0）
     * 
     * 计算公式：
     *   net_position = (today_long + yesterday_long) - (today_short + yesterday_short)
     *   price_diff = base_price - prev_price
     *   delta_pnl = net_position × multiply × price_diff
     * 
     * 其中：
     * - base_price: 盘中用last_price，收盘结算用settlement_price
     * - prev_price: 有缓存用cached_last_price，无缓存用pre_settlement_price
     * 
     * @param code 合约代码
     * @param last_price 最新价（扩大一万倍）
     * @param base_price 基准价格（扩大一万倍）：盘中为last_price，收盘结算时为settlement_price
     * @param is_settlement_price 是否为结算价计算（true=收盘结算，false=盘中行情）
     * @param has_cached_price 是否有缓存的上次最新价
     * @param cached_last_price 缓存的上次最新价（has_cached_price为true时有效）
     * @param pre_settlement_price 昨结算价（has_cached_price为false时作为基准）
     * @param fee_info 合约费率信息（含乘数multiply）
     * @param out_deltas 输出：各作用域的盈亏变化量列表
     * @return 0 成功；负数错误码
     */
    int calcPnlDeltaByContractStat(const char* code,
                                   int64_t last_price,
                                   int64_t base_price,
                                   bool is_settlement_price,
                                   bool has_cached_price,
                                   int64_t cached_last_price,
                                   int64_t pre_settlement_price,
                                   const FeeCodeInfo& fee_info,
                                   std::vector<ScopePnlDelta>& out_deltas);

private:
    PositionUnitStore* pu_store_;
    PositionUnitHisStore* pu_his_store_;
    ContractStatStore* cs_store_;
};
```

### 2.1 辅助结构体

```cpp
typedef struct t_ScopePnlDelta {
    char    run_id[LEN_ID];
    char    account_id[LEN_ACCOUNT_ID];
    int32_t account_type;
    char    strategy_id[LEN_CODE];
    int64_t delta_pnl;    // 本次浮盈变化量（多空合计，×10000）
} ScopePnlDelta;
```

### 2.2 冻结/成交/释放三阶段策略

| 阶段 | 方法 | 策略 | 操作对象 |
|------|------|------|----------|
| 冻结 | onCloseOrderNew | **FIFO**（先进先出）| ContractStat.frozen |
| 成交 | onCloseFill | **FIFO**（先进先出）| PositionUnit |
| 释放 | onCloseOrderCancel | **LIFO**（后进先出）| ContractStat.frozen |

---

## 3. FundtableProcessor（策略级资金）

```cpp
class FundtableProcessor {
public:
    explicit FundtableProcessor(FundtableStore* fund_store);

    /**
     * @brief 统一应用 OrderContext 中累积的所有 delta（om_handle_order 路径）
     * @param ctx 全部 delta_* 字段已填写完毕
     * @return 0 成功；FundtableProc_NotFound 资金记录不存在
     * @note 5个 delta 全为 0 时提前返回，跳过 DB 读写
     */
    int apply(const OrderContext& ctx);

    /**
     * @brief 行情刷新后更新 Fundtable.pnl（om_handle_newprice 路径，单作用域）
     * @param delta_pnl 本次浮盈变化量（×10000）
     */
    int updatePnl(const char* run_id, const char* account_id,
                  int32_t account_type, const char* strategy_id,
                  int64_t delta_pnl);

    /**
     * @brief 行情路径：批量更新多个作用域的盈亏（性能优化版）
     * 用于 om_handle_newprice：将 PositionProcessor 计算出的多个 delta_pnl
     * 一次性批量应用到对应作用域的 Fundtable.pnl。
     * @param deltas 盈亏变化数组，每个元素包含作用域主键和盈亏变化量
     * @return 0 成功；FundtableProc_StoreError
     */
    int batchUpdatePnl(const std::vector<FundPnlDelta>& deltas);

private:
    FundtableStore* fund_store_;

    /** 重算 equity，更新 minimum_cash / minimum_equity */
    static void updateMinima(Fundtable& fund);
};
```

### 3.1 apply 内部逻辑

5 个 delta 全为 0 时跳过；否则按 ctx 作用域查询 Fundtable → 累加 frozen_cash/avail_cash/margin/fee/pnl → updateMinima → 更新数据库。

### 3.2 updateMinima 逻辑

equity = margin + avail_cash + frozen_cash + pnl；若 avail_cash < minimum_cash 或 equity < minimum_equity 则更新对应 minimum 字段。

---

## 4. AccountPositionProcessor（账户级持仓）

```cpp
class AccountPositionProcessor {
public:
    AccountPositionProcessor(AccountPositionUnitStore* pu_store,
                              AccountContractStatStore* cs_store);

    /**
     * @brief 平仓量早失败校验
     * @return 0 充足；AccountPositionProc_InsufficientPosition 不足
     */
    int checkCloseVolume(const OrderContext& ctx);

    /**
     * @brief 开仓新委托到达：当前为空操作
     */
    int onOpenOrderFrozen(OrderContext& ctx);

    /**
     * @brief 开仓成交：插入 AccountPositionUnit
     *
     * 【新增】初始浮动盈亏计算（同策略级）：
     *   若 ctx.last_price > 0 且与成交价不同，计算 (last_price - hold_cost) × multiply × dir_sign
     *   结果累加到 ctx.delta_acct_pnl，并设置 AccountPositionUnit.pnl 初始值
     */
    int onOpenFill(OrderContext& ctx);

    /**
     * @brief 开仓委托撤单：当前为空操作
     */
    int onOpenOrderCancel(OrderContext& ctx);

    /**
     * @brief 平仓新委托到达：冻结 AccountContractStat
     */
    int onCloseOrderNew(const OrderContext& ctx);

    /**
     * @brief 平仓成交：FIFO 匹配 AccountPositionUnit
     */
    int onCloseFill(OrderContext& ctx);

    /**
     * @brief 平仓委托撤单：释放冻结（LIFO）
     */
    int onCloseOrderCancel(OrderContext& ctx);

    /**
     * @brief 【已废弃】行情刷新：批量更新 AccountPositionUnit.pnl
     * @deprecated 改用 calcPnlDeltaByContractStat 以提高性能
     */
    int updateFloatingPnl(const char* code, int64_t last_price,
                          std::vector<AccountScopePnlDelta>& out_deltas);

    /**
     * @brief 【性能优化版】行情刷新：基于AccountContractStat计算盈亏变化
     * 
     * 逻辑与策略级 calcPnlDeltaByContractStat 完全一致，仅作用域不同（不含strategy_id）
     * 
     * 支持结算价计算场景（settlement_price > 0），详见PositionProcessor::calcPnlDeltaByContractStat
     * 
     * @param code 合约代码
     * @param last_price 最新价（扩大一万倍）
     * @param base_price 基准价格（扩大一万倍）：盘中为last_price，收盘结算时为settlement_price
     * @param is_settlement_price 是否为结算价计算（true=收盘结算，false=盘中行情）
     * @param has_cached_price 是否有缓存的上次最新价
     * @param cached_last_price 缓存的上次最新价（has_cached_price为true时有效）
     * @param pre_settlement_price 昨结算价（has_cached_price为false时作为基准）
     * @param fee_info 合约费率信息（含乘数multiply）
     * @param out_deltas 输出：各账户作用域的盈亏变化量列表
     * @return 0 成功；负数错误码
     */
    int calcPnlDeltaByContractStat(const char* code,
                                   int64_t last_price,
                                   int64_t base_price,
                                   bool is_settlement_price,
                                   bool has_cached_price,
                                   int64_t cached_last_price,
                                   int64_t pre_settlement_price,
                                   const FeeCodeInfo& fee_info,
                                   std::vector<AccountScopePnlDelta>& out_deltas);

private:
    AccountPositionUnitStore* pu_store_;
    AccountContractStatStore* cs_store_;
};
```

### 4.1 辅助结构体

```cpp
typedef struct t_AccountScopePnlDelta {
    char    run_id[LEN_ID];
    char    account_id[LEN_ACCOUNT_ID];
    int32_t account_type;
    int64_t delta_pnl;
} AccountScopePnlDelta;
```

### 4.2 关键区别

与 PositionProcessor 完全一致，仅作用域不同：
- PositionProcessor 作用域：run_id + account_id + account_type + strategy_id
- AccountPositionProcessor 作用域：run_id + account_id + account_type

---

## 5. AccountFundtableProcessor（账户级资金）

```cpp
class AccountFundtableProcessor {
public:
    explicit AccountFundtableProcessor(AccountFundtableStore* store,
                                        AccountFundtableHisStore* his_store);

    /**
     * @brief 应用 OrderContext 中的 delta_acct_* 字段
     * @note 6个 delta 全为 0 时提前返回
     */
    int apply(const OrderContext& ctx);

    /**
     * @brief 行情路径：更新 AccountFundtable 的浮动盈亏
     */
    int updatePnl(const char* run_id, const char* account_id,
                  int32_t account_type, int64_t delta_pnl);

    /**
     * @brief 创建账户资金快照
     */
    int createSnapshot(int32_t oper_date, bool skip_duplicate = true);

    /**
     * @brief 查询指定日期的账户资金快照
     */
    int querySnapshot(const char* run_id, int32_t oper_date,
                      std::vector<AccountFundtableHis>& out);

    /**
     * @brief 查询账户资金历史曲线
     */
    int queryEquityCurve(const char* run_id, const char* account_id,
                         int32_t start_date, int32_t end_date,
                         std::vector<AccountFundtableHis>& out);

private:
    AccountFundtableStore* store_;
    AccountFundtableHisStore* his_store_;

    static void updateMinima(AccountFundtable& fund);
    void convertToHis(const AccountFundtable& fund, int32_t oper_date,
                      AccountFundtableHis* out);
};
```

---

## 6. TradeProcessor（成交处理）

```cpp
class TradeProcessor {
public:
    explicit TradeProcessor(TradeStore* trade_store);

    /**
     * @brief 处理成交回报
     * @param trade 成交数据
     * @return 0 成功；TradeProc_InvalidArg 字段无效；TradeStore 错误码 存储失败
     */
    int process(const OmTrade& trade);

private:
    TradeStore* trade_store_;
};
```

**说明**：当前版本 Trade 仅校验字段后写入 trade 表，不参与持仓与资金计算。详见 `02-domain/trade-lifecycle.md`。

---

## 7. CalcHelper（计算辅助）

```cpp
class CalcHelper {
public:
    // 冻结估算
    static int64_t estimateFrozen(int32_t volume, int64_t price,
                                  int32_t multiply, int32_t margin_ratio,
                                  const FeeCodeInfo& fee);

    // 保证金率选择
    static int32_t selectMarginRatio(const FeeCodeInfo& fee_info,
                                      int32_t direction, int32_t hedge_flag);

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
    static bool isCloseSide(int32_t order_side);
    static bool isTodayCloseSide(int32_t order_side);
    static bool isPreDayCloseSide(int32_t order_side);
    static int32_t validateCloseSide(int32_t market, int32_t order_side);

    // 内部/辅助方法
    static int64_t calcPnl(int64_t hold_cost, int64_t current_price,
                          int32_t multiply, int32_t direction, int32_t volume);
    static bool needDistinguishTodayYesterday(int32_t market);
    static bool isTerminalOrderStatus(int32_t status);
    static std::pair<int64_t, int64_t> settleOneUnit(int64_t settlement_price,
        int32_t contract_multiply, int64_t old_margin, int32_t direction,
        const FeeCodeInfo& fee_info, int32_t hedge_flag);
    static bool matchOrderFilter(const OmOrder& order, const OrderFilter& filter);
    static std::string makeScopeKey(const char* run_id, const char* account_id,
        int32_t account_type, const char* strategy_id, const char* code);
    static std::string makeAccountScopeKey(const char* run_id, const char* account_id,
        int32_t account_type, const char* code);
};
```

### 7.1 方向系数约定

| PositionSide | 值 | direction_sign |
|-------------|-----|----------------|
| Long | 1 | +1 |
| Short | 2 | -1 |

### 7.2 计算公式

```cpp
// 保证金
margin = price × multiply × margin_ratio / 10000

// 手续费（按金额）
fee = price × multiply × rate / 100000

// 盈亏（current_price/hold_cost 已×10000，结果已为×10000，无需再除）
pnl = (current_price - hold_cost) × multiply × dir_sign
```

---

## 8. 错误码（-100 ~ -699）

### 8.1 OrderProcessor（-100 ~ -199）

| 错误码 | 值 | 触发条件 |
|--------|-----|----------|
| OrderProc_InvalidArg | -101 | 参数为空 |
| OrderProc_FeeCodeInvalid | -102 | 费率信息无效 |
| OrderProc_Internal | -103 | 内部逻辑错误（不应出现的状态） |
| OrderProc_InvalidState | -104 | 增量计算异常 |
| OrderProc_InvalidMarginRatio | -105 | 保证金率无效 |
| OrderProc_InvalidExchange | -106 | 交易所与平仓方向组合非法 |

### 8.2 PositionProcessor（-200 ~ -299）

| 错误码 | 值 | 触发条件 |
|--------|-----|----------|
| PositionProc_InsufficientPosition | -204 | 持仓不足 |
| PositionProc_InvalidSideForMarket | -205 | 交易所与方向组合非法 |

### 8.3 FundtableProcessor（-300 ~ -399）

| 错误码 | 值 | 触发条件 |
|--------|-----|----------|
| FundtableProc_NotFound | -303 | 资金记录不存在 |

### 8.4 账户级 Processor（与 include/om_error.h 一致）

| 错误码 | 值 | 所属类 | 触发条件 |
|--------|-----|--------|----------|
| AccountPositionProc_InsufficientPosition | -524 | AccountPositionProcessor | 持仓不足 |
| AccountFundtableProc_NotFound | -493 | AccountFundtableProcessor | 资金记录不存在 |

---

## 9. 相关文档

| 主题 | 位置 |
|------|------|
| 业务流程 | `03-implementation/flows/order-flow.md`, `03-implementation/flows/newprice-flow.md`, `03-implementation/flows/settlement-flow.md` |
| 数据模型 | `02-domain/position-model.md`, `02-domain/fund-model.md` |
| 计算公式 | `02-domain/calc-formulas.md` |
| Store 接口 | `03-implementation/interfaces/store-apis.md` |

---

## 10. 修订记录

| 日期 | 版本 | 变更内容 |
|------|------|----------|
| 2026-03-18 | v3.0 | PositionProcessor/AccountPositionProcessor `calcPnlDeltaByContractStat` 接口增加结算价支持参数（is_settlement_price, base_price, has_cached_price, cached_last_price, pre_settlement_price） |
| 2026-03-18 | v2.0 | PositionProcessor/AccountPositionProcessor 新增 `calcPnlDeltaByContractStat` 接口，废弃 `updateFloatingPnl` |
| 2026-03-14 | v1.0 | 初始版本 |
