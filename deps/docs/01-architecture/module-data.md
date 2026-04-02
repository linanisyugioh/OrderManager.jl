# 模块：data（数据层）

> 从old_docs/modules/mod_data.md迁移精简  
> 职责：封装 SQLite 数据库操作，为 core 层提供 CRUD 接口

---

## 1. 模块概述

### 1.1 职责

- 封装 SQLite 数据库操作
- 为上层（core）提供 OmOrder / OmTrade / PositionUnit / ContractStat / Fundtable / AccountFundtable / AccountPositionUnit 表的 CRUD 接口
- 管理数据库连接和事务（事务控制权在 service 层）

### 1.2 设计原则

- data 层**只做数据存取**，不包含任何业务逻辑
- 每个 Store 类负责**一张表**，职责单一
- 所有 Store 共享同一个 sqlite3 连接（由 DbManager 管理）
- FeeCodeInfo 由外部传入，不持久化，data 层无对应 Store

---

## 2. 文件清单

data 层所有类位于 `namespace om`。

| 文件路径 | 类型 | 职责说明 |
|---------|------|---------|
| `data/db_manager.h/.cc` | 头/实现 | DbManager 类：SQLite 连接的打开/关闭/事务控制 |
| `data/order_store.h/.cc` | 头/实现 | OrderStore 类：order 表 CRUD |
| `data/order_his_store.h/.cc` | 头/实现 | OrderHisStore 类：order_his 表 CRUD（日终快照） |
| `data/trade_store.h/.cc` | 头/实现 | TradeStore 类：trade 表 CRUD |
| `data/trade_his_store.h/.cc` | 头/实现 | TradeHisStore 类：trade_his 表 CRUD（日终快照） |
| `data/position_unit_store.h/.cc` | 头/实现 | PositionUnitStore 类：position_unit 表 CRUD |
| `data/position_unit_his_store.h/.cc` | 头/实现 | PositionUnitHisStore 类：position_unit_his 表 CRUD |
| `data/contract_stat_store.h/.cc` | 头/实现 | ContractStatStore 类：contract_stat 表 CRUD |
| `data/fundtable_store.h/.cc` | 头/实现 | FundtableStore 类：fundtable 表 CRUD |
| `data/fundtable_his_store.h/.cc` | 头/实现 | FundtableHisStore 类：fundtablehis 表 CRUD |
| `data/account_fundtable_store.h/.cc` | 头/实现 | AccountFundtableStore 类：accountfundtable 表 CRUD |
| `data/account_fundtable_his_store.h/.cc` | 头/实现 | AccountFundtableHisStore 类：accountfundtablehis 表 |
| `data/account_position_unit_store.h/.cc` | 头/实现 | AccountPositionUnitStore 类：account_position_unit 表 |
| `data/account_position_unit_his_store.h/.cc` | 头/实现 | AccountPositionUnitHisStore 类：account_position_unit_his 表 |
| `data/account_contract_stat_store.h/.cc` | 头/实现 | AccountContractStatStore 类：account_contract_stat 表 |
| `data/combination_unit_store.h/.cc` | 头/实现 | CombinationUnitStore 类：combination_unit 表（组合持仓） |
| `data/combination_unit_his_store.h/.cc` | 头/实现 | CombinationUnitHisStore 类：combination_unit_his 表（组合持仓历史） |

---

## 3. 数据库表结构概览

### 3.1 策略级表

| 表名 | 对应结构体 | 主键 | 说明 |
|------|-----------|------|------|
| order | OmOrder | 6字段联合 | 当日委托记录 |
| order_his | OmOrder | 6字段联合 | 委托历史快照（日终生成） |
| trade | OmTrade | 7字段联合 | 成交记录明细 |
| trade_his | OmTrade | 7字段联合 | 成交历史快照（日终生成） |
| position_unit | PositionUnit | id自增 | 每手持仓一条 |
| position_unit_his | PositionUnitHis | id自增 | 持仓单元平仓历史 |
| contract_stat | ContractStat | 5字段联合 | 合约级持仓统计 |
| fundtable | Fundtable | 4字段联合 | 策略级资金 |
| fundtablehis | FundtableHis | 5字段联合 | 资金历史快照 |

### 3.2 账户级表

| 表名 | 对应结构体 | 主键 | 说明 |
|------|-----------|------|------|
| accountfundtable | AccountFundtable | 3字段联合 | 账户级资金 |
| accountfundtablehis | AccountFundtableHis | 4字段联合 | 账户资金历史 |
| account_position_unit | AccountPositionUnit | id自增 | 账户级持仓 |
| account_position_unit_his | AccountPositionUnitHis | id自增 | 账户级持仓平仓历史 |
| account_contract_stat | AccountContractStat | 4字段联合 | 账户合约统计 |
| combination_unit | CombinationUnit | id自增 | 组合持仓（保证金优惠） |
| combination_unit_his | CombinationUnitHis | id自增 | 组合持仓历史 |

---

## 4. 核心类职责

### 4.1 DbManager（数据库连接管理）

DbManager 为单例，**持有并维护所有 Store 实例**。OmService 通过 DbManager 获取 Store 指针，不直接持有 Store；Store 的生命周期由 DbManager 的 `createStoresAndTables` / `cleanupStores` 控制。

```cpp
class DbManager {
public:
    static DbManager& instance();

    int open(const char* db_path);       // 打开或创建数据库
    void close();                        // 关闭连接
    int createStoresAndTables();         // 创建所有 Store 并建表（open 后调用）
    void cleanupStores();                // 释放所有 Store（不关闭连接）

    sqlite3* getDb();                    // 返回 sqlite3 裸指针
    bool isOpen() const;                 // 连接是否已打开

    // Store 访问接口（非拥有，调用方不得保存超过 cleanupStores 的生命周期）
    OrderStore* getOrderStore();
    OrderHisStore* getOrderHisStore();
    PositionUnitStore* getPositionUnitStore();
    PositionUnitHisStore* getPositionUnitHisStore();
    ContractStatStore* getContractStatStore();
    FundtableStore* getFundtableStore();
    FundtableHisStore* getFundtableHisStore();
    AccountPositionUnitStore* getAccountPositionUnitStore();
    AccountPositionUnitHisStore* getAccountPositionUnitHisStore();
    AccountContractStatStore* getAccountContractStatStore();
    AccountFundtableStore* getAccountFundtableStore();
    AccountFundtableHisStore* getAccountFundtableHisStore();
    TradeStore* getTradeStore();
    TradeHisStore* getTradeHisStore();
    CombinationUnitStore* getCombinationUnitStore();
    CombinationUnitHisStore* getCombinationUnitHisStore();

    // 事务控制（由 service 层调用）
    int beginTransaction();
    int commit();
    void rollback();

    // WAL checkpoint
    int walCheckpoint();                 // TRUNCATE 模式，合并并截断 WAL
    void walCheckpointPassive();         // PASSIVE 模式，不阻塞查询
};
```

**事务分层约定**：事务控制权集中在 **service 层**，core 层和 data 层不得自行调用事务方法。

### 4.2 OrderStore（委托表操作）

```cpp
class OrderStore {
public:
    int createTable();
    int upsert(const OmOrder* order);           // INSERT OR REPLACE
    int queryByOrderId(const char* order_id, int32_t oper_date,
                        const char* strategy_id, const char* run_id,
                        const char* account_id, int32_t account_type,
                        OmOrder* out);
    int deleteAll();                          // 交易日初始化时清空
};
```

### 4.3 TradeStore（成交表操作）

```cpp
class TradeStore {
public:
    int createTable();
    int insert(const OmTrade* trade);             // 插入成交记录
    
    // 按7字段主键查询单条成交
    int queryByPrimaryKey(const char* order_id, int32_t trade_date,
                          const char* strategy_id, const char* run_id,
                          const char* account_id, int32_t account_type,
                          const char* match_seqno, OmTrade* out);
    
    // 按作用域+order_id查询该委托的所有成交
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
    
    int deleteByDate(int32_t trade_date);       // 按日期删除
    int deleteAll();                              // 清空全部
};
```

**特点**：
- TradeStore 仅提供成交记录存取，不参与持仓/资金计算
- 查询方法必须包含4字段作用域，防止跨作用域数据混淆

### 4.4 PositionUnitStore（持仓单元表）

```cpp
class PositionUnitStore {
public:
    int createTable();
    int batchInsert(PositionUnit* units, int count);  // 开仓批量插入
    
    // 查询方法 - 全部使用 std::vector 输出
    int queryUnclosedByDirection(..., std::vector<PositionUnit>& out);
    int queryAllUnclosedByScope(..., std::vector<PositionUnit>& out);
    int queryAllUnclosedByCode(const char* code, std::vector<PositionUnit>& out);
    int queryAllUnclosed(std::vector<PositionUnit>& out);
    
    // 更新方法
    int batchUpdateClose(..., const PositionCloseParam* params, int count);
    int batchUpdatePnl(const PositionPnlParam* params, int count);
    int updateHoldCostAndMargin(int64_t id, int64_t hold_cost, int64_t margin, int64_t pnl);
    int deleteClosedUnits();                  // 删除已平仓记录
};
```

### 4.5 ContractStatStore（合约统计表）

```cpp
class ContractStatStore {
public:
    int createTable();
    int upsert(const ContractStat* stat);
    int queryByScope(..., ContractStat* out);
    int deleteAll();
    
    // delta 方式更新（正=增加，负=减少）
    int updateVolume(..., int32_t today_long_delta, int32_t yesterday_long_delta,
                     int32_t today_short_delta, int32_t yesterday_short_delta);
    int updateFrozen(..., int32_t today_long_frozen_delta, ...);
};
```

### 4.6 FundtableStore（资金表）

```cpp
class FundtableStore {
public:
    int createTable();
    int insert(const Fundtable& fund);       // 插入资金记录
    int queryByScope(..., Fundtable* out);   // 按主键查询
    int update(const Fundtable* fund);       // 全字段更新
    int existsByScope(...);                  // 检查存在性
    int queryAll(std::vector<Fundtable>& out);               // 查询全部（日终结算用）
    int batchUpdatePnl(const std::vector<FundPnlDelta>& deltas);  // 行情路径批量更新 pnl/equity
};
```

### 4.7 FundtableHisStore（资金历史表）

```cpp
class FundtableHisStore {
public:
    int createTable();
    int insert(const FundtableHis& his);
    int batchInsert(const std::vector<FundtableHis>& records, bool skip_duplicate = false);
    int queryByPrimaryKey(..., FundtableHis* out);
    int queryByDateRange(..., std::vector<FundtableHis>& out);
    int exists(...);
};
```

**batchInsert 说明**：`skip_duplicate=true` 时使用 INSERT OR IGNORE，主键重复时忽略；`false` 时主键重复返回 `FundtableHisStore_DupKey(-454)`。

### 4.8 账户级 Store（与策略级类似）

| Store 类 | 对应表 | 说明 |
|---------|--------|------|
| AccountFundtableStore | accountfundtable | 与 FundtableStore 接口类似 |
| AccountFundtableHisStore | accountfundtablehis | 与 FundtableHisStore 接口类似 |
| AccountPositionUnitStore | account_position_unit | 与 PositionUnitStore 接口类似 |
| AccountPositionUnitHisStore | account_position_unit_his | 与 PositionUnitHisStore 接口类似 |
| AccountContractStatStore | account_contract_stat | 与 ContractStatStore 接口类似 |

---

## 5. 索引设计

### 5.1 position_unit 表索引

```sql
-- FIFO 平仓查询
CREATE INDEX idx_pu_scope_dir_closedate 
ON position_unit (run_id, account_id, account_type, strategy_id, direction, close_date);

-- 按合约查询（行情刷新）
CREATE INDEX idx_pu_scope_code_dir_closedate 
ON position_unit (run_id, account_id, account_type, strategy_id, code, direction, close_date);

-- 跨作用域按合约查询
CREATE INDEX idx_pu_code_closedate 
ON position_unit (code, close_date);
```

---

## 6. SQLite 性能优化

DbManager 在 `open()` 时自动应用以下 PRAGMA 设置，实现文件：`data/db_manager.cc`。

### 6.1 已启用的优化

| PRAGMA | 当前值 | 说明 | 取舍 |
|--------|--------|------|------|
| `journal_mode` | WAL | Write-Ahead Logging | 读写互不阻塞，显著提升并发；需配合 checkpoint 控制 WAL 文件大小 |
| `synchronous` | FULL | 关键操作后 fsync | 金融数据优先保证不丢，牺牲部分写入吞吐 |
| `cache_size` | -20480 | 80MB 页缓存（负值=页数，4096 bytes/页） | 减少磁盘 I/O，适合中等规模数据 |
| `wal_autocheckpoint` | 0 | 关闭 WAL 自动 checkpoint | 默认当 WAL 达约 1000 页（~4MB）时 SQLite 会自动 checkpoint，可能导致写入阻塞或不可预期 I/O 峰值；改为由业务在可控时机显式调用 |

### 6.2 WAL Checkpoint

- **自动 checkpoint 已关闭**：`PRAGMA wal_autocheckpoint=0`，避免数据量达到阈值时 SQLite 自动触发 checkpoint 带来的阻塞与延迟
- **显式 checkpoint 接口**：`DbManager::walCheckpoint()`，内部调用 `sqlite3_wal_checkpoint_v2(..., SQLITE_CHECKPOINT_TRUNCATE)`
- **作用**：将 WAL 内容合并回主库并截断 WAL 文件为 0 字节
- **调用时机**：建议在日初始化、日终结算等大批量写入后调用，由 service 层（OmService）负责
- **walCheckpointPassive**：PASSIVE 模式，尽量合并 WAL 帧，不阻塞 reader，供 QueryKit 读连接能见到 writer 已提交的数据

### 6.3 可选优化（未启用）

以下为常见优化项，当前未采用，可根据实际负载评估：

| PRAGMA/选项 | 说明 | 适用场景 |
|-------------|------|----------|
| `temp_store=MEMORY` | 临时表存内存 | 大量临时表/子查询时 |
| `mmap_size` | 内存映射数据库文件 | 大库、随机读多时 |
| `synchronous=NORMAL` | 减少 fsync 频率 | 可接受极端断电丢少量数据时（**金融场景慎用**） |
| `busy_timeout` | 锁等待超时（毫秒） | 多连接并发时 |

### 6.4 设计原则

- **持久性优先**：`synchronous=FULL` 保证关键数据落盘，不因追求性能而放宽
- **批量事务**：委托/成交/持仓等批量操作由 service 层包裹 `BEGIN`/`COMMIT`，减少事务开销
- **预编译语句**：各 Store 使用 `sqlite3_prepare_v2` 预编译常用 SQL，避免重复解析

---

## 7. 错误码（-400 ~ -499）

> 完整错误码以 `include/om_error.h` 为准，以下仅列常用项。

| 错误码 | 值 | 所属 Store | 触发条件 |
|--------|-----|-----------|---------|
| OrderStore_InvalidArg | -401 | OrderStore | 参数非法 |
| OrderStore_SqlError | -402 | OrderStore | SQL 执行错误 |
| OrderStore_NotFound | -403 | OrderStore | 按主键未找到委托记录 |
| TradeStore_InvalidArg | -461 | TradeStore | 参数无效 |
| TradeStore_SqlError | -462 | TradeStore | SQL执行错误 |
| TradeStore_NotFound | -463 | TradeStore | 查询无匹配记录 |
| TradeStore_DupKey | -464 | TradeStore | 插入时主键已存在 |
| PositionUnitStore_NotFound | -413 | PositionUnitStore | 更新时无匹配行 |
| FundtableStore_NotFound | -433 | FundtableStore | 查询/更新无匹配记录 |
| FundtableStore_DupKey | -2 | 通用 | om_set_fund_config 主键重复（非 FundtableStore 段） |
| ContractStatStore_NotFound | -423 | ContractStatStore | 查询/更新无匹配记录 |

---

## 8. 依赖关系

```
data/ 依赖：
  ├── include/（数据类型）
  └── utils/（LogManager）

data/ 被依赖：
  └── core/（所有 Processor 调用 Store）
      └── service/（OmService 初始化 Store）
```

---

## 9. 相关文档

| 主题 | 位置 |
|------|------|
| Store 接口详细定义 | `03-implementation/interfaces/store-apis.md` |
| 表结构详细定义 | `02-domain/*-model.md` |
| 事务控制说明 | `01-architecture/module-service.md` |
