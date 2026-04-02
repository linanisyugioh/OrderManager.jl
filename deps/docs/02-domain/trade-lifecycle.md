# 成交生命周期与字段定义

> OmTrade 结构体完整字段说明、与 OmOrder 的区别、使用场景

---

## 0. 当前实现说明

**重要**：当前版本 `om_handle_trade` **仅校验字段后直接写入 trade 表**，不参与持仓计算和资金计算，也不校验关联 Order 是否存在。§5.2 关联校验、§6 业务处理流程、§4.2 业务处理为**扩展预留**，详见 `03-implementation/flows/trade-flow.md` §1.1。

---

## 1. OmTrade 结构体定义

```c
typedef struct t_OmTrade {
    /* ========== 主键字段（7字段联合主键）========== */
    char    order_id[LEN_ID];           /**< 委托ID（必填），关联 OmOrder 表 */
    int32_t trade_date;                  /**< 成交日期 YYYYMMDD（必填），夜盘填交易归属日 */
    char    strategy_id[LEN_CODE];      /**< 策略ID（主键） */
    char    run_id[LEN_ID];              /**< 实例ID（主键） */
    char    account_id[LEN_ACCOUNT_ID]; /**< 账户ID（主键） */
    int32_t account_type;                /**< 账户类型（主键） */
    char    match_seqno[LEN_ID];        /**< 成交序号（主键），交易所返回的唯一标识 */
    int32_t match_type;                  /**< 成交回报类型，区分成交回报类型 */

    /* ========== 业务字段 ========== */
    char    code[LEN_CODE];             /**< 合约代码（必填） */
    char    product[LEN_PRODUCT];       /**< 品种代码 */
    int32_t market;                      /**< 市场/交易所 */
    char    cl_order_id[LEN_CODE];      /**< 客户端委托ID */
    int32_t side;                        /**< 买卖方向（必填） */
    int32_t volume;                      /**< 成交数量（必填），单位：手 */
    int64_t price;                       /**< 成交价格（必填），包含滑点，扩大一万倍 */
    int64_t filled_turnover;            /**< 合约价值，扩大一万倍 */
    int64_t fee;                         /**< 手续费，扩大一万倍，系统可计算 */

    /* ========== 关联委托信息（用于记录和校验）========== */
    int32_t order_volume;               /**< 原始委托量 */
    int64_t order_price;                /**< 原始委托价，扩大一万倍 */
    int64_t slippage;                   /**< 成交滑点，扩大一万倍 */
    int32_t date;                        /**< 实际发生交易日期，真实时间 */
    int32_t transact_time;               /**< 交易时间，格式 HHMMSSmmm，非主键仅用于记录 */
} OmTrade;
```

---

## 2. 字段详细说明

### 2.1 主键字段（必填）

| 字段 | 类型 | 必填 | 说明 | 错误后果 |
|------|------|------|------|----------|
| order_id | char[64] | ✓ | 委托唯一标识 | 无法关联委托记录 |
| trade_date | int32 | ✓ | 成交日期 YYYYMMDD | 数据归属错误 |
| strategy_id | char[32] | ✓ | 策略ID | 影响作用域定位 |
| run_id | char[64] | ✓ | 实例ID | 影响作用域定位 |
| account_id | char[64] | ✓ | 账户ID | 影响作用域定位 |
| account_type | int32 | ✓ | 账户类型 | 影响作用域定位 |
| match_seqno | char[64] | ✓ | 成交序号（主键，交易所唯一） | 重复成交 |
| match_type | int32 | ✓ | 成交回报类型 | 分类错误 |

**作用域（Scope）概念**：OmTrade 使用与 OmOrder 相同的4维度定位资金/持仓：
- `run_id` + `account_id` + `account_type` + `strategy_id`

### 2.2 业务必填字段

| 字段 | 类型 | 必填 | 说明 | 校验规则 |
|------|------|------|------|----------|
| code | char[32] | ✓ | 合约代码 | 非空，应与 FeeCodeInfo.code 一致 |
| side | int32 | ✓ | 买卖方向 | 必须是合法 OrderSide 枚举值 |
| volume | int32 | ✓ | 成交数量 | > 0 |
| price | int64 | ✓ | 成交价格 | > 0（扩大一万倍，包含滑点） |

### 2.3 资金计算字段

| 字段 | 类型 | 说明 | 维护方式 |
|------|------|------|----------|
| filled_turnover | int64 | 合约价值 | 成交时传入，系统校验 |
| fee | int64 | 手续费 | 可传入或由系统计算 |

**关键公式**：
```
filled_turnover = price × volume × contract_multiply
trade_price = price / 10000  （还原实际价格）
```

### 2.4 关联委托信息（辅助字段）

| 字段 | 类型 | 说明 | 用途 |
|------|------|------|------|
| order_volume | int32 | 原始委托量 | 校验成交不超过委托 |
| order_price | int64 | 原始委托价 | 计算滑点 |
| slippage | int64 | 成交滑点 | 分析执行质量 |
| date | int32 | 实际发生交易日期 | 真实交易时间（与 trade_date 可能不同，夜盘场景） |
| transact_time | int32 | 交易时间 | 格式 HHMMSSmmm，精确到毫秒 |

**滑点计算**：
```
slippage = (trade_price - order_price) × direction_sign
```

---

## 3. OmTrade 与 OmOrder 的区别

### 3.1 数据粒度

| 维度 | OmOrder | OmTrade |
|------|-------|-------|
| **粒度** | 委托级别 | 成交级别 |
| **一条委托** | 可能对应多条成交 | 一条成交对应一个委托片段 |
| **状态流转** | PendingNew → New → PartiallyFilled → Filled | 无状态，成交即确认 |
| **撤单** | 支持撤单 | 不支持撤单 |

### 3.2 使用场景

| 场景 | 推荐接口 | 理由 |
|------|----------|------|
| 从量化系统接收委托状态变化 | `om_handle_order` | 委托驱动，处理全流程 |
| 从交易所/柜台接收成交回报 | `om_handle_trade` | 成交驱动，精确记录 |
| 需要精确分析每笔成交 | `om_handle_trade` | 保留成交明细 |
| 委托与成交分离的系统 | `om_handle_trade` | 独立处理成交流 |

### 3.3 处理流程对比

```
om_handle_order 流程：
OmOrder(PendingNew) → 冻结资金 → OmOrder(New) → ... → OmOrder(Filled) → 释放冻结 → 创建持仓

om_handle_trade 流程（当前实现）：
OmTrade(成交) → 字段校验 → 直接写入 trade 表

om_handle_trade 流程（扩展预留）：
OmTrade(成交) → 校验关联 Order → 直接更新持仓和资金（无冻结阶段）
```

---

## 4. 成交回报类型（TradeReportType）

> **注意**：OmTrade 的 `match_type` 字段对应 `om_def.h` 中的 `TradeReportType` 枚举。

### 4.1 类型定义

| 值 | 枚举名 | 说明 |
|----|--------|------|
| 1 | TradeReportType_Normal | 普通回报 |
| 2 | TradeReportType_Cancel | 撤单回报 |
| 3 | TradeReportType_Abolish | 普通废单回报 |
| 4 | TradeReportType_InsideCancel | 内部撤单回报（还未到交易所便被撤下来） |
| 5 | TradeReportType_CancelAbolish | 撤单废单回报 |

### 4.2 业务处理（扩展预留，当前实现不做区分）

- **TradeReportType_Normal**：正常处理，更新持仓和资金
- **TradeReportType_Cancel**：根据业务需求处理，可能需要回滚
- **TradeReportType_Abolish**：普通废单，记录风控
- **TradeReportType_InsideCancel**：内部撤单，记录日志
- **TradeReportType_CancelAbolish**：撤单废单，记录风控

---

## 5. 字段校验清单

### 5.1 通用校验（所有成交）

| 校验项 | 规则 | 错误码 |
|--------|------|--------|
| order_id 非空 | strlen > 0 | OM_InvalidArg |
| trade_date 匹配 | == trading_date_ | OM_InvalidArg |
| code 非空 | strlen > 0 | OM_InvalidArg |
| side 合法 | inferDirection != 0 | TradeProc_InvalidArg |
| volume > 0 | > 0 | TradeProc_InvalidArg |
| price > 0 | > 0 | TradeProc_InvalidArg |
| match_seqno 非空 | strlen > 0 | TradeProc_InvalidArg |

### 5.2 关联校验（扩展预留，当前实现不执行）

| 校验项 | 规则 | 错误码 |
|--------|------|--------|
| 委托存在性 | order_id 存在于 order 表 | TradeProc_NotFound |
| 成交数量合理性 | volume <= order_volume - Σ已成交 | TradeProc_InvalidArg |

---

## 6. 业务处理流程（扩展预留，当前实现不执行）

### 6.1 开仓成交处理

```
1. 校验 Trade 字段合法性
2. 创建 PositionUnit（每手一条记录）
   - hold_cost = trade.price
   - open_price = trade.price
   - open_date = trading_date_
   - margin = 单手保证金
3. 更新 ContractStat
   - today_long_volume / today_short_volume += volume
4. 更新 Fundtable
   - margin += 总保证金
   - fee += trade.fee
   - avail_cash -= (margin + fee)  [注：trade 模式无冻结阶段]
```

### 6.2 平仓成交处理

```
1. 校验 Trade 字段合法性
2. 校验持仓充足性
3. FIFO 匹配 PositionUnit
4. 更新 PositionUnit
   - close_order_id = trade.order_id
   - close_price = trade.price
   - close_date = trading_date_
   - pnl = (close_price - hold_cost) × multiply × dir_sign
5. 更新 ContractStat
   - today_long_volume / today_short_volume -= volume
6. 更新 Fundtable
   - margin -= 释放保证金
   - fee += trade.fee
   - avail_cash += (释放保证金 + 实现盈亏 - fee)
```

---

## 7. 错误码汇总

### 7.1 TradeProcessor 错误码（-600 ~ -699）

| 错误码 | 值 | 说明 |
|--------|-----|------|
| TradeProc_Ok | 0 | 成功 |
| TradeProc_InvalidArg | -601 | 参数无效 |
| TradeProc_NotFound | -602 | 关联委托不存在 |
| TradeProc_StoreError | -603 | 存储错误 |
| TradeProc_DuplicateKey | -604 | 成交记录已存在 |

---

## 8. 相关文档

| 主题 | 位置 |
|------|------|
| 委托生命周期 | `02-domain/order-lifecycle.md` |
| 对外API接口 | `03-implementation/interfaces/public-apis.md` |
| 计算公式 | `02-domain/calc-formulas.md` |
| 枚举速查 | `00-overview/quick-reference.md` |

---

## 9. 数据库表结构

### 9.1 trade 表定义

```sql
CREATE TABLE IF NOT EXISTS "trade" (
    order_id          TEXT    NOT NULL,    -- 委托ID，关联 order 表
    trade_date        INTEGER NOT NULL,    -- 成交归属日期 YYYYMMDD
    strategy_id       TEXT    NOT NULL,    -- 策略ID（作用域）
    run_id            TEXT    NOT NULL,    -- 实例ID（作用域）
    account_id        TEXT    NOT NULL,    -- 账户ID（作用域）
    account_type      INTEGER NOT NULL,    -- 账户类型（作用域）
    match_seqno       TEXT    NOT NULL,    -- 成交序号，交易所返回的唯一标识
    match_type        INTEGER DEFAULT 0,   -- 成交类型：1=普通成交，2=撤单成交，3=修改，4=强制平仓
    code              TEXT    DEFAULT '',  -- 合约代码
    product           TEXT    DEFAULT '',  -- 品种代码
    market            INTEGER DEFAULT 0,   -- 市场/交易所
    cl_order_id       TEXT    DEFAULT '',  -- 客户端委托ID
    side              INTEGER DEFAULT 0,   -- 买卖方向，参考 OrderSide 枚举
    volume            INTEGER DEFAULT 0,   -- 成交数量（手数）
    price             INTEGER DEFAULT 0,   -- 成交价格，扩大一万倍
    filled_turnover   INTEGER DEFAULT 0,   -- 合约价值，扩大一万倍
    fee               INTEGER DEFAULT 0,   -- 手续费，扩大一万倍
    order_volume      INTEGER DEFAULT 0,   -- 原始委托量
    order_price       INTEGER DEFAULT 0,   -- 原始委托价，扩大一万倍
    slippage          INTEGER DEFAULT 0,   -- 成交滑点，扩大一万倍
    date              INTEGER DEFAULT 0,   -- 实际成交日期（夜盘场景可能与trade_date不同）
    transact_time     INTEGER DEFAULT 0,   -- 成交时间，格式 HHMMSSmmm
    PRIMARY KEY (order_id, trade_date, strategy_id, run_id, account_id, account_type, match_seqno)
);
```

### 9.2 字段与结构体映射

| 字段名 | 类型 | 对应结构体字段 | 说明 |
|--------|------|----------------|------|
| order_id | TEXT | OmTrade.order_id | 委托唯一标识 |
| trade_date | INTEGER | OmTrade.trade_date | 成交归属日期 YYYYMMDD |
| strategy_id | TEXT | OmTrade.strategy_id | 策略ID（主键/作用域） |
| run_id | TEXT | OmTrade.run_id | 实例ID（主键/作用域） |
| account_id | TEXT | OmTrade.account_id | 账户ID（主键/作用域） |
| account_type | INTEGER | OmTrade.account_type | 账户类型（主键/作用域） |
| match_seqno | TEXT | OmTrade.match_seqno | 成交序号，交易所唯一 |
| match_type | INTEGER | OmTrade.match_type | 成交回报类型 |
| code | TEXT | OmTrade.code | 合约代码 |
| product | TEXT | OmTrade.product | 品种代码 |
| market | INTEGER | OmTrade.market | 市场/交易所 |
| cl_order_id | TEXT | OmTrade.cl_order_id | 客户端委托ID |
| side | INTEGER | OmTrade.side | 买卖方向 |
| volume | INTEGER | OmTrade.volume | 成交数量 |
| price | INTEGER | OmTrade.price | 成交价格（×10000） |
| filled_turnover | INTEGER | OmTrade.filled_turnover | 合约价值（×10000） |
| fee | INTEGER | OmTrade.fee | 手续费（×10000） |
| order_volume | INTEGER | OmTrade.order_volume | 原始委托量 |
| order_price | INTEGER | OmTrade.order_price | 原始委托价（×10000） |
| slippage | INTEGER | OmTrade.slippage | 滑点（×10000） |
| date | INTEGER | OmTrade.date | 实际成交日期 |
| transact_time | INTEGER | OmTrade.transact_time | 成交时间 HHMMSSmmm |

### 9.3 索引设计

```sql
-- 按作用域+order_id查询（查询某委托的所有成交）
CREATE INDEX idx_trade_scope_orderid 
ON trade (run_id, account_id, account_type, strategy_id, order_id);

-- 按作用域+合约查询（查询某合约的成交历史）
CREATE INDEX idx_trade_scope_code 
ON trade (run_id, account_id, account_type, strategy_id, code);

-- 按成交日期查询（交易日初始化时清理数据）
CREATE INDEX idx_trade_date 
ON trade (trade_date);
```

### 9.4 数据存储特点

- **当前版本**：OmTrade仅作为成交记录存储，不参与持仓和资金计算
- **写入时机**：通过 `om_handle_trade` API接收成交回报时直接入库
- **查询场景**：
  - 按7字段主键查询单条成交
  - 按作用域+order_id查询某委托的所有成交
  - 按作用域查询某策略的所有成交
  - 按作用域+合约查询某合约的成交历史
- **清理策略**：交易日初始化时可按 `trade_date` 清理历史数据

---

## 10. TradeHis（成交历史表）

### 10.1 设计说明

`trade_his` 表结构与 `trade` 表完全相同，直接使用 `OmTrade` 结构体。在日终结算时生成当日成交快照，保留历史记录。

**与 OrderHis 的区别**：
- `OrderHis`：记录委托级别数据
- `TradeHis`：记录成交级别数据，一个委托可能对应多条成交

### 10.2 数据库表结构

```sql
CREATE TABLE IF NOT EXISTS trade_his (
    order_id          TEXT    NOT NULL,
    trade_date        INTEGER NOT NULL,
    strategy_id       TEXT    NOT NULL,
    run_id            TEXT    NOT NULL,
    account_id        TEXT    NOT NULL,
    account_type      INTEGER NOT NULL,
    match_seqno       TEXT    NOT NULL,
    match_type        INTEGER DEFAULT 0,
    code              TEXT    DEFAULT '',
    product           TEXT    DEFAULT '',
    market            INTEGER DEFAULT 0,
    cl_order_id       TEXT    DEFAULT '',
    side              INTEGER DEFAULT 0,
    volume            INTEGER DEFAULT 0,
    price             INTEGER DEFAULT 0,
    filled_turnover   INTEGER DEFAULT 0,
    fee               INTEGER DEFAULT 0,
    order_volume      INTEGER DEFAULT 0,
    order_price       INTEGER DEFAULT 0,
    slippage          INTEGER DEFAULT 0,
    date              INTEGER DEFAULT 0,
    transact_time     INTEGER DEFAULT 0,
    PRIMARY KEY (order_id, trade_date, strategy_id, run_id, account_id, account_type, match_seqno)
);

-- 按作用域和日期查询
CREATE INDEX IF NOT EXISTS idx_trade_his_scope_date ON trade_his 
    (run_id, account_id, account_type, strategy_id, trade_date);

-- 按日期查询
CREATE INDEX IF NOT EXISTS idx_trade_his_date ON trade_his (run_id, trade_date);
```

### 10.3 使用场景

1. **日终快照**：`OmService::tradingDayEnd` 中查询当日全部成交，批量插入历史表
2. **历史查询**：支持按作用域+日期或日期范围查询历史成交
3. **数据保留**：交易日初始化清空 `trade` 表时，历史数据已保存到 `trade_his`
