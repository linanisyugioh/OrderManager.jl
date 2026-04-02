# 订单生命周期与字段定义

> OmOrder 结构体完整字段说明、状态流转规则

---

## 1. OmOrder 结构体定义

```c
typedef struct t_OmOrder {
    /* ========== 主键字段（6字段联合主键）========== */
    char    order_id[LEN_ID];           /**< 委托ID（必填） */
    int32_t oper_date;                   /**< 委托日期 YYYYMMDD（必填） */
    char    strategy_id[LEN_CODE];      /**< 策略ID（必填） */
    char    run_id[LEN_ID];              /**< 实例ID（必填） */
    char    account_id[LEN_ACCOUNT_ID]; /**< 账户ID（必填） */
    int32_t account_type;               /**< 账户类型（必填） */
    
    /* ========== 业务字段 ========== */
    char    cl_order_id[LEN_CODE];      /**< 客户端委托ID（与 include/om_data_types.h 一致为 LEN_CODE） */
    int32_t date;                        /**< 业务日期 */
    int32_t market;                      /**< 交易所（平仓时必填） */
    char    code[LEN_CODE];             /**< 合约代码（必填） */
    char    product[LEN_CODE];          /**< 产品代码 */
    int32_t status;                      /**< 委托状态（必填） */
    int32_t order_type;                  /**< 订单类型 */
    int32_t side;                        /**< 买卖方向（必填） */
    int32_t margin_ratio;               /**< 保证金率（系统维护） */
    int32_t volume;                      /**< 委托数量（必填） */
    int64_t price;                       /**< 委托价格（扩大一万倍，开仓必填） */
    int32_t contract_multiply;          /**< 合约乘数 */
    int32_t filled_volume;              /**< 已成交数量 */
    int64_t filled_turnover;            /**< 已成交金额（扩大一万倍） */
    int64_t frozen;                      /**< 冻结资金（系统维护） */
    int64_t fee;                         /**< 手续费（系统维护） */
    int32_t cancel_volume;              /**< 撤单数量 */
    int32_t cancel_flag;                /**< 撤单标识 */
    int64_t marketdata_time;            /**< 行情时间 */
    int32_t hedge_flag;                  /**< 套保标识 */
    int64_t create_time;                 /**< 创建时间 HHMMSSmmm */
    int64_t update_time;                 /**< 更新时间 HHMMSSmmm */
    int64_t finish_time;                 /**< 完成时间 HHMMSSmmm */
    char    security[LEN_SECURITY];     /**< 证券名称，长度以 include/om_data_types.h 为准（LEN_SECURITY=64） */
    int32_t err_code;                    /**< 错误码 */
    char    err_msg[LEN_ERR_MSG];       /**< 错误信息，长度以头文件为准 */
} OmOrder;
```

---

## 2. 字段详细说明

### 2.1 主键字段（必填）

| 字段 | 类型 | 必填 | 说明 | 错误后果 |
|------|------|------|------|----------|
| order_id | char[64] | ✓ | 委托唯一标识 | 空串导致无法查询历史状态 |
| oper_date | int32 | ✓ | 委托日期 YYYYMMDD | 必须与当前交易日一致 |
| strategy_id | char[32] | ✓ | 策略ID | 影响资金/持仓作用域定位 |
| run_id | char[64] | ✓ | 实例ID | 影响资金/持仓作用域定位 |
| account_id | char[64] | ✓ | 账户ID | 影响资金/持仓作用域定位 |
| account_type | int32 | ✓ | 账户类型 | 影响资金/持仓作用域定位 |

**作用域（Scope）概念**：4维度定位资金/持仓：
- `run_id` + `account_id` + `account_type` + `strategy_id`
- 账户级数据不含 `strategy_id` 维度

### 2.2 业务必填字段

| 字段 | 类型 | 必填 | 说明 | 校验规则 |
|------|------|------|------|----------|
| code | char[32] | ✓ | 合约代码 | 非空，应与 FeeCodeInfo.code 一致 |
| side | int32 | ✓ | 买卖方向 | 必须是合法 OrderSide 枚举值 |
| status | int32 | ✓ | 委托状态 | OrderStatus 枚举值 |
| volume | int32 | ✓ | 委托数量 | > 0 |
| price | int64 | 开仓必填 | 委托价格 | > 0（扩大一万倍） |
| market | int32 | 平仓必填 | 交易所 | 用于验证平仓方向合法性 |

### 2.3 系统计算字段（入参应填0）

| 字段 | 类型 | 说明 | 维护方式 |
|------|------|------|----------|
| frozen | int64 | 冻结资金 | 系统根据委托状态计算 |
| fee | int64 | 累计手续费 | 成交时累加计算 |
| margin_ratio | int32 | 保证金率 | 首次开仓时从 FeeCodeInfo 选择 |

**注意**：broker 报文中这三个字段始终为 0，由系统自维护。

### 2.4 成交相关字段

| 字段 | 类型 | 必填条件 | 计算公式 |
|------|------|----------|----------|
| filled_volume | int32 | 成交时必填 | **累计**已成交手数（非本次增量） |
| filled_turnover | int64 | filled_volume>0 时必填 | **累计**已成交金额（扩大一万倍），= Σ(成交均价 × 乘数 × 手数) |

**关键公式**：
```
trade_price = filled_turnover / filled_volume / multiply
```

filled_turnover 必须正确，否则导致 trade_price 错误，进而影响 margin/fee 计算。

---

## 3. OrderStatus 状态机

### 3.1 状态定义

**约定**：枚举值与 `include/om_def.h` 中 OrderStatus 一致。

| 值 | 枚举名 | 中文名 | 是否终态 | 触发场景 |
|----|--------|--------|----------|----------|
| 1 | PendingNew | 待报 | ✗ | 已接收待发送交易所 |
| 2 | New | 已报 | ✗ | 已发送到交易所 |
| 3 | PartiallyFilled | 部成 | ✗ | 部分成交 |
| 4 | Filled | 全成 | ✓ | 全部成交 |
| 5 | PendingCancel | 撤单待报 | ✗ | 撤单请求已发 |
| 6 | Canceling | 已报待撤 | ✗ | 等待撤单回报 |
| 7 | CancelFilled | 全撤 | ✓ | 全部撤单 |
| 8 | PartiallyCanceled | 部成部撤 | ✓ | 部分成交后撤单 |
| 9 | Rejected | 废单 | ✓ | 被交易所拒绝 |

### 3.2 状态转换图

```
PendingNew(1) → New(2) → PartiallyFilled(3) ──┬──→ Filled(4)（终态）
                              │                │
                              │                └──→ PartiallyCanceled(8)（终态）
                              │
                              └──→ PendingCancel(5)/Canceling(6) ──→ CancelFilled(7)（终态）
                           
任意状态 ──→ Rejected(9)（终态）
```

### 3.3 终态处理

**终态枚举**：Filled(4), PartiallyCanceled(8), CancelFilled(7), Rejected(9)（与 om_def.h 一致）

**终态行为**：
- 开仓委托终态时，释放全部剩余冻结资金
- 平仓委托终态时，无额外操作（平仓不冻结资金）

---

## 4. OrderSide 买卖方向

### 4.1 方向定义

| 值 | 枚举名 | 类型 | 持仓方向 | 说明 |
|----|--------|------|----------|------|
| 3 | Long_Open | 开仓 | 多头 | 开多仓 |
| 5 | Short_Open | 开仓 | 空头 | 开空仓 |
| 4 | Long_Close | 平仓 | 多头 | 平多仓（不分今昨） |
| 6 | Short_Close | 平仓 | 空头 | 平空仓（不分今昨） |
| 8 | Today_Long_Close | 平仓 | 多头 | 平今多仓（SHFE/INE专用） |
| 10 | Today_Short_Close | 平仓 | 空头 | 平今空仓（SHFE/INE专用） |
| 11 | PreDay_Long_Close | 平仓 | 多头 | 平昨多仓（SHFE/INE专用） |
| 12 | PreDay_Short_Close | 平仓 | 空头 | 平昨空仓（SHFE/INE专用） |

### 4.2 方向分类函数

```cpp
// 是否开仓方向
bool isOpenSide(int32_t side) {
    return side == Long_Open(3) || side == Short_Open(5);
}

// 是否今仓平仓
bool isTodayCloseSide(int32_t side) {
    return side == Today_Long_Close(8) || side == Today_Short_Close(10);
}

// 是否昨仓平仓
bool isPreDayCloseSide(int32_t side) {
    return side == PreDay_Long_Close(11) || side == PreDay_Short_Close(12);
}

// 推导持仓方向
int32_t inferDirection(int32_t side) {
    if (side ∈ {Long_Open, Long_Close, Today_Long_Close, PreDay_Long_Close})
        return PositionSide_Long(1);
    if (side ∈ {Short_Open, Short_Close, Today_Short_Close, PreDay_Short_Close})
        return PositionSide_Short(2);
    return 0;  // 无效
}
```

### 4.3 交易所与平仓方向合法性

| 交易所 | 允许的平仓方向 |
|--------|---------------|
| SHFE, INE | Today_Long_Close, Today_Short_Close, PreDay_Long_Close, PreDay_Short_Close |
| DCE, CZCE, CFFEX, GFEX | Long_Close, Short_Close |

**验证函数**：`CalcHelper::validateCloseSide(market, order_side)`

---

## 5. 委托处理增量计算

### 5.1 增量字段

**前提**：`order.filled_volume` 和 `order.filled_turnover` 为**累计值**，非本次增量。

```cpp
// OrderProcessor::process() 中计算
delta_filled_volume   = order.filled_volume   - stored_order.filled_volume   // 本次新增成交手数
delta_cancel_volume   = order.cancel_volume   - stored_order.cancel_volume
delta_filled_turnover = order.filled_turnover - stored_order.filled_turnover // 本次新增成交额

// 本次成交均价
trade_price = delta_filled_turnover / delta_filled_volume
```

### 5.2 增量约束

- `delta_filled_volume >= 0`
- `delta_cancel_volume >= 0`
- 任一 < 0 时返回 `OrderProc_InvalidState`

---

## 6. 字段校验清单

### 6.1 通用校验（所有委托）

| 校验项 | 规则 | 错误码 |
|--------|------|--------|
| order_id 非空 | strlen > 0 | OM_InvalidArg |
| oper_date 匹配 | == trading_date_ | OM_InvalidArg |
| code 非空 | strlen > 0 | OM_InvalidArg |
| side 合法 | inferDirection != 0 | OrderProc_InvalidArg |
| volume > 0 | > 0 | OrderProc_InvalidArg |

### 6.2 开仓委托校验

| 校验项 | 规则 | 错误码 |
|--------|------|--------|
| price > 0 | > 0 | （建议增强） |
| FeeCodeInfo.multiply > 0 | > 0 | OrderProc_InvalidArg |
| 保证金率可获取 | selectMarginRatio != 0 | OrderProc_InvalidMarginRatio |

### 6.3 平仓委托校验

| 校验项 | 规则 | 错误码 |
|--------|------|--------|
| market 合法 | 1-6 有效枚举 | （建议增强） |
| market + side 组合合法 | 见4.3节 | PositionProc_InvalidSideForMarket |
| 持仓量充足 | volume <= available | PositionProc_InsufficientPosition |

---

## 7. OrderHis（委托历史表）

### 7.1 设计说明

`order_his` 表结构与 `order` 表完全相同，直接使用 `OmOrder` 结构体。在日终结算时生成当日委托快照，保留历史记录。

**与 PositionUnitHis 的区别**：
- `PositionUnitHis`：平仓时实时写入，记录每笔被平的持仓
- `OrderHis`：日终时批量快照，记录当日所有委托

### 7.2 数据库表结构

```sql
CREATE TABLE IF NOT EXISTS order_his (
    order_id          TEXT    NOT NULL,
    oper_date         INTEGER NOT NULL,
    strategy_id       TEXT    NOT NULL,
    run_id            TEXT    NOT NULL,
    account_id        TEXT    NOT NULL,
    account_type      INTEGER NOT NULL,
    cl_order_id       TEXT    DEFAULT '',
    date              INTEGER DEFAULT 0,
    market            INTEGER DEFAULT 0,
    code              TEXT    DEFAULT '',
    product           TEXT    DEFAULT '',
    status            INTEGER DEFAULT 0,
    order_type        INTEGER DEFAULT 0,
    side              INTEGER DEFAULT 0,
    margin_ratio      INTEGER DEFAULT 0,
    volume            INTEGER DEFAULT 0,
    price             INTEGER DEFAULT 0,
    contract_multiply INTEGER DEFAULT 0,
    filled_volume     INTEGER DEFAULT 0,
    filled_turnover   INTEGER DEFAULT 0,
    frozen            INTEGER DEFAULT 0,
    fee               INTEGER DEFAULT 0,
    cancel_volume     INTEGER DEFAULT 0,
    cancel_flag       INTEGER DEFAULT 0,
    marketdata_time   INTEGER DEFAULT 0,
    hedge_flag        INTEGER DEFAULT 0,
    create_time       INTEGER DEFAULT 0,
    update_time       INTEGER DEFAULT 0,
    finish_time       INTEGER DEFAULT 0,
    security          TEXT    DEFAULT '',
    err_code          INTEGER DEFAULT 0,
    err_msg           TEXT    DEFAULT '',
    PRIMARY KEY (order_id, oper_date, strategy_id, run_id, account_id, account_type)
);

-- 按作用域和日期查询
CREATE INDEX IF NOT EXISTS idx_order_his_scope_date ON order_his 
    (run_id, account_id, account_type, strategy_id, oper_date);

-- 按日期查询
CREATE INDEX IF NOT EXISTS idx_order_his_date ON order_his (run_id, oper_date);
```

### 7.3 使用场景

1. **日终快照**：`OmService::tradingDayEnd` 中查询当日全部委托，批量插入历史表
2. **历史查询**：支持按作用域+日期或日期范围查询历史委托
3. **数据保留**：交易日初始化清空 `order` 表时，历史数据已保存到 `order_his`

---

## 8. 相关文档

| 主题 | 文档位置 | 层级 | 说明 |
|------|---------|------|------|
| **委托处理流程** | [`03-implementation/flows/order-flow.md`](../03-implementation/flows/order-flow.md) | L3 | 委托处理详细步骤与数据变更 |
| **Processor接口** | [`03-implementation/interfaces/processor-apis.md`](../03-implementation/interfaces/processor-apis.md) | L3 | OrderProcessor 方法签名 |
| **core模块** | [`01-architecture/module-core.md`](../01-architecture/module-core.md) | L1 | 委托处理模块架构 |
| **计算公式** | [`02-domain/calc-formulas.md`](./calc-formulas.md) | L2 | 保证金/手续费计算 |
| **持仓模型** | [`02-domain/position-model.md`](./position-model.md) | L2 | 委托驱动的持仓变更 |
| **枚举速查** | [`00-overview/quick-reference.md`](../00-overview/quick-reference.md) | L0 | OrderStatus/OrderSide 枚举值 |
| **字段要求** | [`04-reference/order-fields.md`](../04-reference/order-fields.md) | L4 | OmOrder 字段校验规则 |
| **文档导航** | [`00-overview/navigation.md`](../00-overview/navigation.md) | L0 | 按任务类型导航 |
