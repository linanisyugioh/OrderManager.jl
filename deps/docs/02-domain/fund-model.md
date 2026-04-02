# 资金模型定义

> Fundtable 字段定义、权益计算、日终结算模型

---

## 1. Fundtable（策略级资金）

每个策略账户一条资金记录，实时维护资金状态。

### 1.1 结构体定义

```c
typedef struct t_Fundtable {
    char run_id[LEN_ID];                /**< 实例ID（主键） */
    char account_id[LEN_ACCOUNT_ID];    /**< 账户ID（主键） */
    int32_t account_type;               /**< 账户类型（主键） */
    char strategy_id[LEN_CODE];         /**< 策略ID（主键） */
    int32_t currency;                   /**< 货币类型 */
    int64_t frozen_cash;                /**< 冻结资金（扩大一万倍） */
    int64_t margin;                     /**< 保证金（扩大一万倍） */
    int64_t fee;                        /**< 累计手续费（扩大一万倍） */
    int64_t pnl;                        /**< 浮动盈亏（扩大一万倍） */
    int64_t avail_cash;                 /**< 可用资金（扩大一万倍） */
    int64_t start_cash;                 /**< 起始资金（扩大一万倍） */
    int64_t minimum_cash;               /**< 最低可用资金（扩大一万倍） */
    int64_t equity;                     /**< 总权益（扩大一万倍） */
    int64_t start_equity;               /**< 起始权益（扩大一万倍） */
    int64_t minimum_equity;             /**< 最小权益（扩大一万倍） */
} Fundtable;
```

### 1.2 字段说明

| 字段 | 说明 | 计算公式/来源 |
|------|------|--------------|
| frozen_cash | 冻结资金 | 委托挂单的预估占用 |
| margin | 保证金 | 所有持仓保证金之和 |
| fee | 累计手续费 | 自起始以来的累计手续费 |
| pnl | 浮动盈亏 | 所有未平仓持仓的浮动盈亏 |
| avail_cash | 可用资金 | 扣除冻结和手续费后可用的资金 |
| start_cash | 起始资金 | 当日初始化时的可用资金 |
| minimum_cash | 最低可用资金 | 当日最低记录，用于回撤评估 |
| equity | 总权益 | `margin + avail_cash + frozen_cash + pnl` |
| start_equity | 起始权益 | 当日初始化时的总权益 |
| minimum_equity | 最小权益 | 当日最小记录，用于回撤评估 |

---

## 2. FundtableHis（资金历史）

日终结算前创建的资金快照。

### 2.1 结构体定义

与 Fundtable 相同，增加 oper_date 字段：

```c
typedef struct t_FundtableHis {
    char run_id[LEN_ID];
    char account_id[LEN_ACCOUNT_ID];
    int32_t account_type;
    char strategy_id[LEN_CODE];
    int32_t oper_date;                  /**< 快照日期（主键）YYYYMMDD */
    // ... 其他字段与 Fundtable 相同
} FundtableHis;
```

---

## 3. 权益计算

### 3.1 总权益公式

```
equity = margin + avail_cash + frozen_cash + pnl
```

**说明**：
- `fee` 不直接参与 equity 计算
- `fee` 已在成交时从 avail_cash 中扣除
- 重复扣减 fee 会导致权益计算错误

### 3.2 最低记录更新

```cpp
void updateMinima(Fundtable& fund) {
    // 重算权益
    fund.equity = fund.margin + fund.avail_cash + fund.frozen_cash + fund.pnl;
    
    // 更新最低可用资金
    if (fund.avail_cash < fund.minimum_cash) {
        fund.minimum_cash = fund.avail_cash;
    }
    
    // 更新最小权益
    if (fund.equity < fund.minimum_equity) {
        fund.minimum_equity = fund.equity;
    }
}
```

---

## 4. 日终结算（逐日盯市）

### 4.1 结算模型概述

本系统采用与中国期货市场一致的**逐日盯市（Mark-to-Market）**结算制度，又称每日无负债结算。

**核心原则**：
- 每日收盘后结算盈亏
- 持仓价更新为当日结算价
- 盈亏转入可用资金
- 保证金按结算价重算

**等价理解**：日终结算等价于以 0 手续费将所有持仓按结算价平仓，再按结算价重新开仓。

### 4.2 持仓价（hold_cost）规则

| 持仓类别 | hold_cost 含义 | 来源 |
|---------|---------------|------|
| 今仓（当日开仓） | 开仓成交价 | 开仓时写入 |
| 昨仓（跨日持仓） | **前一日结算价** | 日终结算时更新 |

### 4.3 结算核心步骤（概述）

> **详细流程实现**见 `03-implementation/flows/settlement-flow.md`

| 步骤 | 操作 | 资金影响 | 持仓影响 |
|------|------|---------|---------|
| 1 | 逐手计算结算盈亏 | `avail_cash += Σ(settlement_pnl)` | - |
| 2 | 盈亏兑现 | `pnl = 0` | - |
| 3 | 更新持仓价 | - | `hold_cost = settlement_price` |
| 4 | 重算保证金 | `avail_cash -= margin_delta` | `margin = new_margin` |

**结算盈亏公式**：
```
settlement_pnl = (settlement_price - hold_cost) × multiply × dir_sign
```

### 4.4 结算前后字段变化

**PositionUnit 字段**：

| 字段 | 盘中 | 日终结算后 |
|------|------|----------|
| hold_cost | 今仓=开仓价，昨仓=前日结算价 | 统一更新为当日结算价 |
| pnl | 浮动盈亏（实时变化） | 归零 |
| margin | 按开仓时计算的保证金 | 按结算价重新计算 |

**Fundtable 字段**：

| 字段 | 盘中 | 日终结算后 |
|------|------|----------|
| pnl | 所有未平仓持仓的浮动盈亏之和 | 归零 |
| avail_cash | 可用资金 | += 结算盈亏，+= 保证金差额退补 |
| margin | 所有持仓保证金之和 | 按结算价重新计算后之和 |
| equity | 总权益 | 保持不变（盈亏只是从 pnl 搬到了 avail_cash） |

---

## 5. Delta 更新机制

### 5.1 OrderContext Delta 字段

```c
typedef struct t_OrderContext {
    // ... 其他字段 ...
    int64_t delta_frozen_cash;   /**< 冻结资金变化量 */
    int64_t delta_avail_cash;    /**< 可用资金变化量 */
    int64_t delta_margin;        /**< 保证金变化量 */
    int64_t delta_fee;           /**< 手续费变化量 */
    int64_t delta_pnl;           /**< 盈亏变化量 */
} OrderContext;
```

### 5.2 Delta 计算场景

| 场景 | delta_frozen | delta_avail | delta_margin | delta_fee | delta_pnl |
|------|-------------|-------------|--------------|-----------|-----------|
| 开仓新委托 | +E | -E | 0 | 0 | 0 |
| 开仓成交 | -(M+F) | 0 | +M | +F | 0 |
| 开仓终态释放 | -R | +R | 0 | 0 | 0 |
| 平仓成交 | 0 | +R-M-F | -M | +F | -P |

**符号说明**：
- E: 估算冻结额
- M: 实际保证金
- F: 实际手续费
- R: 实现盈亏
- P: 原浮动盈亏

---

## 6. 数据库表结构

### 6.1 fundtable 表

```sql
CREATE TABLE IF NOT EXISTS fundtable (
    run_id          TEXT    NOT NULL,
    account_id      TEXT    NOT NULL,
    account_type    INTEGER NOT NULL,
    strategy_id     TEXT    NOT NULL,
    currency        INTEGER DEFAULT 0,
    frozen_cash     INTEGER DEFAULT 0,
    margin          INTEGER DEFAULT 0,
    fee             INTEGER DEFAULT 0,
    pnl             INTEGER DEFAULT 0,
    avail_cash      INTEGER DEFAULT 0,
    start_cash      INTEGER DEFAULT 0,
    minimum_cash    INTEGER DEFAULT 0,
    equity          INTEGER DEFAULT 0,
    start_equity    INTEGER DEFAULT 0,
    minimum_equity  INTEGER DEFAULT 0,
    PRIMARY KEY (run_id, account_id, account_type, strategy_id)
);
```

### 6.2 fundtablehis 表

```sql
CREATE TABLE IF NOT EXISTS fundtablehis (
    run_id          TEXT    NOT NULL,
    account_id      TEXT    NOT NULL,
    account_type    INTEGER NOT NULL,
    strategy_id     TEXT    NOT NULL,
    oper_date       INTEGER NOT NULL,
    currency        INTEGER DEFAULT 0,
    frozen_cash     INTEGER DEFAULT 0,
    margin          INTEGER DEFAULT 0,
    fee             INTEGER DEFAULT 0,
    pnl             INTEGER DEFAULT 0,
    avail_cash      INTEGER DEFAULT 0,
    start_cash      INTEGER DEFAULT 0,
    minimum_cash    INTEGER DEFAULT 0,
    equity          INTEGER DEFAULT 0,
    start_equity    INTEGER DEFAULT 0,
    minimum_equity  INTEGER DEFAULT 0,
    PRIMARY KEY (run_id, account_id, account_type, strategy_id, oper_date)
);
```

---

## 7. 相关文档

| 主题 | 文档位置 | 说明 |
|------|---------|------|
| **结算详细流程** | [`03-implementation/flows/settlement-flow.md`](../../03-implementation/flows/settlement-flow.md) | L3层：完整流程步骤、伪代码、事务边界 |
| **结算盈亏公式** | [`02-domain/calc-formulas.md`](./calc-formulas.md) §盈亏计算 | 计算公式详细定义与示例 |
| **Processor接口** | [`03-implementation/interfaces/processor-apis.md`](../../03-implementation/interfaces/processor-apis.md) §日终结算 | 资金快照接口定义 |
| **core模块职责** | [`01-architecture/module-core.md`](../01-architecture/module-core.md) | 资金计算模块架构 |
| **账户级资金模型** | [`02-domain/account-fund.md`](./account-fund.md) | 账户维度资金字段定义 |
| **资金历史快照** | [`02-domain/fund-model.md`](./fund-model.md) §2 FundtableHis | 日终结算前快照模型 |
| **枚举速查** | [`00-overview/quick-reference.md`](../00-overview/quick-reference.md) | 错误码、枚举值速查 |
| **文档导航** | [`00-overview/navigation.md`](../00-overview/navigation.md) §理解日终结算流程 | 任务导向文档索引 |
