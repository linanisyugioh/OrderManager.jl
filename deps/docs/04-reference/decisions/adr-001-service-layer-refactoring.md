# ADR-001：Service 层结构性改造方案

**状态：** 待讨论  
**日期：** 2026-03-17  
**决策者：** 开发团队

> **说明**：本文档描述拟议改造方案，部分文件名与当前代码可能不一致（如 `settlement_service` 对应当前 `trading_day_end_service`，`service_factory` 尚未实现）。

---

## 一、背景

当前 `service/` 目录下的代码在功能上已经完整运行，但随着组合委托、账户级资金/持仓、查询套件等功能的加入，暴露出多项结构性问题：OmService 类膨胀为上帝类、事务控制权分散、策略级/账户级逻辑高度重复、全局单例耦合导致不可测试等。

本文档提出一个**分阶段、低风险**的改造方案，每个阶段可独立合入，改造过程中不改变对外 API 行为。

### 当前 service/ 文件清单

| 文件 | 行数（约） | 职责 |
|------|-----------|------|
| `om_service.h/.cc` | 180 + 833 | 系统生命周期、交易日管理、全部业务操作、组件创建、缓存管理 |
| `settlement_service.h/.cc` | 55 + 664 | 日终结算（从 OmService 提取） |
| `combo_order_service.h/.cc` | 89 + 521 | 组合委托处理 |
| `query_kit_service.h/.cc` | 119 + 397 | 查询接口、QueryKitPool 生命周期、查询作用域缓存 |
| `service_factory.h/.cc` | 109 + 102 | Store/Processor 创建工厂 |

### 问题清单

| # | 问题 | 严重程度 | 影响 |
|---|------|---------|------|
| P1 | OmService 上帝类（1000+ 行，7+ 种职责） | 高 | 维护困难、改动风险高 |
| P2 | 事务控制权分散（ComboOrderService 自控事务） | 高 | 违反架构约定，嵌套事务风险 |
| P3 | 策略级/账户级逻辑 3 对重复 | 中 | 修改需同步两处，易遗漏 |
| P4 | SettlementService 依赖注入不一致 | 中 | 部分参数传入、部分直取单例 |
| P5 | setFundConfig 临时资源管理复杂 | 中 | 多退出路径需手动清理，易泄露 |
| P6 | QueryKitService 职责混杂 | 中 | 查询业务逻辑嵌入 service 层 |
| P7 | 全局单例导致不可测试 | 高 | 无法 mock 数据层 |
| P8 | queryOrdersByScope 全表扫描过滤 | 中 | 大数据量下性能隐患 |
| P9 | SettlementService 类/自由函数混合风格 | 低 | 风格不统一，无法 mock 子步骤 |
| P10 | ServiceStores 双轨制冗余 | 低 | 增加理解成本 |

---

## 二、改造目标

1. **OmService 瘦身**：从 1000+ 行降至 ~300 行，仅保留顶层编排和生命周期管理
2. **事务控制统一**：所有事务边界集中在 OmService，子 service 不自行管理事务
3. **消除重复**：策略级/账户级对称逻辑通过模板或统一抽象消除
4. **依赖注入一致**：子 service 的全部依赖通过构造函数或参数传入，不直取单例
5. **可测试性**：关键 service 类可在测试中注入 mock 依赖

### 不改动范围

- **对外 C API 签名不变**：`om_init`/`om_handle_order` 等接口签名、行为、返回值不变
- **core 层不动**：Processor 类的接口和实现保持不变
- **data 层不动**：Store 类的接口和实现保持不变
- **数据库 schema 不变**：表结构和 SQL 不变

---

## 三、改造方案

### 阶段 1：统一事务控制（解决 P2）

**目标**：ComboOrderService 不再自行管理事务，事务边界统一上移至 OmService。

#### 1.1 当前问题

```
OmService::handleOrder
├── 普通委托：OmService 开事务 → order_proc_->process → 提交
└── 组合委托：直接调用 combo_svc_->handle()
                └── ComboOrderService::handle() 自己开事务 → 逻辑 → 提交
```

ComboOrderService 直接调用 `DbManager::instance().beginTransaction()/commit()/rollback()`，违反"事务控制集中在 service 层"的架构约定。

#### 1.2 改造方案

将 `ComboOrderService::handle()` 拆分为：
- `handleInTx()`：纯业务逻辑，假定已在事务内，不做事务管理
- 事务边界由 `OmService::handleOrder` 统一控制

**改造前 OmService::handleOrder**：
```cpp
if (ComboOrderService::isCombinationOrder(order.code)) {
    return combo_svc_->handle(order, fee_info); // 内部自管事务
}
int rc = DbManager::instance().beginTransaction();
// ... 普通委托处理 ...
```

**改造后 OmService::handleOrder**：
```cpp
int rc = DbManager::instance().beginTransaction();
if (rc != OM_Ok) return rc;

if (ComboOrderService::isCombinationOrder(order.code)) {
    rc = combo_svc_->handleInTx(order, fee_info);
} else {
    rc = order_proc_->process(order, fee_info);
}

if (rc != OM_Ok) {
    DbManager::instance().rollback();
    return rc;
}
return DbManager::instance().commit();
```

**改造后 ComboOrderService**：
```cpp
class ComboOrderService {
public:
    // 新接口：在已有事务内执行，不管理事务边界
    int handleInTx(const OmOrder& order, const FeeCodeInfo& fee_info);

    // 静态工具方法不变
    static bool isCombinationOrder(const char* code);

private:
    // 所有私有方法不变，仅删除 handle() 中的 beginTransaction/commit/rollback 调用
};
```

#### 1.3 涉及文件

| 文件 | 改动内容 |
|------|---------|
| `service/combo_order_service.h` | `handle()` 重命名为 `handleInTx()`，删除事务管理 |
| `service/combo_order_service.cc` | 移除 `handle()` 中的 `beginTransaction`/`commit`/`rollback` |
| `service/om_service.cc` | `handleOrder` 统一事务边界，组合和普通共用 |

#### 1.4 验证方法

- 场景 10（组合委托开平仓）全部测试用例通过
- 场景 12（日终结算未终态委托处理）通过
- 故意在 `handleInTx` 中途制造失败，确认回滚行为正确

---

### 阶段 2：提取 TradingDayInitService（解决 P1 核心部分）

**目标**：将交易日初始化的 4 个私有方法从 OmService 提取为独立 service 类，与 SettlementService 对称。

#### 2.1 当前问题

OmService 中 `tradingDayUpdate` 占约 200 行，包含 4 个仅供 `tradingDayUpdate` 调用的私有方法：
- `cleanupDailyData()`（48 行）
- `rebuildStrategyContractStats()`（42 行）
- `rebuildAccountContractStats()`（44 行）
- `validateAccountFunds()`（60 行）

这些方法形成一个独立的内聚单元，与日常业务操作（handleOrder 等）无关。

#### 2.2 改造方案

新建 `TradingDayInitService`，设计模式与 `SettlementService` 对称：

**新文件 `service/trading_day_init_service.h`**：
```cpp
namespace om {

struct TradingDayInitDeps {
    int32_t trading_date = 0;
    // 所有 Store 指针通过参数传入，不直取单例
    OrderStore* order_store = nullptr;
    PositionUnitStore* pu_store = nullptr;
    ContractStatStore* cs_store = nullptr;
    AccountPositionUnitStore* acct_pu_store = nullptr;
    AccountContractStatStore* acct_cs_store = nullptr;
    TradeStore* trade_store = nullptr;
    CombinationUnitStore* combo_store = nullptr;
    FundtableStore* fund_store = nullptr;
    AccountFundtableStore* acct_fund_store = nullptr;
};

class TradingDayInitService {
public:
    /**
     * @brief 执行交易日初始化（事务已由调用方开启）
     * @return OM_Ok 成功；OM_FundCheckFailed 资金校验失败；其他错误码
     */
    int run(const TradingDayInitDeps& deps);

private:
    int cleanupDailyData(const TradingDayInitDeps& deps);
    int rebuildStrategyContractStats(const TradingDayInitDeps& deps);
    int rebuildAccountContractStats(const TradingDayInitDeps& deps);
    int validateAccountFunds(const TradingDayInitDeps& deps);
};

} // namespace om
```

**改造后 OmService::tradingDayUpdate 核心部分**：
```cpp
int rc = DbManager::instance().beginTransaction();
if (rc != OM_Ok) return rc;

TradingDayInitDeps deps;
deps.trading_date    = trading_date;
deps.order_store     = db.getOrderStore();
deps.pu_store        = db.getPositionUnitStore();
// ... 填充其余依赖 ...

TradingDayInitService init_svc;
rc = init_svc.run(deps);
if (rc != OM_Ok) {
    DbManager::instance().rollback();
    return rc;
}

return DbManager::instance().commit();
```

#### 2.3 涉及文件

| 文件 | 改动内容 |
|------|---------|
| `service/trading_day_init_service.h` | **新建**，定义 TradingDayInitDeps 和 TradingDayInitService |
| `service/trading_day_init_service.cc` | **新建**，从 om_service.cc 迁移 4 个私有方法 |
| `service/om_service.h` | 删除 4 个私有方法声明 |
| `service/om_service.cc` | `tradingDayUpdate` 改为委托 TradingDayInitService::run() |
| `CMakeLists.txt` | 新增编译单元 |

#### 2.4 验证方法

- 场景 5（交易日初始化）全部通过
- 场景 6（多日完整流程）通过

---

### 阶段 3：统一 SettlementService 依赖注入（解决 P4 + P9）

**目标**：SettlementService 的全部依赖通过 `SettlementDeps` 传入，不再从 `DbManager::instance()` 直取 Store。同时将 file-static 函数改为类私有方法。

#### 3.1 当前问题

```cpp
// settlement_service.cc
int SettlementService::run(const SettlementDeps& deps, ...) {
    DbManager& db = DbManager::instance(); // 直取单例
    ctx.fund_store = db.getFundtableStore(); // 隐式依赖
    // ...
}
```

业务层依赖通过参数传入，数据层依赖直取单例，来源不一致。且所有逻辑写在 file-static 自由函数中，`SettlementService` 类只有一个 `run()` 方法。

#### 3.2 改造方案

**扩展 SettlementDeps，包含全部数据层依赖**：
```cpp
struct SettlementDeps {
    int32_t trading_date = 0;

    /* 业务层（Processor） */
    OrderProcessor* order_proc = nullptr;
    FundtableSnapshotHandler* fund_snapshot = nullptr;
    AccountFundtableProcessor* acct_fund_proc = nullptr;

    /* 缓存 */
    const std::unordered_map<std::string, int64_t>* price_cache = nullptr;
    const std::unordered_map<std::string, FeeCodeInfo>* fee_cache = nullptr;

    /* 数据层（Store）—— 改造新增 */
    FundtableStore* fund_store = nullptr;
    AccountFundtableStore* acct_fund_store = nullptr;
    PositionUnitStore* pu_store = nullptr;
    AccountPositionUnitStore* acct_pu_store = nullptr;
    OrderStore* order_store = nullptr;
    OrderHisStore* order_his_store = nullptr;
    TradeStore* trade_store = nullptr;
    TradeHisStore* trade_his_store = nullptr;
    CombinationUnitStore* combo_store = nullptr;
    CombinationUnitHisStore* combo_his_store = nullptr;
    ContractStatStore* cs_store = nullptr;
    AccountContractStatStore* acct_cs_store = nullptr;
};
```

**将自由函数改为私有成员方法**：
```cpp
class SettlementService {
public:
    int run(const SettlementDeps& deps, bool* out_has_pnl_mismatch);

private:
    int processNonTerminalOrders();
    void checkFrozenAssets();
    int createFundSnapshots();
    int validateCacheCompleteness(const std::vector<Fundtable>& funds);
    int settleStrategyFunds(std::vector<Fundtable>& funds, bool& has_pnl_mismatch);
    int settleAccountFunds(std::vector<AccountFundtable>& acct_funds);
    int archiveHistory();

    // run() 中填充，私有方法共享
    SettlementDeps deps_;
};
```

删除 `SettlementCtx`，不再需要从 `DbManager::instance()` 获取 Store。调用方（OmService）负责填充全部依赖。

#### 3.3 涉及文件

| 文件 | 改动内容 |
|------|---------|
| `service/settlement_service.h` | 扩展 SettlementDeps，自由函数改为私有方法声明 |
| `service/settlement_service.cc` | 删除 SettlementCtx，static 函数改为成员方法，用 `deps_` 替代 `ctx` |
| `service/om_service.cc` | `tradingDayEnd` 中填充 SettlementDeps 的 Store 字段 |

#### 3.4 验证方法

- 场景 4（日终结算完整流程）通过
- 场景 12（日终结算未终态委托处理）通过

---

### 阶段 4：setFundConfig RAII 化（解决 P5）

**目标**：消除 `setFundConfig` / `setAccountFundConfig` 中的手动资源管理。

#### 4.1 当前问题

```cpp
bool temp_opened = openDbIfNeeded(open_rc);
// ... 多个 if (temp_opened) DbManager::instance().close(); 分布在各退出路径 ...
```

6 个退出路径中有 4 处需要检查 `temp_opened`，容易遗漏。

#### 4.2 改造方案

引入一个轻量级 RAII 守卫：

```cpp
// 文件级辅助类（om_service.cc 内部）
class ScopedDbGuard {
public:
    explicit ScopedDbGuard(bool should_close)
        : should_close_(should_close) {}
    ~ScopedDbGuard() {
        if (should_close_ && DbManager::instance().isOpen()) {
            DbManager::instance().close();
        }
    }
    void dismiss() { should_close_ = false; }
private:
    bool should_close_;
};
```

**改造后 setFundConfig**：
```cpp
int OmService::setFundConfig(const Fundtable& fund) {
    if (!inited_) return OM_NotInited;
    // ... 参数校验 ...

    int open_rc = OM_Ok;
    bool temp_opened = openDbIfNeeded(open_rc);
    if (open_rc != OM_Ok) return open_rc;
    ScopedDbGuard guard(temp_opened); // 自动在退出时关闭

    FundtableStore* store = DbManager::instance().getFundtableStore();
    std::unique_ptr<FundtableStore> temp_store;
    if (!store) {
        temp_store = std::make_unique<FundtableStore>(DbManager::instance().getDb());
        int rc = temp_store->init();
        if (rc != OM_Ok) return rc; // guard 自动关闭
        store = temp_store.get();
    }

    int exists = store->existsByScope(...);
    if (exists == 1) return FundtableStore_DupKey; // guard 自动关闭
    if (exists < 0) return exists;

    return store->insert(fund); // guard 自动关闭
}
```

所有退出路径不再需要手动检查 `temp_opened`。`setAccountFundConfig` 做相同改造。

#### 4.3 涉及文件

| 文件 | 改动内容 |
|------|---------|
| `service/om_service.cc` | 添加 ScopedDbGuard，重写 setFundConfig 和 setAccountFundConfig |

#### 4.4 验证方法

- 在 tradingDayUpdate 之前调用 setFundConfig，验证临时打开/关闭正常
- 在 tradingDayUpdate 之后调用 setFundConfig，验证无多余关闭
- 制造 existsByScope 失败，确认数据库正确关闭

---

### 阶段 5：消除策略级/账户级重复（解决 P3）

**目标**：将 3 对重复的策略级/账户级方法统一为模板或参数化实现。

#### 5.1 当前重复清单

| 对 | 策略级方法 | 账户级方法 | 位置 |
|----|----------|----------|------|
| 1 | `rebuildStrategyContractStats` | `rebuildAccountContractStats` | TradingDayInitService |
| 2 | `settleStrategyFunds` | `settleAccountFunds` | SettlementService |
| 3 | `setFundConfig` | `setAccountFundConfig` | OmService |

#### 5.2 改造方案

**对 1 — rebuildContractStats 模板化**：

两个方法的逻辑完全相同：查询未平仓 → 按 scope key 聚合 → upsert。差异仅在于类型（`PositionUnit`/`ContractStat` vs `AccountPositionUnit`/`AccountContractStat`）和 scope key 的组成。

引入一个模板辅助函数：

```cpp
// trading_day_init_service.cc 内部

template<typename Unit, typename Stat, typename QueryFn, typename UpsertFn, typename KeyFn, typename InitFn>
static int rebuildContractStatsImpl(
    int32_t trading_date,
    QueryFn query_fn,        // 查询未平仓: (vector<Unit>&) -> int
    UpsertFn upsert_fn,      // 写入统计: (Stat*) -> int
    KeyFn make_key,           // 生成 scope key: (const Unit&) -> string
    InitFn init_stat          // 初始化 Stat 字段: (Stat&, const Unit&) -> void
) {
    std::vector<Unit> units;
    int rc = query_fn(units);
    if (rc != OM_Ok) return rc;

    std::map<std::string, Stat> stat_map;
    for (const auto& unit : units) {
        std::string key = make_key(unit);
        Stat& stat = stat_map[key];
        if (/* 未初始化 */) {
            memset(&stat, 0, sizeof(Stat));
            init_stat(stat, unit);
        }
        bool is_today = (unit.open_date == trading_date);
        if (unit.direction == PositionSide_Long) {
            is_today ? stat.today_long_volume++ : stat.yesterday_long_volume++;
        } else {
            is_today ? stat.today_short_volume++ : stat.yesterday_short_volume++;
        }
    }

    for (auto& kv : stat_map) {
        rc = upsert_fn(&kv.second);
        if (rc != OM_Ok) return rc;
    }
    return OM_Ok;
}
```

调用时只需传入不同的类型参数和 lambda：
```cpp
int TradingDayInitService::rebuildStrategyContractStats(const TradingDayInitDeps& deps) {
    return rebuildContractStatsImpl<PositionUnit, ContractStat>(
        deps.trading_date,
        [&](auto& units) { return deps.pu_store->queryAllUnclosed(units); },
        [&](auto* stat)  { return deps.cs_store->upsert(stat); },
        [](const auto& u) { /* makeScopeKey with strategy_id */ },
        [](auto& stat, const auto& u) { /* copy run_id, account_id, strategy_id, code */ }
    );
}

int TradingDayInitService::rebuildAccountContractStats(const TradingDayInitDeps& deps) {
    return rebuildContractStatsImpl<AccountPositionUnit, AccountContractStat>(
        deps.trading_date,
        [&](auto& units) { return deps.acct_pu_store->queryAllUnclosed(units); },
        [&](auto* stat)  { return deps.acct_cs_store->upsert(stat); },
        [](const auto& u) { /* makeScopeKey without strategy_id */ },
        [](auto& stat, const auto& u) { /* copy run_id, account_id, code */ }
    );
}
```

**对 2 — settleXxxFunds**：逻辑虽然相似，但字段名不同（`avail_cash` vs `account_cash`、`margin` vs `account_margin`），且 C 结构体无法多态。此对建议**保留两个方法但提取公共的单手结算辅助函数**到 `CalcHelper`：

```cpp
// core/calc_helper.h 新增
struct SettlementUnitResult {
    int64_t new_margin;
    int64_t margin_delta;
};

static SettlementUnitResult settleOneUnit(
    int64_t settlement_price, int32_t contract_multiply,
    int64_t old_margin, int32_t direction,
    const FeeCodeInfo& fee_info, int32_t hedge_flag);
```

两个 settle 方法调用相同的 `settleOneUnit`，只是将结果写入不同的字段。这样核心计算逻辑只有一份。

**对 3 — setFundConfig / setAccountFundConfig**：可以提取一个模板方法处理"打开 DB → 查重 → 插入"的公共流程，但这对方法的主要问题已在阶段 4 通过 RAII 解决，且两者行数不多，模板化收益有限。**建议保持现状，不额外模板化。**

#### 5.3 涉及文件

| 文件 | 改动内容 |
|------|---------|
| `service/trading_day_init_service.cc` | 引入 rebuildContractStatsImpl 模板 |
| `service/settlement_service.cc` | settleStrategyFunds/settleAccountFunds 调用 CalcHelper::settleOneUnit |
| `core/calc_helper.h` | 新增 settleOneUnit 辅助函数 |

#### 5.4 验证方法

- 场景 4、5、6、7 全部通过（覆盖日初始化和日终结算）

---

### 阶段 6：QueryKitService 职责拆分（解决 P6 + P8）

**目标**：将查询业务逻辑下沉到 core/data 层，QueryKitService 退化为纯编排层。

#### 6.1 当前问题

`QueryKitService::queryOrderIdsSimple` 包含 ~40 行的业务过滤逻辑：

```cpp
// query_kit_service.cc:210-250
for (size_t i = 0; i < orders.size(); ++i) {
    const OmOrder& o = orders[i];
    if (status == 0) { /* 未终态过滤 */ }
    else if (status == 1) { /* 终态过滤 */ }
    if (filter_code && strcmp(o.code, code) != 0) { continue; }
    if (filter_side) { /* 方向过滤 */ }
    if (filter_bs) { /* 买卖过滤 */ }
    // ...
}
```

`queryOrdersByScope` 使用 `queryAll()` 取全部委托再内存过滤：

```cpp
std::vector<OmOrder> all_orders;
int rc = kit->getOrderStore()->queryAll(all_orders); // 全表扫描
for (const auto& order : all_orders) {
    if (strcmp(order.run_id, run_id) == 0 && ...) { // 内存过滤
        out.push_back(order);
    }
}
```

#### 6.2 改造方案

**步骤 A：在 OrderStore 中增加按 scope 查询方法**：

```cpp
// data/order_store.h 新增
int queryByScope(const char* run_id, const char* account_id,
                 int32_t account_type, const char* strategy_id,
                 std::vector<OmOrder>& out);
```

用 SQL `WHERE run_id=? AND account_id=? AND account_type=? AND strategy_id=?` 替代全表扫描 + 内存过滤。

**步骤 B：将委托过滤逻辑下沉到 CalcHelper**：

```cpp
// core/calc_helper.h 新增

struct OrderFilter {
    int32_t status = 3;    // 0=未终态, 1=终态, 3=全部
    const char* code = nullptr;
    int32_t side = 3;      // 0=平仓, 1=开仓, 3=全部
    int32_t bs = 3;        // 0=空, 1=多, 3=全部
};

static bool matchOrderFilter(const OmOrder& order, const OrderFilter& filter);
```

**步骤 C：QueryKitService 简化为编排**：

```cpp
int QueryKitService::queryOrderIdsSimple(...) {
    // ...
    std::vector<OmOrder> orders;
    int rc = kit->getOrderStore()->queryByScope(
        query_run_id_.c_str(), query_account_id_.c_str(),
        query_account_type_, strategy_id, orders);

    OrderFilter filter;
    filter.status = status;
    filter.code = code;
    filter.side = side;
    filter.bs = bs;

    std::string result;
    for (const auto& o : orders) {
        if (CalcHelper::matchOrderFilter(o, filter)) {
            if (!result.empty()) result += ",";
            result += o.order_id;
        }
    }
    // ...
}
```

#### 6.3 涉及文件

| 文件 | 改动内容 |
|------|---------|
| `data/order_store.h/.cc` | 新增 `queryByScope` 方法 |
| `core/calc_helper.h` | 新增 `OrderFilter` 和 `matchOrderFilter` |
| `service/query_kit_service.cc` | 使用新接口重写 `queryOrdersByScope` 和 `queryOrderIdsSimple` |
| `kit/query_kit.h` | QueryKit 的 OrderStore 需要能访问新方法（已自动支持） |

#### 6.4 验证方法

- 场景 13（om_query.h 对外查询接口完整测试）全部通过

---

### 阶段 7（长期）：依赖注入基础设施（解决 P7）

**目标**：为关键 service 类引入依赖注入支持，使其可在测试中 mock。

> 此阶段影响较大，建议在前 6 个阶段稳定后再执行。不阻塞当前开发。

#### 7.1 改造思路

当前系统的核心耦合点是 `DbManager::instance()` 单例。改造思路是在 `OmService` 中集中持有 `DbManager*` 指针（通过 `init` 时传入或创建），子 service 不再直取单例。

**注意**：不引入复杂的 DI 框架，只在构造/调用参数中传递依赖。

**改造后的依赖传递路径**：

```
OmService（持有 DbManager 指针和所有 Processor/Service）
├── 传递给 TradingDayInitService（通过 TradingDayInitDeps）
├── 传递给 SettlementService（通过 SettlementDeps）
├── 传递给 ComboOrderService（通过构造函数）
└── 传递给 QueryKitService（通过 init 参数）
```

前 6 个阶段已将 TradingDayInitService 和 SettlementService 改为参数传入依赖，本阶段主要处理：

1. `OmService` 自身不再通过 `DbManager::instance()` 获取 Store，改为存储 `DbManager&` 引用
2. `ComboOrderService` 构造函数已经是依赖注入（现在已是），确认不使用 `DbManager::instance()`
3. 测试中可以创建独立的 `DbManager` 实例（使用内存数据库）传入

#### 7.2 涉及文件

| 文件 | 改动内容 |
|------|---------|
| `service/om_service.h` | 添加 `DbManager&` 成员引用 |
| `service/om_service.cc` | 全文 `DbManager::instance()` 替换为成员引用 |
| `service/combo_order_service.cc` | 删除剩余的 `DbManager::instance()` 引用 |

#### 7.3 验证方法

- 全部 14 个测试场景通过
- 可选：编写一个使用内存数据库的单元测试验证注入可行性

---

## 四、阶段执行顺序与依赖关系

```
阶段 1: 统一事务控制
  │        独立改造，无前置依赖
  ▼
阶段 2: 提取 TradingDayInitService
  │        独立改造，无前置依赖（可与阶段 1 并行）
  ▼
阶段 3: 统一 SettlementService 依赖注入
  │        独立改造，无前置依赖（可与阶段 1、2 并行）
  ▼
阶段 4: setFundConfig RAII 化
  │        独立改造（可与 1、2、3 并行）
  ▼
阶段 5: 消除策略级/账户级重复
  │        依赖阶段 2（rebuildContractStats 在 TradingDayInitService 中）
  │        依赖阶段 3（settleXxxFunds 在 SettlementService 中）
  ▼
阶段 6: QueryKitService 职责拆分
  │        独立改造（可与 1-5 并行）
  ▼
阶段 7: 依赖注入基础设施（长期）
           依赖阶段 1-3 完成
```

**推荐合入顺序**：1 → 2 → 3 → 4 → 5 → 6 → 7

阶段 1-4 相互独立，可并行开发，但建议按上述顺序逐个合入以控制风险。阶段 5 依赖 2 和 3 的产出。阶段 7 可长期推进。

---

## 五、改造后 service/ 文件清单（预期）

| 文件 | 行数（预估） | 职责 |
|------|------------|------|
| `om_service.h/.cc` | ~150 + ~300 | 系统生命周期、顶层编排、缓存管理 |
| `trading_day_init_service.h/.cc` | ~50 + ~150 | 交易日初始化（清理、重建、校验） |
| `settlement_service.h/.cc` | ~70 + ~600 | 日终结算（依赖全参数注入） |
| `combo_order_service.h/.cc` | ~80 + ~500 | 组合委托处理（不管理事务） |
| `query_kit_service.h/.cc` | ~110 + ~250 | 查询编排（业务逻辑下沉 core） |
| `service_factory.h/.cc` | ~100 + ~100 | Store/Processor 创建工厂 |

OmService 从 ~1013 行降至 ~450 行，减少约 55%。

---

## 六、风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 重构引入回归 bug | 高 | 每阶段完成后运行全部 14 个场景测试 |
| 阶段 5 模板化增加编译错误定位难度 | 低 | 模板仅在 .cc 文件中使用，不暴露到头文件 |
| 阶段 7 DbManager 引用替换范围广 | 中 | 可用全局搜索替换，但需逐一验证 |
| C++11 限制模板用法 | 低 | 项目已有 `om_compat.h` 兼容层，模板辅助函数使用 C++11 兼容写法 |

---

## 七、不纳入本次改造的项

以下问题已识别但不在本方案范围内，记录供后续参考：

| 项 | 原因 |
|----|------|
| ServiceStores 双轨制（P10） | 当前仅 QueryKit 路径使用，影响小，可在 QueryKit 重构时一并处理 |
| DbManager 本身重构为非单例 | 改动范围过大，留到阶段 7 之后评估 |
| data 层增加批量按 scope 查询接口 | 属于 data 层优化，阶段 6 仅新增 `queryByScope` 一个方法 |
| 引入接口（纯虚基类）实现 mock | C 风格项目引入虚函数有性能和风格考量，留待评估 |

---

## 修订记录

| 版本 | 日期 | 修改内容摘要 |
|------|------|------------|
| v1.0 | 2026-03-17 | 初始版本，识别 10 个问题，提出 7 阶段改造方案 |
