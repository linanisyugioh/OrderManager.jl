# 组合持仓模型

> 组合持仓（CombinationUnit）字段定义、保证金优惠计算规则

> **关联文档**：本文档描述**组合持仓**（保证金优惠机制）。如需了解**组合委托**与普通委托的区别（order_id格式、价格语义、盈亏计算等），请参考 [`combination-order.md`](combination-order.md)。

---

## 0. 当前实现范围（重要）

| 能力 | 当前实现 | 未实现 |
|------|----------|--------|
| **组合委托成交后自动配对** | ✓ 已实现 | - |
| **CombinationUnit 创建与 combination_id 回填** | ✓ 已实现 | - |
| **code 字段** | 使用组合委托的 code（如 `DCE.b2606&b2612`） | 交易所组合代码（如 `SHFE.au.spread`） |
| **margin 优惠计算** | 固定为 0 | 按交易所规则计算优惠金额 |
| **申请组合 / 拆分组合** | - | 手动申请保证金优惠、拆分后资金更新 |
| **资金表 margin 联动** | - | 申请/拆分时 account_margin、avail_cash 变更 |

§6 保证金计算流程描述的是**完整业务模型**，供后续扩展参考；当前代码仅实现「自动配对」，不涉及 margin 优惠与资金更新。

---

## 1. 概述

### 1.1 什么是组合持仓

组合持仓是**期货交易所提供的保证金优惠机制**，通过将特定的多头和空头持仓进行组合，可以申请保证金减免。

**典型应用场景**：
- 跨期套利：同一品种不同月份的正向/反向持仓
- 跨品种套利：相关品种间的对冲持仓
- 期权期货组合：期货与期权的对冲组合

### 1.2 组合持仓的特点

| 特点 | 说明 |
|------|------|
| **账户级** | 组合只在账户级别存在，不涉及策略级 |
| **两手组合** | 一个组合由两个持仓单元（A和B）组成 |
| **保证金优惠** | 组合后实际保证金 = 两持仓保证金 - 优惠金额 |
| **可拆分** | 组合可以解除，恢复为独立持仓 |

### 1.3 与策略级持仓的关系

```
┌─────────────────────────────────────────────────────────┐
│  策略级持仓（PositionUnit）                                │
│  ├─ Strategy A: 5手 au2506 多头                           │
│  └─ Strategy B: 5手 au2506 空头                           │
└─────────────────────────────────────────────────────────┘
                           ↓ 聚合到账户级
┌─────────────────────────────────────────────────────────┐
│  账户级持仓（AccountPositionUnit）                        │
│  ├─ 5手 au2506 多头（来自Strategy A）                      │
│  └─ 5手 au2506 空头（来自Strategy B）                    │
└─────────────────────────────────────────────────────────┘
                           ↓ 申请组合
┌─────────────────────────────────────────────────────────┐
│  组合持仓（CombinationUnit）                               │
│  ├─ 组合合约代码: SHFE.au2506.spread                      │
│  ├─ 持仓单元A ID: [多头持仓ID]                           │
│  ├─ 持仓单元B ID: [空头持仓ID]                           │
│  └─ 优惠保证金: 500000（扩大一万倍）                      │
└─────────────────────────────────────────────────────────┘
```

---

## 2. CombinationUnit 结构体定义

```c
/** 组合持仓单元，用于组合持仓的查询和统计，组合只在账户级拥有 */
typedef struct t_CombinationUnit {
    int64_t id;                         /**< 主键. 组合ID，与 combination_unit 表 id 一致；插入时 0，batchAdd 后回填 */
    char run_id[LEN_ID];                /**< 实例ID（主键） */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID（主键） */
    int32_t account_type;               /**< 资金账户类型（主键） */
    char order_id[LEN_ID];              /**< 组合委托ID */
    char code[LEN_CODE];                /**< 组合合约代码 */
    int32_t side;                       /**< 组合方向，1=多头，2=空头，参考 PositionSide 枚举 */
    int64_t position_unit_id_a;         /**< 持仓单元A ID */
    int64_t position_unit_id_b;         /**< 持仓单元B ID */
    int64_t margin;                     /**< 组合优惠保证金（扩大一万倍），即优惠了多少，实际保证金=两个持仓的保证金-本优惠保证金 */
    int32_t existed_flag;               /**< 是否存在，0=不存在，1=存在，用于表示组合是否还存在 */
    int64_t create_time;                /**< 创建时间，精确到毫秒，格式YYYYMMDDHHmmSSsss */
    int64_t break_time;                 /**< 拆分时间，精确到毫秒，格式YYYYMMDDHHmmSSsss */
} CombinationUnit;
```

---

## 3. 字段详细说明

### 3.1 主键字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| id | int64 | ✓ | 自增主键，数据库回填 |
| run_id | char[64] | ✓ | 实例ID，3维度作用域（无strategy_id） |
| account_id | char[64] | ✓ | 账户ID |
| account_type | int32 | ✓ | 账户类型 |

**注意**：组合持仓是**账户级**概念，没有 `strategy_id` 维度。

### 3.2 业务字段

| 字段 | 类型 | 说明 |
|------|------|------|
| order_id | char[64] | 申请组合的委托ID，关联order表 |
| code | char[32] | 组合合约代码；当前实现使用组合委托的 code（如 `DCE.b2606&b2612`）；交易所组合代码（如 `SHFE.au.spread`）待扩展 |
| side | int32 | 组合方向：1=多头组合，2=空头组合 |
| position_unit_id_a | int64 | 持仓单元A的ID（`AccountPositionUnit.id`） |
| position_unit_id_b | int64 | 持仓单元B的ID（`AccountPositionUnit.id`） |

### 3.3 保证金字段

| 字段 | 类型 | 说明 |
|------|------|------|
| margin | int64 | 组合**优惠**保证金（扩大一万倍），即节省的保证金金额 |

**实际保证金计算公式**：
```
持仓A保证金 = 价格A × 乘数 × 保证金率A
持仓B保证金 = 价格B × 乘数 × 保证金率B
优惠前总保证金 = 持仓A保证金 + 持仓B保证金
实际保证金 = 优惠前总保证金 - margin（优惠金额）
```

### 3.4 状态字段

| 字段 | 类型 | 说明 |
|------|------|------|
| existed_flag | int32 | 组合是否存在：0=已拆分不存在，1=有效存在 |
| create_time | int64 | 组合创建时间，格式 YYYYMMDDHHmmSSsss |
| break_time | int64 | 组合拆分时间，格式 YYYYMMDDHHmmSSsss；未拆分时为0 |

**状态流转**：
```
申请组合 → create_time 填充, existed_flag=1
    ↓
组合生效 → 保证金按优惠后计算
    ↓
拆分组合 → break_time 填充, existed_flag=0
```

---

## 4. 数据库表结构

### 4.1 combination_unit 表

```sql
-- 与 data/combination_unit_store.cc createTable 一致；当前实现未建外键
CREATE TABLE IF NOT EXISTS combination_unit (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id              TEXT    NOT NULL,
    account_id          TEXT    NOT NULL,
    account_type        INTEGER NOT NULL,
    order_id            TEXT    NOT NULL,
    code                TEXT    NOT NULL,
    side                INTEGER DEFAULT 0,
    position_unit_id_a  INTEGER NOT NULL,
    position_unit_id_b  INTEGER NOT NULL,
    margin              INTEGER DEFAULT 0,
    existed_flag        INTEGER DEFAULT 1,
    create_time         INTEGER DEFAULT 0,
    break_time          INTEGER DEFAULT 0
);

-- 账户级查询索引
CREATE INDEX idx_comb_scope
    ON combination_unit (run_id, account_id, account_type);

CREATE INDEX idx_comb_order_id
    ON combination_unit (order_id);

-- 持仓单元反向查询索引
CREATE INDEX idx_comb_pos_a
    ON combination_unit (position_unit_id_a);

CREATE INDEX idx_comb_pos_b
    ON combination_unit (position_unit_id_b);
```

### 4.2 combination_unit_his 表（历史表）

日终结算时将 `existed_flag=0`（已拆分）的组合记录移动到历史表，保留历史记录。

```sql
-- 与 data/combination_unit_his_store.cc createTable 一致
CREATE TABLE IF NOT EXISTS combination_unit_his (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    combination_id      INTEGER NOT NULL,      -- 原组合ID
    run_id              TEXT    NOT NULL,
    account_id          TEXT    NOT NULL,
    account_type        INTEGER NOT NULL,
    order_id            TEXT    NOT NULL,
    code                TEXT    NOT NULL,
    side                INTEGER DEFAULT 0,
    position_unit_id_a  INTEGER NOT NULL,
    position_unit_id_b  INTEGER NOT NULL,
    margin              INTEGER DEFAULT 0,
    existed_flag        INTEGER DEFAULT 0,     -- 保存原值
    create_time         INTEGER DEFAULT 0,
    break_time          INTEGER DEFAULT 0,
    oper_date           INTEGER NOT NULL        -- 结算日期
);

-- 按作用域和日期查询索引
CREATE INDEX idx_comb_his_scope_date
    ON combination_unit_his (run_id, account_id, account_type, oper_date);

CREATE INDEX idx_comb_his_date
    ON combination_unit_his (oper_date);
```

**与主表区别**：
- 增加 `combination_id` 字段保存原 `id`
- 增加 `oper_date` 字段表示结算日期
- 无主键冲突风险（自增id）

**生命周期**：
- 日终结算时：查询 `existed_flag=0` 记录 → 插入历史表
- 日初始化时：删除 `existed_flag=0` 记录（已备份到历史表）

---

## 5. 关联关系

### 5.1 与 AccountPositionUnit 的关系

```
CombinationUnit.position_unit_id_a ──→ AccountPositionUnit.id
CombinationUnit.position_unit_id_b ──→ AccountPositionUnit.id

AccountPositionUnit.combination_id ←── 被组合时填充
```

### 5.2 关系约束

1. **组合创建时**：
   - 两个持仓单元必须属于同一账户（run_id + account_id + account_type）
   - 两个持仓单元方向必须相反（一个多头，一个空头）
   - 两个持仓单元必须未参与其他组合（combination_id = 0）
   - 填充 `AccountPositionUnit.combination_id` 为组合ID

2. **组合拆分时**：
   - `existed_flag` 置为 0
   - `break_time` 填充当前时间
   - 清空两个持仓单元的 `combination_id` 字段

### 5.3 平仓打破组合机制

**触发条件**：账户级平仓成交时，若匹配到的 `AccountPositionUnit` 的 `combination_id ≠ 0`，则触发组合打破。

**处理流程**：

```
1. 根据 combination_id 查询 CombinationUnit
2. 标记该 Unit：existed_flag = 0, break_time = 当前时间
3. 确定被平仓的是哪条腿（A 或 B）
4. 将另一腿的 AccountPositionUnit.combination_id 重置为 0（释放为普通持仓）
```

**关键设计**：

- **一手对一手**：每个 `CombinationUnit` 只管理一对持仓（position_unit_id_a + position_unit_id_b）
- **独立打破**：开仓 N 手产生 N 个独立的 `CombinationUnit`，平仓时只打破被平掉的那个持仓对
- **不影响其他**：其他持仓对的组合状态保持不变

**示例**：

```
组合委托开仓 3 手：
- 产生 6 个 AccountPositionUnit（3 手腿1 + 3 手腿2）
- 产生 3 个 CombinationUnit（每手对一手）
  ├─ Unit 101: (APU id=1, APU id=2)
  ├─ Unit 102: (APU id=3, APU id=4)
  └─ Unit 103: (APU id=5, APU id=6)

平仓 1 手腿1（平掉了 id=3 的持仓）：
- Unit 102 被打破：existed_flag=0
- id=4 的另一腿释放：combination_id=0
- Unit 101 和 103 保持不变
```

---

## 6. 保证金计算流程

### 6.1 组合申请时

```
1. 计算持仓A的原始保证金
2. 计算持仓B的原始保证金
3. 根据交易所规则计算优惠金额（margin字段）
4. 更新账户级资金：
   - account_margin 减少 = 优惠金额
   - avail_cash 增加 = 优惠金额
```

### 6.2 组合生效后持仓处理

```
对于已组合的持仓单元：
- 保证金按优惠后计算
- 浮动盈亏仍按原始持仓计算
- 日终结算时按优惠后保证金重算
```

### 6.3 组合拆分时

```
1. existed_flag = 0
2. break_time = 当前时间
3. 恢复原始保证金计算
4. 更新账户级资金：
   - account_margin 增加 = 优惠金额
   - avail_cash 减少 = 优惠金额
```

---

## 7. 相关文档

| 主题 | 位置 |
|------|------|
| 组合委托定义 | `02-domain/combination-order.md` |
| 账户级持仓模型 | `02-domain/account-position.md` |
| 资金计算公式 | `02-domain/calc-formulas.md` |
| 对外API接口 | `03-implementation/interfaces/public-apis.md` |
