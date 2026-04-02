# 持仓模型定义

> PositionUnit 和 ContractStat 字段定义、FIFO管理规则

---

## 1. PositionUnit（持仓单元）

每手持仓一条记录，精确追踪开平仓信息。

### 1.1 结构体定义

```c
typedef struct t_PositionUnit {
    int64_t id;                         /**< 主键，自增 */
    char run_id[LEN_ID];                /**< 实例ID */
    char account_id[LEN_ACCOUNT_ID];    /**< 账户ID */
    int32_t account_type;               /**< 账户类型 */
    char strategy_id[LEN_CODE];         /**< 策略ID */
    char code[LEN_CODE];                /**< 合约代码 */
    char order_id[LEN_ID];              /**< 开仓委托ID */
    int32_t direction;                  /**< 持仓方向（PositionSide） */
    int64_t hold_cost;                  /**< 持仓成本价（扩大一万倍） */
    int32_t open_date;                  /**< 开仓日期 YYYYMMDD */
    int32_t open_time;                  /**< 开仓时间 HHMMSSmmm */
    int64_t open_price;                 /**< 开仓成交价（扩大一万倍） */
    char close_order_id[LEN_ID];        /**< 平仓委托ID（未平仓为空） */
    int64_t close_price;                /**< 平仓价（扩大一万倍，未平仓为0） */
    int32_t close_date;                 /**< 平仓日期（未平仓为0） */
    int32_t close_time;                 /**< 平仓时间（未平仓为0） */
    int64_t fee;                        /**< 开仓+平仓手续费（扩大一万倍） */
    int64_t margin;                     /**< 保证金（扩大一万倍） */
    int64_t pnl;                        /**< 浮动盈亏（扩大一万倍） */
    int32_t contract_multiply;          /**< 合约乘数 */
} PositionUnit;
```

### 1.2 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| id | int64 | 自增主键，开仓插入后回填 |
| run_id/account_id/account_type/strategy_id | - | 作用域4维度 |
| code | char[32] | 合约代码，用于按合约查询 |
| order_id | char[64] | 开仓委托ID，关联order表 |
| direction | int32 | PositionSide_Long(1) 或 PositionSide_Short(2) |
| hold_cost | int64 | 持仓成本价，今仓=开仓价，昨仓=前日结算价 |
| open_date | int32 | 开仓日期，用于区分今仓/昨仓 |
| open_time | int32 | 开仓时间，FIFO排序依据 |
| open_price | int64 | 开仓成交价，保持不变 |
| close_* | - | 平仓时回填，未平仓时均为0/空 |
| fee | int64 | 开仓费+平仓费合计 |
| margin | int64 | 单手保证金 |
| pnl | int64 | 浮动盈亏（扩大一万倍）。**开仓时初始值**：若成交价与缓存最新价不同，按价差计算初始盈亏；否则为0。平仓时回填实现盈亏 |
| contract_multiply | int32 | 合约乘数，计算盈亏用 |

### 1.3 今仓/昨仓判断

```cpp
bool isTodayPosition(const PositionUnit& unit, int32_t trading_date) {
    return unit.open_date == trading_date;
}

bool isYesterdayPosition(const PositionUnit& unit, int32_t trading_date) {
    return unit.open_date < trading_date;
}
```

---

## 2. ContractStat（合约统计）

每个作用域 + 合约一条记录，维护持仓量统计和冻结量。

### 2.1 结构体定义

```c
typedef struct t_ContractStat {
    char run_id[LEN_ID];                /**< 实例ID */
    char account_id[LEN_ACCOUNT_ID];    /**< 账户ID */
    int32_t account_type;               /**< 账户类型 */
    char strategy_id[LEN_CODE];         /**< 策略ID */
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
} ContractStat;
```

### 2.2 字段说明

| 字段 | 说明 |
|------|------|
| volume 字段 | 持仓量（正数） |
| frozen 字段 | 已挂平仓单但未成交的量 |
| 可用量 | available = volume - frozen |

### 2.3 可用量计算

根据平仓方向计算可用持仓：

```cpp
// 平今仓
if (side == Today_Long_Close)
    available = today_long_volume - today_long_frozen;
if (side == Today_Short_Close)
    available = today_short_volume - today_short_frozen;

// 平昨仓
if (side == PreDay_Long_Close)
    available = yesterday_long_volume - yesterday_long_frozen;
if (side == PreDay_Short_Close)
    available = yesterday_short_volume - yesterday_short_frozen;

// 不区分今昨
if (side == Long_Close)
    available = (today_long + yesterday_long) - (today_long_frozen + yesterday_long_frozen);
if (side == Short_Close)
    available = (today_short + yesterday_short) - (today_short_frozen + yesterday_short_frozen);
```

---

## 3. FIFO 平仓匹配规则

### 3.1 三阶段策略

| 阶段 | 方法 | 策略 | 操作对象 |
|------|------|------|----------|
| 冻结 | onCloseOrderNew | **FIFO**（先进先出） | ContractStat.frozen |
| 成交 | onCloseFill | **FIFO**（先进先出） | PositionUnit |
| 释放 | onCloseOrderCancel | **LIFO**（后进先出） | ContractStat.frozen |

### 3.2 冻结规则（优先冻结昨仓）

```cpp
// Long_Close: 优先冻结昨仓，不足时冻结今仓
if (volume <= yesterday_avail) {
    yesterday_long_frozen += volume;
} else {
    yesterday_long_frozen += yesterday_avail;
    today_long_frozen += (volume - yesterday_avail);
}

// Short_Close: 同上
```

### 3.3 释放规则（优先释放今仓）

```cpp
// Long_Close 撤单: 优先释放今仓，不足时释放昨仓
if (cancel_volume <= today_frozen) {
    today_long_frozen -= cancel_volume;
} else {
    today_long_frozen -= today_frozen;
    yesterday_long_frozen -= (cancel_volume - today_frozen);
}
```

### 3.4 成交匹配规则

根据平仓类型过滤 PositionUnit：

| 平仓类型 | 匹配范围 | 排序 |
|----------|----------|------|
| Today_Long_Close | open_date == trading_date 的多头 | open_date ASC, open_time ASC, id ASC |
| PreDay_Long_Close | open_date < trading_date 的多头 | 同上 |
| Long_Close | 全部多头（今+昨混合） | 同上 |

---

## 4. 数据库表结构

### 4.1 position_unit 表

```sql
CREATE TABLE IF NOT EXISTS position_unit (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id          TEXT    NOT NULL,
    account_id      TEXT    NOT NULL,
    account_type    INTEGER NOT NULL,
    strategy_id     TEXT    NOT NULL,
    code            TEXT    DEFAULT '',
    order_id        TEXT    DEFAULT '',
    direction       INTEGER DEFAULT 0,
    hold_cost       INTEGER DEFAULT 0,
    open_date       INTEGER DEFAULT 0,
    open_time       INTEGER DEFAULT 0,
    open_price      INTEGER DEFAULT 0,
    close_order_id  TEXT    DEFAULT '',
    close_price     INTEGER DEFAULT 0,
    close_date      INTEGER DEFAULT 0,
    close_time      INTEGER DEFAULT 0,
    fee             INTEGER DEFAULT 0,
    margin          INTEGER DEFAULT 0,
    pnl             INTEGER DEFAULT 0,
    contract_multiply INTEGER DEFAULT 0
);

-- FIFO查询索引
CREATE INDEX IF NOT EXISTS idx_pu_scope_dir_closedate
    ON position_unit (run_id, account_id, account_type, strategy_id, direction, close_date);

-- 按合约查询索引
CREATE INDEX IF NOT EXISTS idx_pu_scope_code_dir_closedate
    ON position_unit (run_id, account_id, account_type, strategy_id, code, direction, close_date);

-- 跨作用域行情查询索引
CREATE INDEX IF NOT EXISTS idx_pu_code_closedate
    ON position_unit (code, close_date);
```

### 4.2 contract_stat 表

```sql
CREATE TABLE IF NOT EXISTS contract_stat (
    run_id                TEXT    NOT NULL,
    account_id            TEXT    NOT NULL,
    account_type          INTEGER NOT NULL,
    strategy_id           TEXT    NOT NULL,
    code                  TEXT    NOT NULL,
    today_long_volume     INTEGER DEFAULT 0,
    today_long_frozen     INTEGER DEFAULT 0,
    yesterday_long_volume INTEGER DEFAULT 0,
    yesterday_long_frozen INTEGER DEFAULT 0,
    today_short_volume    INTEGER DEFAULT 0,
    today_short_frozen    INTEGER DEFAULT 0,
    yesterday_short_volume INTEGER DEFAULT 0,
    yesterday_short_frozen INTEGER DEFAULT 0,
    PRIMARY KEY (run_id, account_id, account_type, strategy_id, code)
);
```

---

## 5. PositionUnitHis（持仓单元历史表）

### 5.1 结构体定义

```c
typedef struct t_PositionUnitHis {
    int64_t id;                         /**< 主键，自增，表示平仓顺序 */
    int64_t open_id;                    /**< 关联原 PositionUnit.id */
    char run_id[LEN_ID];                /**< 实例ID */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID */
    int32_t account_type;               /**< 资金账户类型 */
    char strategy_id[LEN_CODE];         /**< 策略ID */
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
} PositionUnitHis;
```

### 5.2 设计说明

**与 PositionUnit 的区别**：

| 字段 | PositionUnit | PositionUnitHis |
|------|-------------|-----------------|
| `id` | 自增主键，表示开仓顺序 | 自增主键，表示平仓顺序 |
| `open_id` | 无 | 关联原 PositionUnit.id |

**使用场景**：
- 每次平仓时，将被平的持仓完整信息写入 `position_unit_his` 表
- 日终结算时，`position_unit` 表的已平仓记录会被清理，但历史表数据永久保留
- 支持按原持仓ID或作用域查询历史平仓记录

### 5.3 数据库表结构

```sql
CREATE TABLE IF NOT EXISTS position_unit_his (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    open_id           INTEGER NOT NULL,
    run_id            TEXT    NOT NULL,
    account_id        TEXT    NOT NULL,
    account_type      INTEGER NOT NULL,
    strategy_id       TEXT    NOT NULL,
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
    contract_multiply INTEGER DEFAULT 0
);

-- 按原持仓ID查询历史
CREATE INDEX IF NOT EXISTS idx_puh_open_id ON position_unit_his (open_id);

-- 按作用域查询历史
CREATE INDEX IF NOT EXISTS idx_puh_scope ON position_unit_his (run_id, account_id, account_type, strategy_id);
```

---

## 6. 相关文档

| 主题 | 位置 |
|------|------|
| 持仓处理流程 | `03-implementation/flows/order-flow.md` |
| Processor接口 | `03-implementation/interfaces/processor-apis.md` |
| core模块职责 | `01-architecture/module-core.md` |
| 计算公式 | `02-domain/calc-formulas.md` |
| Store接口 | `03-implementation/interfaces/store-apis.md` |
| 账户级持仓 | `02-domain/account-position.md` |
| 枚举速查 | `00-overview/quick-reference.md` |
| 快速导航 | `00-overview/navigation.md` |
