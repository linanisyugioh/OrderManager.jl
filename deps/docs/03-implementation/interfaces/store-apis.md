# 接口：Store 层 API 汇总

> 从 old_docs/modules/mod_data.md §5.x 迁移  
> data 层所有 Store 类的接口定义

---

## 1. OrderStore

```cpp
class OrderStore {
public:
    explicit OrderStore(sqlite3* db);
    ~OrderStore();

    int init();       // 预编译 SQL（createTable 后调用）
    void cleanup();   // 释放预编译语句

    int createTable();
    int upsert(const OmOrder* order);
    int queryByOrderId(const char* order_id, int32_t oper_date,
                        const char* strategy_id, const char* run_id,
                        const char* account_id, int32_t account_type,
                        OmOrder* out);
    int queryAll(std::vector<OmOrder>& out);
    int queryByScope(const char* run_id, const char* account_id,
                     int32_t account_type, const char* strategy_id,
                     std::vector<OmOrder>& out);
    int deleteAll();
};
```

| 方法 | 功能 | 参数 | 返回值 |
|------|------|------|--------|
| `init` | 预编译所有 SQL 语句 | createTable 后调用 | 0 成功；OrderStore_SqlError 失败 |
| `cleanup` | 释放预编译语句 | - | - |
| `createTable` | 创建 order 表 | - | 0 成功 |
| `upsert` | 插入或更新委托 | order: 非空 | 0 成功 |
| `queryByOrderId` | 按6字段主键查询 | 6主键字段 | 0 成功；OrderStore_NotFound(-403) 未找到 |
| `queryAll` | 查询全部委托 | out: 结果追加 | 0 成功；日终快照用 |
| `queryByScope` | 按作用域查询 | run_id, account_id, account_type, strategy_id | 0 成功；strategy_id 为空时查该账户下所有策略 |
| `deleteAll` | 清空当日委托 | - | 0 成功 |

---

## 1.1 OrderHisStore

```cpp
class OrderHisStore {
public:
    explicit OrderHisStore(sqlite3* db);
    
    int createTable();
    int batchInsert(const OmOrder* orders, int count);
    int queryByScopeAndDate(const char* run_id, const char* account_id,
                            int32_t account_type, const char* strategy_id,
                            int32_t oper_date, std::vector<OmOrder>& out);
    int queryByDateRange(const char* run_id,
                         const char* account_id, const char* strategy_id,
                         int32_t start_date, int32_t end_date,
                         std::vector<OmOrder>& out);
    int existsByDate(const char* run_id, int32_t oper_date);
};
```

| 方法 | 功能 | 说明 |
|------|------|------|
| `createTable` | 创建 order_his 表及索引 | 含 scope+date 和 date 索引 |
| `batchInsert` | 批量插入历史记录 | 日终快照用，使用 INSERT OR REPLACE |
| `queryByScopeAndDate` | 按作用域+日期查询 | 查询某策略某日的全部委托 |
| `queryByDateRange` | 按日期范围查询 | 支持可选的 account_id/strategy_id 过滤 |
| `existsByDate` | 检查某日期是否已快照 | 幂等控制 |

**错误码**（-560 ~ -569）：OrderHisStore_InvalidArg(-561)、OrderHisStore_SqlError(-562)

---

## 2. TradeStore

```cpp
class TradeStore {
public:
    explicit TradeStore(sqlite3* db);
    
    int createTable();
    int insert(const OmTrade* trade);             // 插入成交记录
    
    // 按7字段主键查询单条成交
    int queryByPrimaryKey(const char* order_id, int32_t trade_date,
                          const char* strategy_id, const char* run_id,
                          const char* account_id, int32_t account_type,
                          const char* match_seqno, OmTrade* out);
    
    // 按作用域+order_id查询该委托的所有成交（避免查到其他作用域相同order_id的成交）
    int queryByOrderIdAndScope(const char* order_id,
                               const char* run_id, const char* account_id,
                               int32_t account_type, const char* strategy_id,
                               std::vector<OmTrade>& out);
    
    // 按作用域查询所有成交
    int queryByScope(const char* run_id, const char* account_id,
                     int32_t account_type, const char* strategy_id,
                     std::vector<OmTrade>& out);
    
    // 按作用域+合约查询成交
    int queryByScopeAndCode(const char* run_id, const char* account_id,
                            int32_t account_type, const char* strategy_id,
                            const char* code, std::vector<OmTrade>& out);
    
    int deleteByDate(int32_t trade_date);       // 按日期删除（交易日初始化）
    int deleteAll();                              // 清空全部
};
```

| 方法 | 功能 | 参数 | 返回值 |
|------|------|------|--------|
| `createTable` | 创建 trade 表 | - | 0 成功 |
| `insert` | 插入成交记录 | trade: 非空 | 0 成功；TradeStore_DupKey 主键重复 |
| `queryByPrimaryKey` | 按7字段主键查询 | 7主键字段 | 0 成功；TradeStore_NotFound 未找到 |
| `queryByOrderIdAndScope` | 按作用域+order_id查询 | 4字段作用域 + order_id | 0 成功；无记录返回空vector |
| `queryByScope` | 按作用域查询全部 | 4字段作用域 | 0 成功；无记录返回空vector |
| `queryByScopeAndCode` | 按作用域+合约查询 | 4字段作用域 + code | 0 成功；无记录返回空vector |
| `deleteByDate` | 按日期删除 | trade_date: 日期 | 0 成功 |
| `deleteAll` | 清空全部 | - | 0 成功 |

**重要约束**：
- 所有查询方法必须包含4字段作用域（run_id + account_id + account_type + strategy_id），防止跨作用域数据混淆
- 当前版本Trade仅作为记录存储，不参与持仓和资金计算

---

## 2.1 TradeHisStore

```cpp
class TradeHisStore {
public:
    explicit TradeHisStore(sqlite3* db);
    
    int createTable();
    int batchInsert(const OmTrade* trades, int count);
    int queryByScopeAndDate(const char* run_id, const char* account_id,
                            int32_t account_type, const char* strategy_id,
                            int32_t trade_date, std::vector<OmTrade>& out);
    int queryByDateRange(const char* run_id,
                         const char* account_id, const char* strategy_id,
                         int32_t start_date, int32_t end_date,
                         std::vector<OmTrade>& out);
    int existsByDate(const char* run_id, int32_t trade_date);
};
```

| 方法 | 功能 | 说明 |
|------|------|------|
| `createTable` | 创建 trade_his 表及索引 | 含 scope+date 和 date 索引 |
| `batchInsert` | 批量插入历史记录 | 日终快照用，使用 INSERT OR REPLACE |
| `queryByScopeAndDate` | 按作用域+日期查询 | 查询某策略某日的全部成交 |
| `queryByDateRange` | 按日期范围查询 | 支持可选的 account_id/strategy_id 过滤 |
| `existsByDate` | 检查某日期是否已快照 | 幂等控制 |

**错误码**（-570 ~ -579）：TradeHisStore_InvalidArg(-571)、TradeHisStore_SqlError(-572)

---

## 4. PositionUnitStore

```cpp
class PositionUnitStore {
public:
    explicit PositionUnitStore(sqlite3* db);
    
    int createTable();
    int batchInsert(PositionUnit* units, int count);
    
    // 查询方法 - 使用 vector 输出
    int queryUnclosedByDirection(const char* run_id, const char* account_id,
                                  int32_t account_type, const char* strategy_id,
                                  const char* code, int32_t direction,
                                  std::vector<PositionUnit>& out);
    int queryAllUnclosedByScope(const char* run_id, const char* account_id,
                                 int32_t account_type, const char* strategy_id,
                                 std::vector<PositionUnit>& out);
    int queryAllUnclosedByCode(const char* code,
                                std::vector<PositionUnit>& out);
    int queryAllUnclosed(std::vector<PositionUnit>& out);
    
    // 更新方法（单条平仓方法名为 close，与头文件一致）
    int close(int64_t id, const char* close_order_id,
              int64_t close_price, int32_t close_date,
              int32_t close_time, int64_t fee, int64_t pnl);
    int batchUpdateClose(const char* close_order_id,
                         int64_t close_price, int32_t close_date,
                         int32_t close_time,
                         const PositionCloseParam* params, int count);
    int batchUpdatePnl(const PositionUnit* units, int count);  // 实现仅使用 id、pnl 字段，与 PositionPnlParam 语义等价
    int updateHoldCostAndMargin(int64_t id, int64_t hold_cost, 
                                int64_t margin, int64_t pnl);
    int deleteClosedUnits();
};
```

| 方法 | 功能 | 关键参数 |
|------|------|----------|
| `batchInsert` | 批量插入持仓 | units: 数组，count: 数量；回填 id |
| `queryUnclosedByDirection` | FIFO平仓查询 | direction + code；按 open_date/time/id 排序 |
| `queryAllUnclosedByCode` | 行情刷新查询 | code；跨 scope |
| `queryAllUnclosedByScope` | 日终结算查询 | 4字段 scope；不分 direction |
| `queryAllUnclosed` | 重建 ContractStat | 跨所有 scope |
| `close` | 单条平仓 | 按 id 回填平仓字段（与 updateClose 同义，头文件命名为 close） |
| `batchUpdateClose` | 批量平仓 | params: id/fee/pnl 数组；单事务 |
| `batchUpdatePnl` | 批量更新盈亏 | 实现为 PositionUnit*（仅用 id、pnl）；单事务 |
| `updateHoldCostAndMargin` | 日终结算更新 | hold_cost, margin, pnl |
| `deleteClosedUnits` | 删除已平仓 | close_date > 0 的记录 |

---

## 4.1 PositionUnitHisStore

```cpp
class PositionUnitHisStore {
public:
    explicit PositionUnitHisStore(sqlite3* db);
    
    int createTable();
    int batchInsert(const PositionUnitHis* records, int count);
    int queryByOpenId(int64_t open_id, std::vector<PositionUnitHis>& out);
    int queryByScope(const char* run_id, const char* account_id,
                     int32_t account_type, const char* strategy_id,
                     std::vector<PositionUnitHis>& out);
};
```

| 方法 | 功能 | 说明 |
|------|------|------|
| `createTable` | 创建 position_unit_his 表及索引 | 含 open_id 和 scope 索引 |
| `batchInsert` | 批量插入历史记录 | 平仓时写入 |
| `queryByOpenId` | 按原持仓ID查询 | 查询某持仓的所有平仓历史 |
| `queryByScope` | 按作用域查询 | 查询某策略的所有平仓记录 |

**错误码**（-540 ~ -549）：PositionUnitHisStore_InvalidArg(-541)、PositionUnitHisStore_SqlError(-542)

---

## 5. ContractStatStore

```cpp
class ContractStatStore {
public:
    explicit ContractStatStore(sqlite3* db);
    
    int createTable();
    int upsert(const ContractStat* stat);
    int queryByScope(const char* run_id, const char* account_id,
                     int32_t account_type, const char* strategy_id,
                     const char* code, ContractStat* out);
    int deleteAll();
    
    // delta 方式更新
    int updateVolume(const char* run_id, const char* account_id,
                      int32_t account_type, const char* strategy_id,
                      const char* code,
                      int32_t today_long_delta, int32_t yesterday_long_delta,
                      int32_t today_short_delta, int32_t yesterday_short_delta);
    int updateFrozen(const char* run_id, const char* account_id,
                      int32_t account_type, const char* strategy_id,
                      const char* code,
                      int32_t today_long_frozen_delta, int32_t yesterday_long_frozen_delta,
                      int32_t today_short_frozen_delta, int32_t yesterday_short_frozen_delta);
};
```

| 方法 | 功能 | 说明 |
|------|------|------|
| `upsert` | 插入或覆盖统计记录 | 用于重建 ContractStat |
| `queryByScope` | 查询统计记录 | 0 成功；ContractStatStore_NotFound(-423) 未找到；返回全部8个 volume/frozen 字段 |
| `updateVolume` | 原子增减持仓量 | delta 为正=开仓，为负=平仓；自动建记录 |
| `updateFrozen` | 原子增减冻结量 | delta 为正=冻结，为负=释放 |

---

## 6. FundtableStore

```cpp
class FundtableStore {
public:
    explicit FundtableStore(sqlite3* db);
    
    int createTable();
    int insert(const Fundtable& fund);
    int queryByScope(const char* run_id, const char* account_id,
                     int32_t account_type, const char* strategy_id,
                     Fundtable* out);
    int update(const Fundtable* fund);
    int existsByScope(const char* run_id, const char* account_id,
                      int32_t account_type, const char* strategy_id);
    int queryAll(std::vector<Fundtable>& out);                    // 日终结算等遍历全部记录
    int batchUpdatePnl(const std::vector<FundPnlDelta>& deltas);  // 行情路径批量更新 pnl/equity
};
```

| 方法 | 功能 | 说明 |
|------|------|------|
| `insert` | 插入资金记录 | 前置条件：记录不存在 |
| `queryByScope` | 查询资金记录 | 按4字段主键 |
| `update` | 全字段更新 | 主键必须存在 |
| `existsByScope` | 检查存在性 | 1 存在，0 不存在 |
| `queryAll` | 查询全部资金记录 | 日终结算遍历使用 |
| `batchUpdatePnl` | 批量更新多作用域 pnl/equity | 行情刷新性能优化，见 FundPnlDelta |

---

## 7. FundtableHisStore

```cpp
class FundtableHisStore {
public:
    explicit FundtableHisStore(sqlite3* db);
    
    int createTable();
    int insert(const FundtableHis& his);
    int batchInsert(const std::vector<FundtableHis>& records);
    int queryByPrimaryKey(const char* run_id, const char* account_id,
                          int32_t account_type, const char* strategy_id,
                          int32_t oper_date, FundtableHis* out);
    int queryByDateRange(const char* run_id,
                          const char* account_id,
                          const char* strategy_id,
                          int32_t start_date, int32_t end_date,
                          std::vector<FundtableHis>& out);
    int exists(const char* run_id, const char* account_id,
               int32_t account_type, const char* strategy_id,
               int32_t oper_date);
};
```

| 方法 | 功能 | 说明 |
|------|------|------|
| `insert` | 插入单条历史记录 | oper_date 必须已填充 |
| `batchInsert` | 批量插入 | 事务外包裹 |
| `queryByDateRange` | 日期范围查询 | 用于权益曲线查询 |
| `exists` | 检查快照是否存在 | 幂等控制 |

---

## 8. 账户级 Store

### 8.1 AccountFundtableStore

与 FundtableStore 接口相同，操作 `accountfundtable` 表。

### 8.2 AccountFundtableHisStore

与 FundtableHisStore 接口相同，操作 `accountfundtablehis` 表，主键不含 `strategy_id`。

### 8.3 AccountPositionUnitStore

与 PositionUnitStore 接口相似，操作 `account_position_unit` 表，主键/查询不含 `strategy_id`。

**组合持仓相关扩展**（详见 `../flows/combo-order-leg-split.md` §5.4）：

| 方法 | 功能 | 说明 |
|------|------|------|
| `queryUnpairedByOrderId` | 按 order_id 查未配对未平仓 | run_id + account_id + account_type + order_id，`combination_id=0` 且 `close_date=0`；按 id ASC；用于组合配对 |
| `updateCombinationId` | 回填 combination_id | 配对完成后按 id 更新 |
| `queryUnclosedByDirection` | FIFO 平仓查询 | 排序含 `CASE WHEN combination_id=0 THEN 0 ELSE 1 END ASC`，普通持仓优先 |

### 8.4 AccountContractStatStore

与 ContractStatStore 接口相同，操作 `account_contract_stat` 表，主键/查询不含 `strategy_id`。

### 8.5 CombinationUnitStore

```cpp
class CombinationUnitStore {
public:
    explicit CombinationUnitStore(sqlite3* db);
    ~CombinationUnitStore();

    int init();       // 预编译 SQL（createTable 后调用）
    void cleanup();   // 释放预编译语句

    int createTable();
    int insert(CombinationUnit* unit);  // 单条插入，回填 unit->id
    int queryById(int64_t combination_id, CombinationUnit* out);
    int breakCombination(int64_t combination_id, int64_t break_time);
    int queryByExistedFlag(int32_t existed_flag, std::vector<CombinationUnit>& out);
    int deleteByExistedFlag(int32_t existed_flag);
};
```

| 方法 | 功能 | 返回值 |
|------|------|--------|
| `init` | 预编译所有 SQL 语句 | 0 成功；CombinationUnitStore_SqlError 失败 |
| `cleanup` | 释放预编译语句 | - |
| `createTable` | 创建 combination_unit 表及索引 | 0 成功；CombinationUnitStore_SqlError |
| `insert` | 插入组合持仓记录 | 0 成功；CombinationUnitStore_InvalidArg；CombinationUnitStore_SqlError |
| `queryById` | 根据组合ID查询 | 0 成功；1 未找到；负数错误码 |
| `breakCombination` | 标记组合已拆分 | 0 成功；负数错误码 |
| `queryByExistedFlag` | 按 existed_flag 查询 | 0 成功；0=已拆分，1=有效 |
| `deleteByExistedFlag` | 按 existed_flag 删除 | 0 成功；交易日初始化清理用 |

**错误码**（-530 ~ -539）：CombinationUnitStore_InvalidArg(-531)、CombinationUnitStore_SqlError(-532)

### 8.6 AccountPositionUnitHisStore

```cpp
class AccountPositionUnitHisStore {
public:
    explicit AccountPositionUnitHisStore(sqlite3* db);
    
    int createTable();
    int batchInsert(const AccountPositionUnitHis* records, int count);
    int queryByOpenId(int64_t open_id, std::vector<AccountPositionUnitHis>& out);
    int queryByScope(const char* run_id, const char* account_id,
                     int32_t account_type, std::vector<AccountPositionUnitHis>& out);
};
```

| 方法 | 功能 | 说明 |
|------|------|------|
| `createTable` | 创建 account_position_unit_his 表及索引 | 含 open_id 和 scope 索引 |
| `batchInsert` | 批量插入历史记录 | 平仓时写入 |
| `queryByOpenId` | 按原持仓ID查询 | 查询某持仓的所有平仓历史 |
| `queryByScope` | 按作用域查询 | 查询某账户的所有平仓记录（无 strategy_id） |

**错误码**（-550 ~ -559）：AccountPositionUnitHisStore_InvalidArg(-551)、AccountPositionUnitHisStore_SqlError(-552)

### 8.7 CombinationUnitHisStore

```cpp
class CombinationUnitHisStore {
public:
    explicit CombinationUnitHisStore(sqlite3* db);
    ~CombinationUnitHisStore();

    int init();       // 预编译 SQL（createTable 后调用）
    void cleanup();   // 释放预编译语句

    int createTable();
    int batchInsert(const CombinationUnit* units, int count, int32_t oper_date);
    int queryByScopeAndDate(const char* run_id, const char* account_id,
                            int32_t account_type, int32_t oper_date,
                            std::vector<CombinationUnitHis>& out);
};
```

| 方法 | 功能 | 说明 |
|------|------|------|
| `init` | 预编译所有 SQL 语句 | createTable 后调用 |
| `cleanup` | 释放预编译语句 | - |
| `createTable` | 创建 combination_unit_his 表 | 日终结算归档用 |
| `batchInsert` | 批量插入组合单元历史 | oper_date 为结算日期 YYYYMMDD |
| `queryByScopeAndDate` | 按作用域+日期查询 | 查询某账户某日的组合历史 |

**错误码**（-533 ~ -534）：CombinationUnitHisStore_InvalidArg(-533)、CombinationUnitHisStore_SqlError(-534)

---

## 9. 辅助结构体

```cpp
// 批量平仓参数
typedef struct t_PositionCloseParam {
    int64_t id;     // PositionUnit.id
    int64_t fee;    // 开仓费 + 平仓费（×10000）
    int64_t pnl;    // 实现盈亏（×10000）
} PositionCloseParam;

// 批量盈亏更新参数（PositionUnitStore::batchUpdatePnl 实现使用 PositionUnit*，仅填 id、pnl，语义等价）
typedef struct t_PositionPnlParam {
    int64_t id;     // PositionUnit.id
    int64_t pnl;    // 最新浮动盈亏（×10000）
} PositionPnlParam;
```

---

## 10. 错误码（-400 ~ -499）

| 错误码 | 值 | 所属 Store | 触发条件 |
|--------|-----|-----------|---------|
| OrderStore_NotFound | -403 | OrderStore | queryByOrderId 未找到时返回 |
| TradeStore_InvalidArg | -461 | TradeStore | 参数无效（trade为空或主键字段缺失） |
| TradeStore_SqlError | -462 | TradeStore | SQL执行错误 |
| TradeStore_NotFound | -463 | TradeStore | queryByPrimaryKey未找到记录 |
| TradeStore_DupKey | -464 | TradeStore | insert时主键已存在 |
| PositionUnitStore_NotFound | -413 | PositionUnitStore | 更新时无匹配行 |
| FundtableStore_NotFound | -433 | FundtableStore | 查询/更新无匹配记录 |
| FundtableStore_DupKey | -2 | 通用 | om_set_fund_config 主键重复（非 FundtableStore 段） |
| ContractStatStore_NotFound | -423 | ContractStatStore | 查询/更新无匹配记录 |
| PositionUnitHisStore_InvalidArg | -541 | PositionUnitHisStore | 参数无效 |
| PositionUnitHisStore_SqlError | -542 | PositionUnitHisStore | SQL执行错误 |
| AccountPositionUnitHisStore_InvalidArg | -551 | AccountPositionUnitHisStore | 参数无效 |
| AccountPositionUnitHisStore_SqlError | -552 | AccountPositionUnitHisStore | SQL执行错误 |
| OrderHisStore_InvalidArg | -561 | OrderHisStore | 参数无效 |
| OrderHisStore_SqlError | -562 | OrderHisStore | SQL执行错误 |
| TradeHisStore_InvalidArg | -571 | TradeHisStore | 参数无效 |
| TradeHisStore_SqlError | -572 | TradeHisStore | SQL执行错误 |

---

## 11. 相关文档

| 主题 | 位置 |
|------|------|
| 数据模型定义 | `02-domain/*-model.md` |
| 表结构 SQL | `02-domain/*-model.md` §数据库表结构 |
| 模块职责 | `01-architecture/module-data.md` |
