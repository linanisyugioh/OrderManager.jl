# 账户级资金模型

> 从old_docs/account_fundtable_design.md迁移精简  
> AccountFundtable 字段定义、Delta更新机制

---

## 1. 概述

账户资金表（AccountFundtable）用于按账户维度记录资金信息，与策略级资金表（Fundtable）形成**平行关系**：

| 维度 | 表名 | 粒度 | 维护方式 |
|------|------|------|----------|
| 策略级 | Fundtable | 4维度 | 通过 delta_* 增量更新 |
| 账户级 | AccountFundtable | 3维度 | 通过 delta_acct_* 增量更新 |

**核心设计原则**：
- **独立维护**：账户级资金不从策略级聚合，独立通过 ctx 差值增量更新
- **独立计算**：delta_acct_* 必须按账户作用域重新计算，**禁止**从策略级 delta_* 直接赋值（平仓匹配作用域不同，手续费、盈亏可能不同）
- **逻辑一致**：与 Fundtable 采用相同的更新模式（apply(ctx)），仅 delta 字段前缀不同
- **实时同步**：同一笔委托产生的资金变动，同时更新策略级和账户级

---

## 2. AccountFundtable

### 2.1 结构体定义

```c
typedef struct t_AccountFundtable {
    char run_id[LEN_ID];                /**< 实例ID（主键） */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID（主键） */
    int32_t account_type;               /**< 资金账户类型（主键） */
    int32_t currency;                   /**< 货币类型 */
    int64_t account_frozen;             /**< 冻结资金（扩大一万倍） */
    int64_t fee;                        /**< 手续费（扩大一万倍） */
    int64_t bouns;                      /**< 现金红利（扩大一万倍） */
    int64_t account_pnl;                /**< 浮动盈亏（扩大一万倍） */
    int64_t account_start_cash;           /**< 当日起始资金（扩大一万倍） */
    int64_t account_cash;                 /**< 可用资金（扩大一万倍） */
    int64_t account_mincash;              /**< 当日最低可用资金（扩大一万倍） */
    int64_t account_margin;               /**< 保证金（扩大一万倍） */
    int64_t account_start_equity;         /**< 当日起始权益（扩大一万倍） */
    int64_t account_equity;               /**< 总权益（扩大一万倍） */
    int64_t account_minequity;            /**< 当日最小权益（扩大一万倍） */
} AccountFundtable;
```

### 2.2 字段说明

| 字段 | 说明 | 计算公式/来源 |
|------|------|--------------|
| account_frozen | 冻结资金 | 委托挂单的预估占用 |
| fee | 累计手续费 | 自起始资金以来的累计手续费 |
| bouns | 现金红利 | 累计收到的现金分红 |
| account_pnl | 浮动盈亏 | 未平仓头寸的浮动盈亏 |
| account_start_cash | 起始资金 | 当日初始化时的可用资金 |
| account_cash | 当前可用资金 | 已扣除手续费、含现金红利、含平仓盈亏 |
| account_mincash | 当日最低可用资金 | 用于评估资金回撤 |
| account_margin | 保证金 | 期货开仓占用保证金 |
| account_start_equity | 起始权益 | 当日初始化时的总权益 |
| account_equity | 当前总权益 | `account_margin + account_cash + account_frozen + account_pnl` |
| account_minequity | 当日最小权益 | 用于评估最大回撤 |

### 2.3 与 Fundtable 的字段映射

| Fundtable | AccountFundtable | 说明 |
|-----------|------------------|------|
| frozen_cash | account_frozen | 冻结资金 |
| avail_cash | account_cash | 可用资金 |
| margin | account_margin | 保证金 |
| pnl | account_pnl | 浮动盈亏 |
| equity | account_equity | 总权益 |
| minimum_cash | account_mincash | 最低可用 |
| minimum_equity | account_minequity | 最小权益 |

---

## 3. AccountFundtableHis

### 3.1 结构体定义

```c
typedef struct t_AccountFundtableHis {
    char run_id[LEN_ID];
    char account_id[LEN_ACCOUNT_ID];
    int32_t account_type;
    int32_t oper_date;                  /**< 快照日期（主键）YYYYMMDD */
    // ... 其他字段与 AccountFundtable 相同
} AccountFundtableHis;
```

---

## 4. OrderContext 账户级 Delta 字段

```c
typedef struct t_OrderContext {
    // ... 策略级 delta 字段 ...
    
    /* 账户级 delta 字段 */
    int64_t delta_acct_frozen;   /**< 账户级冻结资金变化（×10000） */
    int64_t delta_acct_cash;     /**< 账户级可用资金变化（×10000） */
    int64_t delta_acct_margin;   /**< 账户级保证金变化（×10000） */
    int64_t delta_acct_fee;      /**< 账户级手续费变化（×10000） */
    int64_t delta_acct_pnl;      /**< 账户级盈亏变化（×10000） */
    int64_t delta_acct_bouns;    /**< 账户级现金红利变化（×10000） */
} OrderContext;
```

### 4.1 账户级 Delta 必须独立计算（禁止直接赋值）

**账户级 delta 绝不能从策略级 delta 直接赋值**，必须由 AccountPositionProcessor 和 OrderProcessor 按账户作用域重新计算。

**原因：平仓匹配的作用域不同**

| 作用域 | 持仓来源 | 平仓匹配规则 |
|--------|----------|--------------|
| 策略级 | 仅该策略的持仓 | 按策略内今/昨仓独立匹配 |
| 账户级 | 该账户下所有策略的持仓 | 按账户内今/昨仓统一匹配 |

**示例**：账户 A 有策略 S1 和 S2，S1 有 1 手昨仓，S2 有 1 手今仓。S2 发起平仓时：

- **策略级**：S2 只有今仓，按**平今**计算 → 平今手续费、平今盈亏
- **账户级**：A 账户有 S1 的昨仓，按 LIFO 等规则可能匹配到 S1 的昨仓 → 按**平昨**计算 → 平昨手续费、平昨盈亏可能不同

因此，平仓手续费、平仓盈亏在策略级和账户级可能**数值不同**，必须分别计算。

**实现要求**：

- **持仓表**：PositionProcessor（策略级）与 AccountPositionProcessor（账户级）各自按自身作用域做平仓匹配、计算 delta
- **资金表**：delta_acct_* 由 OrderProcessor（冻结/释放）和 AccountPositionProcessor（开/平仓成交）独立写入，不能从 delta_* 赋值

---

## 5. AccountFundtableProcessor

### 5.1 核心方法

```cpp
class AccountFundtableProcessor {
public:
    int apply(const OrderContext& ctx);  // 统一应用 delta
    int updatePnl(const char* run_id, const char* account_id,
                  int32_t account_type, int64_t delta_pnl);  // 行情路径
    int createSnapshot(int32_t oper_date, bool skip_duplicate = true);
    int querySnapshot(const char* run_id, int32_t oper_date,
                      std::vector<AccountFundtableHis>& out);
    int queryEquityCurve(const char* run_id, const char* account_id,
                         int32_t start_date, int32_t end_date,
                         std::vector<AccountFundtableHis>& out);
};
```

### 5.2 apply 逻辑

```cpp
int AccountFundtableProcessor::apply(const OrderContext& ctx) {
    // 6 个 delta 全为 0 时跳过
    if (ctx.delta_acct_frozen == 0 && ctx.delta_acct_cash == 0 &&
        ctx.delta_acct_margin == 0 && ctx.delta_acct_fee   == 0 &&
        ctx.delta_acct_pnl    == 0 && ctx.delta_acct_bouns  == 0) {
        return OM_Ok;
    }
    
    // 查询当前资金记录
    AccountFundtable fund;
    int rc = store_->queryByPrimaryKey(..., &fund);
    if (rc != 0) return AccountFundtableProc_NotFound;
    
    // 累加 delta
    fund.account_frozen += ctx.delta_acct_frozen;
    fund.account_cash   += ctx.delta_acct_cash;
    fund.account_margin += ctx.delta_acct_margin;
    fund.fee            += ctx.delta_acct_fee;
    fund.account_pnl    += ctx.delta_acct_pnl;
    fund.bouns          += ctx.delta_acct_bouns;
    
    // 重算权益和最低记录
    updateMinima(fund);
    
    // 写回
    return store_->update(&fund);
}
```

### 5.3 updateMinima 逻辑

```cpp
void AccountFundtableProcessor::updateMinima(AccountFundtable& fund) {
    fund.account_equity = fund.account_margin + fund.account_cash + 
                          fund.account_frozen + fund.account_pnl;
    if (fund.account_cash < fund.account_mincash) {
        fund.account_mincash = fund.account_cash;
    }
    if (fund.account_equity < fund.account_minequity) {
        fund.account_minequity = fund.account_equity;
    }
}
```

---

## 6. 数据库表结构

### 6.1 accountfundtable 表

```sql
CREATE TABLE IF NOT EXISTS accountfundtable (
    run_id              TEXT    NOT NULL,
    account_id          TEXT    NOT NULL,
    account_type        INTEGER NOT NULL,
    currency            INTEGER DEFAULT 0,
    account_frozen      INTEGER DEFAULT 0,
    fee                 INTEGER DEFAULT 0,
    bouns               INTEGER DEFAULT 0,
    account_pnl         INTEGER DEFAULT 0,
    account_start_cash  INTEGER DEFAULT 0,
    account_cash        INTEGER DEFAULT 0,
    account_mincash     INTEGER DEFAULT 0,
    account_margin      INTEGER DEFAULT 0,
    account_start_equity INTEGER DEFAULT 0,
    account_equity      INTEGER DEFAULT 0,
    account_minequity   INTEGER DEFAULT 0,
    PRIMARY KEY (run_id, account_id, account_type)
);
```

### 6.2 accountfundtablehis 表

```sql
CREATE TABLE IF NOT EXISTS accountfundtablehis (
    run_id              TEXT    NOT NULL,
    account_id          TEXT    NOT NULL,
    account_type        INTEGER NOT NULL,
    oper_date           INTEGER NOT NULL,
    currency            INTEGER DEFAULT 0,
    account_frozen      INTEGER DEFAULT 0,
    fee                 INTEGER DEFAULT 0,
    bouns               INTEGER DEFAULT 0,
    account_pnl         INTEGER DEFAULT 0,
    account_start_cash  INTEGER DEFAULT 0,
    account_cash        INTEGER DEFAULT 0,
    account_mincash     INTEGER DEFAULT 0,
    account_margin      INTEGER DEFAULT 0,
    account_start_equity INTEGER DEFAULT 0,
    account_equity      INTEGER DEFAULT 0,
    account_minequity   INTEGER DEFAULT 0,
    PRIMARY KEY (run_id, account_id, account_type, oper_date)
);
```

---

## 7. 资金配置与校验

### 7.1 初始化流程

#### 策略级资金配置
```cpp
// 初始化单个策略的资金
Fundtable fund;
strncpy(fund.run_id, "RUN_001", sizeof(fund.run_id));
strncpy(fund.account_id, "ACC_001", sizeof(fund.account_id));
fund.account_type = AccountType_Futures;
strncpy(fund.strategy_id, "STRAT_001", sizeof(fund.strategy_id));
fund.avail_cash = 10000000000LL;  // 100万元

int rc = om_set_fund_config(&fund);
// 返回值：OM_Ok(0), FundtableStore_DupKey(-2) 等
```

#### 账户级资金配置
```cpp
// 初始化账户级资金（跨策略汇总）
AccountFundtable acct_fund;
strncpy(acct_fund.run_id, "RUN_001", sizeof(acct_fund.run_id));
strncpy(acct_fund.account_id, "ACC_001", sizeof(acct_fund.account_id));
acct_fund.account_type = AccountType_Futures;
acct_fund.account_cash = 10000000000LL;  // 应 >= 策略级资金之和

int rc = om_set_account_fund_config(&acct_fund);
// 返回值：OM_Ok(0), AccountFundtableStore_DupKey(-474) 等
```

### 7.2 资金校验约束

交易日初始化时（`om_trading_day_update`）执行强制校验：

```
account_cash   >= Σ(avail_cash)   // 账户可用 >= 各策略可用之和
account_margin >= Σ(margin)       // 账户保证金 >= 各策略保证金之和
account_equity >= Σ(equity)       // 账户权益 >= 各策略权益之和
```

| 校验项 | 错误码 | 处理 |
|--------|--------|------|
| 可用资金不足 | OM_FundCheckFailed(-13) | 事务回滚，返回错误 |
| 保证金不足 | OM_FundCheckFailed(-13) | 事务回滚，返回错误 |
| 权益不足 | OM_FundCheckFailed(-13) | 事务回滚，返回错误 |

### 7.3 配置时序要求

```
om_init()
    ↓
om_set_fund_config(fund)          // 每个策略分别配置
    ↓
om_set_account_fund_config(fund)  // 配置账户级汇总资金
    ↓
om_trading_day_update()           // 内部执行资金校验
    │  // 校验失败时返回 OM_FundCheckFailed，事务回滚
    ↓
【盘中交易】
```

## 8. 业务流程集成

### 8.1 委托处理流程

```
om_handle_order():
├─ Step 1~6: 分别计算策略级 delta_*（PositionProcessor）与账户级 delta_acct_*（AccountPositionProcessor），按各自作用域独立计算
├─ Step 7: fund_proc_->apply(ctx)          // 策略级
├─ Step 8: acct_fund_proc_->apply(ctx)     // 账户级
└─ Step 9: 写回 OrderStore
```

### 8.2 行情刷新流程

```
om_handle_newprice():
├─ Step 1~2: PositionProcessor::updateFloatingPnl  // 策略级
├─ Step 3:   FundtableProcessor::batchUpdatePnl  // 策略级（批量）
├─ Step 4:   AccountPositionProcessor::updateFloatingPnl  // 账户级
└─ Step 5:   AccountFundtableProcessor::updatePnl       // 账户级
```

### 8.3 日终结算流程

```
om_trading_day_end():
├─ Step 1: 创建资金快照
│   ├─ fundtable_snapshot_handler_->createSnapshot()    // 策略级
│   └─ account_fundtable_processor_->createSnapshot()    // 账户级 ← 新增
├─ Step 2~4: 日终结算...
```

---

## 9. 相关文档

| 主题 | 位置 |
|------|------|
| 策略级资金模型 | `02-domain/fund-model.md` |
| 账户级持仓模型 | `02-domain/account-position.md` |
| 计算公式 | `02-domain/calc-formulas.md` |
| Store接口 | `03-implementation/interfaces/store-apis.md` |
| 对外API | `03-implementation/interfaces/public-apis.md` |
| 交易日初始化流程 | `03-implementation/flows/dayinit-flow.md` |
