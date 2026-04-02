# 账户级持仓模型

> 从old_docs/account_position_design.md迁移精简  
> AccountPositionUnit 和 AccountContractStat 字段定义

---

## 1. 概述

账户级持仓与策略级持仓形成**平行独立**关系：

| 维度 | 策略级持仓 | 账户级持仓 |
|------|-----------|-----------|
| **粒度** | run_id + account_id + account_type + strategy_id + code | run_id + account_id + account_type + code |
| **用途** | 策略风控、策略盈亏统计 | 账户风控、账户盈亏统计 |
| **数据关系** | 独立维护，不从账户级派生 | 独立维护，不从策略级聚合 |
| **处理时机** | 委托处理时同步更新 | 委托处理时同步更新 |

**核心设计原则**：
- **完全独立**：账户级持仓不从策略级持仓聚合，独立维护
- **并行处理**：同一笔委托，同时更新策略级和账户级持仓
- **逻辑一致**：处理流程与策略级完全一致（FIFO、今昨区分）

---

## 2. AccountPositionUnit

### 2.1 结构体定义

```c
typedef struct t_AccountPositionUnit {
    int64_t id;                         /**< 主键，自增 */
    char run_id[LEN_ID];                /**< 实例ID */
    char account_id[LEN_ACCOUNT_ID];    /**< 账户ID */
    int32_t account_type;               /**< 账户类型 */
    char code[LEN_CODE];                /**< 合约代码 */
    char order_id[LEN_ID];              /**< 开仓委托ID */
    int32_t direction;                  /**< 持仓方向（PositionSide） */
    int64_t hold_cost;                  /**< 持仓成本价（扩大一万倍） */
    int32_t open_date;                  /**< 开仓日期 YYYYMMDD */
    int32_t open_time;                  /**< 开仓时间 HHMMSSmmm */
    int64_t open_price;                 /**< 开仓成交价（扩大一万倍） */
    char close_order_id[LEN_ID];        /**< 平仓委托ID */
    int64_t close_price;                /**< 平仓价（扩大一万倍） */
    int32_t close_date;                 /**< 平仓日期 */
    int32_t close_time;                 /**< 平仓时间 */
    int64_t fee;                        /**< 开仓+平仓手续费（扩大一万倍） */
    int64_t margin;                     /**< 保证金（扩大一万倍） */
    int64_t pnl;                        /**< 盈亏（扩大一万倍） */
    int32_t contract_multiply;          /**< 合约乘数 */
    int64_t combination_id;             /**< 组合ID，0=未参与组合；非0=关联 CombinationUnit.id（来自组合委托拆腿或保证金优惠申请） */
} AccountPositionUnit;
```

### 2.2 与 PositionUnit 的区别

| 字段 | PositionUnit | AccountPositionUnit |
|------|-------------|---------------------|
| 作用域 | 4维度（含strategy_id） | 3维度（无strategy_id） |
| combination_id | 无 | 有（组合关联，0=无，非0=CombinationUnit.id） |
| 其他字段 | 完全相同 | 完全相同 |

---

## 3. AccountContractStat

### 3.1 结构体定义

```c
typedef struct t_AccountContractStat {
    char run_id[LEN_ID];                /**< 实例ID */
    char account_id[LEN_ACCOUNT_ID];    /**< 账户ID */
    int32_t account_type;               /**< 账户类型 */
    char code[LEN_CODE];                /**< 合约代码 */
    
    /* 多头持仓 */
    int32_t today_long_volume;          /**< 今仓多头持仓量 */
    int32_t today_long_frozen;          /**< 今仓多头冻结量 */
    int32_t yesterday_long_volume;      /**< 昨仓多头持仓量 */
    int32_t yesterday_long_frozen;      /**< 昨仓多头冻结量 */
    
    /* 空头持仓 */
    int32_t today_short_volume;         /**< 今仓空头持仓量 */
    int32_t today_short_frozen;         /**< 今仓空头冻结量 */
    int32_t yesterday_short_volume;     /**< 昨仓空头持仓量 */
    int32_t yesterday_short_frozen;     /**< 昨仓空头冻结量 */
} AccountContractStat;
```

---

## 4. 账户级盈亏变化输出

```c
typedef struct t_AccountScopePnlDelta {
    char    run_id[LEN_ID];
    char    account_id[LEN_ACCOUNT_ID];
    int32_t account_type;
    int64_t delta_pnl;                  /**< 本次浮盈变化量（多空合计，×10000） */
} AccountScopePnlDelta;
```

与策略级 `ScopePnlDelta` 的区别：不含 `strategy_id` 维度。

---

## 5. 数据库表结构

### 5.1 account_position_unit 表

```sql
CREATE TABLE IF NOT EXISTS account_position_unit (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id              TEXT    NOT NULL,
    account_id          TEXT    NOT NULL,
    account_type        INTEGER NOT NULL,
    code                TEXT    DEFAULT '',
    order_id            TEXT    DEFAULT '',
    direction           INTEGER DEFAULT 0,
    hold_cost           INTEGER DEFAULT 0,
    open_date           INTEGER DEFAULT 0,
    open_time           INTEGER DEFAULT 0,
    open_price          INTEGER DEFAULT 0,
    close_order_id      TEXT    DEFAULT '',
    close_price         INTEGER DEFAULT 0,
    close_date          INTEGER DEFAULT 0,
    close_time          INTEGER DEFAULT 0,
    fee                 INTEGER DEFAULT 0,
    margin              INTEGER DEFAULT 0,
    pnl                 INTEGER DEFAULT 0,
    contract_multiply   INTEGER DEFAULT 0,
    combination_id      INTEGER DEFAULT 0
);

-- 索引（与 data/account_position_unit_store.cc createTable 一致）
CREATE INDEX IF NOT EXISTS idx_acct_pu_scope_dir_closedate
    ON account_position_unit (run_id, account_id, account_type, direction, close_date);

CREATE INDEX IF NOT EXISTS idx_acct_pu_scope_code_dir_closedate
    ON account_position_unit (run_id, account_id, account_type, code, direction, close_date);

CREATE INDEX IF NOT EXISTS idx_acct_pu_code_closedate
    ON account_position_unit (code, close_date);
```

### 5.2 account_contract_stat 表

```sql
CREATE TABLE IF NOT EXISTS account_contract_stat (
    run_id                  TEXT    NOT NULL,
    account_id              TEXT    NOT NULL,
    account_type            INTEGER NOT NULL,
    code                    TEXT    NOT NULL,
    today_long_volume       INTEGER DEFAULT 0,
    today_long_frozen       INTEGER DEFAULT 0,
    yesterday_long_volume   INTEGER DEFAULT 0,
    yesterday_long_frozen   INTEGER DEFAULT 0,
    today_short_volume      INTEGER DEFAULT 0,
    today_short_frozen      INTEGER DEFAULT 0,
    yesterday_short_volume  INTEGER DEFAULT 0,
    yesterday_short_frozen  INTEGER DEFAULT 0,
    PRIMARY KEY (run_id, account_id, account_type, code)
);
```

---

## 6. 关键区别：策略级 vs 账户级

| 场景 | 策略级 | 账户级 |
|------|--------|--------|
| **可用量校验** | 查 ContractStat（含 strategy_id） | 查 AccountContractStat（无 strategy_id） |
| **FIFO匹配** | 从 PositionUnit 匹配 | 从 AccountPositionUnit 匹配 |
| **平仓对象** | 只平指定策略的持仓 | 平该账户下所有策略的持仓（跨策略） |
| **影响范围** | 仅影响策略级资金 | 仅影响账户级资金 |

**关键区别**：
- 策略级平仓：**不能**跨策略平仓
- 账户级平仓：**可以**跨策略平仓（按 FIFO 不分策略）

---

## 7. 账户级平仓 FIFO 优先级规则

### 7.1 背景

当同一合约存在**普通委托**和**组合委托**两类开仓持仓时，平仓时需要区分优先顺序，以保护组合持仓的完整性。

### 7.2 优先级定义

```
优先级 1（先平）：combination_id = 0    普通单腿委托产生的持仓
优先级 2（后平）：combination_id ≠ 0   组合委托拆腿产生的持仓（已创建 CombinationUnit）

同优先级内部：按原有 FIFO 规则（open_date ASC, open_time ASC）
```

### 7.3 查询排序

`AccountPositionUnitStore::queryUnclosedByDirection` 使用以下排序，确保平仓时普通持仓优先：

```sql
SELECT * FROM account_position_unit
WHERE run_id=? AND account_id=? AND account_type=?
  AND code=? AND direction=? AND close_date=0
ORDER BY
    CASE WHEN combination_id = 0 THEN 0 ELSE 1 END ASC,
    open_date ASC,
    open_time ASC,
    id ASC;
```

### 7.4 combination_id 写入时机

- **普通委托开仓**：`AccountPositionProcessor::onOpenFill` 写入 `combination_id = 0`
- **组合委托拆腿开仓**：两腿分别 `onOpenFill` 后，`OmService::handleCombinationOrder` 再调用配对逻辑，为每对 leg1/leg2 创建 `CombinationUnit` 并回填 `combination_id = CombinationUnit.id`

---

## 8. 数据关系示例

```
同一笔开仓成交 100 手：
├─ 策略级：插入 100 条 PositionUnit（strategy_id = "StrategyA"）
└─ 账户级：插入 100 条 AccountPositionUnit（无 strategy_id）

数据特点：
- 两条记录独立，无外键关联
- 数量相同（都是 100 手）
- 字段值相同（open_price、margin、fee 等）
- 用途不同（策略风控 vs 账户风控）
```

---

## 9. AccountPositionUnitHis（账户级持仓单元历史表）

### 9.1 结构体定义

```c
typedef struct t_AccountPositionUnitHis {
    int64_t id;                         /**< 主键，自增，表示平仓顺序 */
    int64_t open_id;                    /**< 关联原 AccountPositionUnit.id */
    char run_id[LEN_ID];                /**< 实例ID */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID */
    int32_t account_type;               /**< 资金账户类型 */
    char code[LEN_CODE];                /**< 合约代码 */
    char order_id[LEN_ID];              /**< 开仓委托 ID */
    int32_t direction;                  /**< 持仓方向，取 PositionSide 枚举值 */
    int64_t hold_cost;                  /**< 持仓价格，扩大一万倍 */
    int32_t open_date;                  /**< 开仓日期 YYYYMMDD */
    int32_t open_time;                  /**< 开仓时间 HHmmSSsss(毫秒) */
    int64_t open_price;                 /**< 开仓价格，扩大一万倍 */
    char close_order_id[LEN_ID];        /**< 平仓委托 ID */
    int64_t close_price;                /**< 平仓价，扩大一万倍 */
    int32_t close_date;                 /**< 平仓日期 YYYYMMDD */
    int32_t close_time;                 /**< 平仓时间 HHmmSSsss(毫秒) */
    int64_t fee;                        /**< 开仓手续费+平仓手续费（扩大一万倍） */
    int64_t margin;                     /**< 保证金（扩大一万倍） */
    int64_t pnl;                        /**< 平仓盈亏（扩大一万倍） */
    int32_t contract_multiply;          /**< 合约乘数 */
    int64_t combination_id;             /**< 组合ID，0表示未参与组合 */
} AccountPositionUnitHis;
```

### 9.2 设计说明

**与 AccountPositionUnit 的区别**：

| 字段 | AccountPositionUnit | AccountPositionUnitHis |
|------|---------------------|------------------------|
| `id` | 自增主键，表示开仓顺序 | 自增主键，表示平仓顺序 |
| `open_id` | 无 | 关联原 AccountPositionUnit.id |

**与 PositionUnitHis 的区别**：
- 无 `strategy_id` 字段（账户级跨策略）
- 增加 `combination_id` 字段（记录组合持仓关联）

### 9.3 数据库表结构

```sql
CREATE TABLE IF NOT EXISTS account_position_unit_his (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    open_id           INTEGER NOT NULL,
    run_id            TEXT    NOT NULL,
    account_id        TEXT    NOT NULL,
    account_type      INTEGER NOT NULL,
    code              TEXT    DEFAULT '',
    order_id          TEXT    DEFAULT '',
    direction         INTEGER DEFAULT 0,
    hold_cost         INTEGER DEFAULT 0,
    open_date         INTEGER DEFAULT 0,
    open_time         INTEGER DEFAULT 0,
    open_price        INTEGER DEFAULT 0,
    close_order_id    TEXT    DEFAULT '',
    close_price       INTEGER DEFAULT 0,
    close_date        INTEGER DEFAULT 0,
    close_time        INTEGER DEFAULT 0,
    fee               INTEGER DEFAULT 0,
    margin            INTEGER DEFAULT 0,
    pnl               INTEGER DEFAULT 0,
    contract_multiply INTEGER DEFAULT 0,
    combination_id    INTEGER DEFAULT 0
);

-- 按原持仓ID查询历史
CREATE INDEX IF NOT EXISTS idx_acct_puh_open_id ON account_position_unit_his (open_id);

-- 按作用域查询历史（无 strategy_id）
CREATE INDEX IF NOT EXISTS idx_acct_puh_scope ON account_position_unit_his (run_id, account_id, account_type);
```

---

## 10. 相关文档

| 主题 | 位置 |
|------|------|
| 策略级持仓模型 | `02-domain/position-model.md` |
| 账户级资金模型 | `02-domain/account-fund.md` |
| 持仓处理流程 | `03-implementation/flows/order-flow.md` |
| Processor接口 | `03-implementation/interfaces/processor-apis.md` |
