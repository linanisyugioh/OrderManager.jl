# 接口：对外 API（C 风格）

> 从 old_docs/architecture/overview.md §8 迁移  
> 定义在 `include/om_manager_api.h`、`include/om_hft_api.h`、`include/om_query.h`

---

## 1. 系统生命周期 API

### 1.1 om_init - 系统初始化

```cpp
/**
 * @brief 系统初始化
 * @param work_dir 工作目录路径（必须可写）
 * @return 0 成功；负数错误码（见 include/om_error.h）
 *
 * 初始化顺序：
 *   1. LogManager 初始化（work_dir/logs）
 *   2. DbManager 打开数据库（work_dir/om.db）
 *   3. 各 Store 创建表（createTable）
 *   4. 关闭数据库连接（不保留开启；om_trading_day_update 时会重新打开）
 */
int om_init(const char* work_dir);
```

**前置条件**：work_dir 必须存在且可写

**后置条件**：系统已初始化，可调用其他 API

**错误码**：OM_AlreadyInited(-9) 重复初始化时返回

---

### 1.2 om_release - 系统释放

```cpp
/**
 * @brief 释放全部资源
 *
 * 释放顺序（与初始化相反）：
 *   1. 释放 Processor 实例
 *   2. 关闭数据库（DbManager::close）
 *   3. 关闭日志（LogManager::shutdown）
 */
void om_release(void);
```

---

## 2. 交易日管理 API

### 2.1 om_trading_day_update - 交易日初始化

```cpp
/**
 * @brief 进入新交易日
 * @param trading_date 交易日（YYYYMMDD 格式）
 * @return 0 成功；负数错误码（OM_NotInited、OM_InvalidArg 等）
 *
 * 【FeeCodeInfo 说明】合约基础信息不再由此接口传入，而是通过 om_add_fee_info 逐个传入。
 * 建议流程：交易日初始化成功后，调用 om_set_query_scope 设置账户作用域，再调用 om_query_account_position_codes
 * 获取持仓 codes，遍历调用 om_add_fee_info 将各合约的 FeeCodeInfo 传入。
 *
 * 【缓存清空说明】本接口会自动清空以下内存缓存，确保跨交易日数据不混淆：
 *   - price_cache_：价格缓存（昨结算价、今结算价、最新价）
 *   - fee_info_cache_：合约费率缓存
 *
 * 流程：
 *   1. 关闭旧数据库连接，清理旧 Store 实例后重新打开数据库并重建各 Store/表
 *   2. 清空 price_cache_ 和 fee_info_cache_（合约信息需通过 om_add_fee_info 逐个传入）
 *   3. 清空 order 表、trade 表，删除已平仓持仓（close_date > 0），清理未配对组合持仓（combination_unit）
 *   4. 清空并重建 ContractStat（策略级 + 账户级）
 *   5. 账户级资金校验：账户级资金 >= 该账户下所有策略级资金聚合值（不通过则返回 OM_FundCheckFailed）
 */
int om_trading_day_update(int trading_date);
```

**前置条件**：已调用 om_init

**线程安全**：调用方保证不并发调用

---

### 2.1.1 om_add_fee_info - 添加合约基础信息

```cpp
/**
 * @brief 添加合约基础信息至缓存（供日终结算等使用）
 * @param fee_info 合约基础信息（合约乘数、保证金率等），手续费字段可留空
 * @return 0 成功；OM_NotInited 未初始化；OM_InvalidArg 参数非法（fee_info 为空或 code 为空）
 *
 * 可在 om_trading_day_update 成功后，按持仓 codes 遍历调用，逐个传入各合约的 FeeCodeInfo。
 * fee_info_cache_ 中必须包含所有持仓合约的基础信息，否则 om_trading_day_end 将返回 OM_MissingFeeInfo。
 */
int om_add_fee_info(const FeeCodeInfo* fee_info);
```

---

### 2.2 om_trading_day_end - 日终结算

```cpp
/**
 * @brief 日终结算
 * @return 0 成功；负数错误码
 *
 * **前置依赖**：
 *   - price_cache_ 中必须包含所有持仓合约的结算价（通过 om_handle_newprice 传入）
 *   - fee_info_cache_ 中必须包含所有持仓合约的基础信息（通过 om_add_fee_info 传入）
 *
 * **缓存清空说明**：本接口在结算完成后会自动清空 price_cache_ 和 fee_info_cache_，
 * 并关闭数据库连接。下一交易日需重新调用 om_trading_day_update 初始化。
 *
 * 流程（全流程在同一事务内，最后提交后关闭数据库并清理 Store）：
 *   0. 处理未终态委托：将 order 表中未终态委托按规则改为 CancelFilled/PartiallyCanceled，并调用 OrderProcessor::process 释放冻结资金/持仓
 *   0.5. 检测冻结资产：若仍存在冻结资金或冻结持仓则打 ERROR 日志（不阻断）
 *   1. 创建资金快照（策略级 fundtable_his + 账户级 account_fundtable_his）
 *   2. 前置检查：遍历所有持仓合约，校验结算价与费率缓存完整；缺失则回滚并返回 OM_MissingSettlementPrice / OM_MissingFeeInfo
 *   3. 策略级结算：按作用域逐手重算结算盈亏 → 对比验证 fund.pnl（超阈值记 OM_SettlementPnlMismatch，不阻断）→ 盈亏兑现到 avail_cash → 逐手更新持仓价和保证金 → 重算 equity → 更新 fundtable
 *   4. 账户级结算：按账户逐手重算结算盈亏 → 兑现到 account_cash → 逐手更新持仓价和保证金 → 重算 account_equity → 更新 accountfundtable
 *   5. 生成快照：当日 order、当日 trade、existed_flag=0 的 combination_unit 写入对应历史表
 *   6. 提交事务；关闭数据库连接并清理所有 Store 和内存缓存（本系统仅提供策略运行期间使用，日终后不再提供查询接口；需查数据可由调用方直接读数据库文件）
 */
int om_trading_day_end(void);
```

**错误码**：
- `OM_MissingSettlementPrice(-10)`：缺少结算价（回滚，未完成结算）
- `OM_MissingFeeInfo(-11)`：缺少费率信息（回滚，未完成结算）
- `OM_SettlementPnlMismatch(-12)`：结算盈亏与盘中浮动盈亏对比超差；结算流程仍会完成并关闭数据库，但返回本码

---

## 3. 业务处理 API

### 3.1 om_handle_order - 接收委托回报

```cpp
/**
 * @brief 处理委托回报（驱动 OmOrder→Position→Fund 全流程计算）
 * @param in_order 委托数据（全量传入）
 * @param fee_code_info 费率与合约参数
 * @return 0 成功；负数错误码
 *
 * **OmOrder 字段要求**：
 *   - 主键字段（6个）：必须有效
 *   - code, side, status, volume：必填
 *   - price：开仓时必填
 *   - market：平仓时必填
 *   - filled_volume/filled_turnover：成交时必填
 *   - frozen/fee/margin_ratio：系统维护，入参填0
 *
 * **事务控制**：本接口内部包裹事务
 *
 * **组合委托**：若 order.code 含 `&`（如 DCE.b2606&b2612），则走 handleCombinationOrder：
 *   - 有成交增量时，从 trade 表查询成交、校验、生成两腿委托分别 OrderProcessor::process，并配对创建 CombinationUnit
 *   - 原始组合委托直接 order_store upsert 入库，不经过 OrderProcessor::process
 */
int om_handle_order(OmOrder in_order, FeeCodeInfo fee_code_info);
```

**错误码**：
- `OM_InvalidArg(-1)`：参数无效
- `OrderProc_InvalidArg(-101)` / `OrderProc_FeeCodeInvalid(-102)` / `OrderProc_Internal(-103)` / `OrderProc_InvalidState(-104)` / `OrderProc_InvalidMarginRatio(-105)` / `OrderProc_InvalidExchange(-106)`
- `PositionProc_InvalidArg(-201)` / `PositionProc_NotFound(-202)` / `PositionProc_StoreError(-203)` / `PositionProc_InsufficientPosition(-204)` / `PositionProc_InvalidSideForMarket(-205)`
- `FundtableProc_InvalidArg(-301)` / `FundtableProc_StoreError(-302)` / `FundtableProc_NotFound(-303)`
- `OM_ComboLegCodeMismatch(-610)`：组合成交 code 与腿不匹配（组合委托）
- `OM_ComboLegVolumeMismatch(-611)`：两腿成交量不一致（组合委托）
- `OM_ComboTradeNotFound(-612)`：组合委托无对应成交
- `OM_ComboInvalidFormat(-613)`：组合委托格式非法（code 解析失败）

**详见**：`04-reference/order-fields.md`、`include/om_error.h`

---

### 3.2 om_handle_newprice - 接收行情

```cpp
/**
 * @brief 接收行情，刷新合约未平仓持仓浮动盈亏
 * @param code 合约代码
 * @param last_price 最新价（×10000）
 * @param pre_settlement_price 昨结算价（×10000）
 * @param settlement_price 今结算价（×10000），0表示盘中行情
 * @return 0 成功；负数错误码
 *
 * 【结算价逻辑】
 * 1. 若 settlement_price > 0，表示收盘结算，使用结算价计算盈亏变化
 *    - 有缓存最新价时：价差 = settlement_price - 缓存最新价
 *    - 无缓存最新价时：价差 = settlement_price - pre_settlement_price
 * 2. 若 settlement_price == 0，表示盘中行情，使用最新价计算盈亏变化
 *    - 有缓存最新价时：价差 = last_price - 缓存最新价
 *    - 无缓存最新价时：价差 = last_price - pre_settlement_price
 * 3. 若缓存中已有 settlement_price > 0，直接返回，代表当天已终态
 *
 * 流程：
 *   1. 检查缓存中是否已有结算价（当天已终态判断）
 *   2. 判断结算价有效性，确定使用结算价还是最新价作为基准
 *   3. PositionProcessor::calcPnlDeltaByContractStat - 策略级盈亏计算
 *   4. FundtableProcessor::batchUpdatePnl - 策略级资金更新
 *   5. AccountPositionProcessor::calcPnlDeltaByContractStat - 账户级盈亏计算
 *   6. AccountFundtableProcessor::batchUpdatePnl - 账户级资金更新
 *   7. 更新 price_cache_（保存最新价、昨结算价、今结算价）
 *
 * **事务控制**：本接口内部包裹事务
 */
int om_handle_newprice(const char* code, int64_t last_price,
                       int64_t pre_settlement_price, int64_t settlement_price);
```

---

### 3.3 om_handle_trade - 接收成交回报

```cpp
/**
 * @brief 接收成交回报，写入成交记录至 trade 表
 * @param trade 成交数据（按值传入）
 * @return 0 成功；负数错误码
 *
 * **当前实现**：仅校验字段后直接入库，不参与持仓计算和资金计算。详见 trade-flow.md §1.1。
 *
 * **OmTrade 字段要求**：
 *   - 主键字段（7个）：order_id, trade_date, strategy_id, run_id, account_id, account_type, match_seqno
 *   - code, side, volume, price：必填
 *   - filled_turnover, fee：用于记录
 *   - order_volume, order_price, slippage：用于记录原始委托信息
 *
 * **与 om_handle_order 区别**：
 *   - om_handle_order：委托状态驱动，处理 PendingNew → Filled 全流程
 *   - om_handle_trade：成交回报驱动，直接写入成交明细
 *
 * **适用场景**：
 *   - 从交易所/柜台直接接收成交回报时
 *   - 需要精确记录每笔成交明细时
 *   - 成交回报与委托回报分离的场景
 *
 * **组合委托时序**：组合委托（code 含 `&`）场景下，**需由调用方确保**成交回报先于委托回报推送，
 *   本系统不做时序处理或降级。om_handle_trade 写入成交后，om_handle_order 才能基于 trade 表执行拆腿。详见 `../flows/combo-order-leg-split.md` §2。
 *
 * **事务控制**：本接口内部包裹事务
 */
int om_handle_trade(OmTrade trade);
```

**错误码**：
- `OM_InvalidArg(-1)`：参数无效
- `TradeProc_InvalidArg(-601)`：OmTrade 参数无效
- `TradeProc_NotFound(-602)`：关联委托不存在（**当前未使用，扩展预留**）
- `TradeProc_StoreError(-603)`：成交存储错误
- `TradeProc_DuplicateKey(-604)`：成交主键重复

**详见**：`02-domain/trade-lifecycle.md`

---

## 4. 配置 API

### 4.1 om_set_fund_config - 写入策略级资金配置

```cpp
/**
 * @brief 写入策略级资金记录（初始化资金）
 * @param fund 资金配置数据（策略级，含 strategy_id）
 * @return 0 成功；负数错误码
 *
 * **约束**：
 *   - 同主键记录不存在时才插入
 *   - 若已存在返回 FundtableStore_DupKey(-2)
 *
 * **调用时机**：交易日开始前，为每个策略账户初始化资金
 *
 * **注意**：本接口仅创建策略级资金记录，账户级资金需单独调用 om_set_account_fund_config
 */
int om_set_fund_config(const Fundtable* fund);
```

**前置条件**：已调用 om_init

---

### 4.2 om_set_account_fund_config - 写入账户级资金配置

```cpp
/**
 * @brief 写入账户级资金记录（初始化资金）
 * @param fund 账户级资金配置数据（不含 strategy_id，跨策略汇总）
 * @return 0 成功；负数错误码
 *
 * **约束**：
 *   - 同主键记录不存在时才插入
 *   - 若已存在返回 AccountFundtableStore_DupKey(-474)
 *
 * **调用时机**：交易日开始前，为每个账户初始化资金
 *
 * **与策略级资金关系**：
 *   - 账户级资金 >= 该账户下所有策略级资金之和（校验逻辑在 tradingDayUpdate）
 *   - 可用资金、保证金、权益均需满足此约束
 *
 * **示例**：
 *   - 策略A: avail_cash=500万, margin=100万, equity=600万
 *   - 策略B: avail_cash=300万, margin=50万, equity=350万
 *   - 账户级: account_cash>=800万, account_margin>=150万, account_equity>=950万
 */
int om_set_account_fund_config(const AccountFundtable* fund);
```

**前置条件**：已调用 om_init

**错误码**：
- `AccountFundtableStore_DupKey(-474)`：该账户级资金记录已存在

---

## 5. HFT 适配 API（`include/om_hft_api.h`）

适用于接入 HFT（High-Frequency Trading）系统的场景，入参使用 `hft_structs.h` 中的 `HftOrder`/`HftTrade`/`HftCodeInfo` 结构体。

### 5.0 om_add_fee_info_hft - 添加 HFT 合约基础信息

```cpp
/**
 * @brief 添加合约基础信息至缓存（HFT 适配，对应 om_add_fee_info）
 * 将 HftCodeInfo 转换为 FeeCodeInfo 后调用 om_add_fee_info。
 *
 * @param hft_code_info HFT 合约信息（symbol、乘数、保证金率、手续费等）
 * @return OM_Ok 成功；OM_NotInited 未初始化；OM_InvalidArg 参数非法
 */
int om_add_fee_info_hft(const HftCodeInfo* hft_code_info);
```

### 5.1 om_handle_order_hft - 接收 HFT 委托回报

```cpp
/**
 * @brief 接收 HFT 委托回报，驱动 Order → Position → Fundtable 业务流程
 * @param hft_order HFT 委托结构指针（HftOrder）
 * @param hft_code_info 与该委托标的（symbol）对应的 HFT 合约信息（手续费、保证金等）
 * @return 同 om_handle_order，0 成功，否则为错误码
 *
 * 转换规则：
 *   - HFT symbol（市场.合约ID）→ OM code
 *   - 从 symbol 解析市场前缀映射为 Exchange 枚举
 *   - 从 symbol 解析品种填入 product
 *   - order_status→status, order_type→order_type, side→side（枚举值一致）
 *   - hft_code_info 内部转换为 FeeCodeInfo 后调用 om_handle_order
 */
int om_handle_order_hft(const HftOrder* hft_order, const HftCodeInfo* hft_code_info);
```

**等价于**：`om_handle_order`，仅入参为 HFT 结构。

---

### 5.3 om_handle_trade_hft - 接收 HFT 成交回报

```cpp
/**
 * @brief 接收 HFT 成交回报，写入成交记录至 trade 表
 * @param hft_trade HFT 成交结构指针（HftTrade）
 * @return 同 om_handle_trade，0 成功，否则为错误码
 *
 * 转换规则：
 *   - HFT exec_id → OM match_seqno
 *   - HFT exec_type → OM match_type
 *   - HFT turnover → OM filled_turnover
 *   - transact_time：HFT 微秒 → OM 毫秒（/1000）
 */
int om_handle_trade_hft(const HftTrade* hft_trade);
```

**等价于**：`om_handle_trade`，仅入参为 HFT 结构。

**详见**：`include/hft_structs.h`

---

## 6. 查询 API（`include/om_query.h`）

简化版单条查询，使用系统缓存的 `run_id`/`account_id`/`account_type` 作为查询作用域，避免每次传入。**使用前需先调用 `om_set_query_scope` 设置作用域**。

> **实现说明**：查询接口内部使用 `QueryKitPool`（查询套件池）获取独立的 SQLite 连接，与写入操作完全隔离，支持多线程并发查询。套件池在交易日初始化时创建，默认包含5个独立连接。

### 6.1 om_set_query_scope - 设置查询作用域

```cpp
/**
 * @brief 设置查询作用域（run_id, account_id, account_type）
 * @param run_id      实例ID（非空）
 * @param account_id  账户ID（非空）
 * @param account_type 账户类型
 * @return 0成功；OM_NotInited未初始化；OM_InvalidArg参数非法
 */
int om_set_query_scope(const char* run_id, const char* account_id, int32_t account_type);
```

### 6.2 om_get_query_run_id / om_get_query_account_id / om_get_query_account_type - 获取当前作用域

```cpp
const char* om_get_query_run_id(void);
const char* om_get_query_account_id(void);
int32_t om_get_query_account_type(void);
```

**未初始化时**：未调用 om_init 或已 om_release 时，om_get_query_run_id / om_get_query_account_id 返回 NULL，om_get_query_account_type 返回 0。

### 6.3 om_query_order - 按主键查询单条委托

```cpp
int om_query_order(const char* order_id, int32_t oper_date,
                   const char* strategy_id, OmOrder* out);
```

**返回**：0成功；OM_NotInited；OM_InvalidArg；OrderStore_NotFound 未找到

### 6.4 om_query_order_ids - 查询委托 ID 列表

```cpp
/**
 * 在 service 内按 strategy_id 维护 order_id 列表字符串缓存（逗号分隔）。
 * 每次查询将结果写入该策略的缓存，通过 out_order_ids 返回 string.c_str()。
 * 返回格式：order_id 之间用 "," 分割，无数据时为空字符串。
 * 指针在下次对同一 strategy_id 的查询或 om_release 前有效。调用方使用 strlen() 可获取字符串长度。
 *
 * @param strategy_id    策略ID（非空）
 * @param status         0=未终态委托，1=已终态，2=所有状态
 * @param code           合约代码，空指针表示所有合约
 * @param side           0=平，1=开，3=全部
 * @param bs             0=空，1=多，3=全部
 * @param out_order_ids  输出，成功时指向该策略的缓存字符串（不能为空指针）
 */
int om_query_order_ids(const char* strategy_id, int status,
                       const char* code, int side, int bs,
                       const char** out_order_ids);
```

### 6.5 om_query_position_codes - 查询持仓 code 列表

```cpp
/**
 * 查询 strategy_id 下持仓的 code 列表（使用缓存作用域）。
 * 返回逗号分隔的 code 字符串，无数据时为空字符串。调用方使用 strlen() 可获取长度。
 *
 * @param strategy_id  策略ID（非空）
 * @param status       0=冻结，1=可用，2=全部
 * @param period       0=昨仓，1=今仓，2=全部
 * @param side         0=空，1=多，2=全部
 * @param out_codes    输出，成功时指向缓存字符串（不能为空指针）
 */
int om_query_position_codes(const char* strategy_id, int status, int period, int side,
                           const char** out_codes);
```

### 6.5.1 om_query_account_position_codes - 查询账户级持仓 code 列表

```cpp
/**
 * 查询账户下未平仓持仓的 code 列表（使用缓存作用域）。
 * 用于交易日初始化后获取持仓 codes，遍历调用 om_add_fee_info 传入各合约 FeeCodeInfo。
 * @param out_codes 输出，成功时指向缓存的逗号分隔 code 字符串（不能为空指针）
 */
int om_query_account_position_codes(const char** out_codes);
```

**错误码**：0 成功；OM_NotInited；OM_InvalidArg

### 6.6 om_query_contract_stat - 查询合约统计（策略级）

```cpp
int om_query_contract_stat(const char* strategy_id, const char* code,
                           ContractStat* out);
```

**错误码**：ContractStatStore_NotFound(-423) 未找到

### 6.7 om_query_account_contract_stat - 查询账户级合约统计

```cpp
int om_query_account_contract_stat(const char* code, AccountContractStat* out);
```

**错误码**：AccountContractStatStore_NotFound(-513) 未找到

### 6.8 om_query_fund - 查询策略级资金

```cpp
int om_query_fund(const char* strategy_id, Fundtable* out);
```

**错误码**：FundtableStore_NotFound(-433) 未找到

### 6.9 om_query_account_fund - 查询账户级资金

```cpp
int om_query_account_fund(AccountFundtable* out);
```

**错误码**：AccountFundtableStore_NotFound(-473) 未找到

**说明**：当前对外查询能力以 `om_query.h` 为准，其余 OmService 内部查询方法已移除。

---

## 7. QueryKitService 实现详解

### 7.1 类职责与设计

`QueryKitService` 是查询服务的统一入口，封装了查询套件池（QueryKitPool）的生命周期管理和所有查询接口的实现。

**核心职责**：
1. **生命周期管理**：init/release QueryKitPool
2. **作用域缓存**：缓存 run_id/account_id/account_type，支持简化版接口
3. **查询接口实现**：完整参数版 + 简化版
4. **并发安全**：通过 QueryKitPool 获取独立连接，支持多线程并发

### 7.2 生命周期管理

**主要接口**：
- init(db_path)：初始化查询套件池，创建独立 SQLite 连接池
- release()：释放查询套件池，关闭所有连接
- isInited()：检查是否已初始化

**调用时序**：
```
OmService::tradingDayUpdate()
    ├── 主体初始化（Store/Processor）
    ├── QueryKitService::instance().init(db_path)  // 初始化查询套件池
    └── 返回

OmService::tradingDayEnd() / OmService::release()
    ├── ...
    └── QueryKitService::instance().release()  // 释放查询套件池
```

### 7.3 查询作用域管理

**作用域缓存**：
- setQueryScope(run_id, account_id, account_type)：设置查询作用域参数
- getQueryRunId / getQueryAccountId / getQueryAccountType：获取缓存值

**缓存成员**：query_run_id_, query_account_id_, query_account_type_

### 7.4 查询接口分类

**完整参数接口**：需传入完整 scope（run_id/account_id/account_type/strategy_id）
- queryOrderById / queryOrdersByScope
- queryContractStat / queryAccountContractStat
- queryFund / queryAccountFund

**简化版接口**：使用 setQueryScope 缓存的作用域，减少参数
- queryOrderByIdSimple / queryOrderIdsSimple
- queryPositionCodesSimple / queryAccountPositionCodesSimple
- queryContractStatSimple / queryAccountContractStatSimple
- queryFundSimple / queryAccountFundSimple

**实现模式**：
```
1. 从 QueryKitPool 获取独立 SQLite 连接（QueryKit）
2. 调用对应 Store 的查询方法
3. 归还 QueryKit 到连接池
4. 简化版接口自动追加缓存的作用域参数
```

### 7.5 关键设计特点

**读写隔离**：
- 查询使用 QueryKitPool 提供的独立 SQLite 连接
- 与写入操作完全隔离，不阻塞交易流程
- 支持多线程并发查询（默认5个连接）

**作用域缓存**：
- setQueryScope 缓存 run_id/account_id/account_type
- 简化版接口自动使用缓存，减少参数传递

**查询结果缓存**：
- queryOrderIdsSimple / queryPositionCodesSimple 结果缓存
- 按查询参数组合作为 key
- release() 时统一清空

---

## 8. 数据结构索引

### 7.1 核心结构体

| 结构体 | 用途 | 主键 | 持久化 |
|--------|------|------|--------|
| `OmOrder` | 委托记录 | 6字段联合 | 是（order 表） |
| `OmTrade` | 成交记录 | 7字段联合 | 是（trade 表） |
| `PositionUnit` | 策略级持仓单元（每手） | id 自增 | 是（position_unit 表） |
| `AccountPositionUnit` | 账户级持仓单元 | id 自增 | 是（account_position_unit 表） |
| `Fundtable` | 策略级资金 | 4字段联合 | 是（fundtable 表） |
| `AccountFundtable` | 账户级资金 | 3字段联合 | 是（accountfundtable 表） |
| `FeeCodeInfo` | 费率+合约参数 | code | 否（内存使用） |
| `ContractStat` | 合约统计 | 5字段联合 | 是（contract_stat 表） |

### 7.2 核心枚举

| 枚举 | 用途 | 关键值 |
|------|------|--------|
| `OrderStatus` | 委托状态 | Filled(4), CancelFilled(7), PartiallyCanceled(8), Rejected(9) |
| `OrderSide` | 买卖方向 | Long_Open(3), Short_Open(5), Long_Close(4), Short_Close(6) |
| `PositionSide` | 持仓方向 | Long(1), Short(2) |
| `Exchange` | 交易所 | SHFE(1), DCE(2), CZCE(3), CFFEX(4), INE(5), GFEX(6) |

**详见**：`00-overview/quick-reference.md` §枚举值速查

---

## 9. 错误码汇总

### 9.1 通用错误码（-1 ~ -99）

| 错误码 | 值 | 说明 |
|--------|-----|------|
| OM_Ok | 0 | 成功 |
| OM_InvalidArg | -1 | 参数无效 |
| OM_NotInited | -8 | 未初始化 |
| OM_AlreadyInited | -9 | 重复初始化 |
| OM_MissingSettlementPrice | -10 | 日终结算时缺少结算价 |
| OM_MissingFeeInfo | -11 | 日终结算时缺少费率信息 |
| OM_SettlementPnlMismatch | -12 | 日终结算盈亏对比不一致 |
| OM_FundCheckFailed | -13 | 交易日初始化时账户级资金校验失败（低于策略级聚合值） |

### 9.2 模块错误码（详见 `03-implementation/interfaces/processor-apis.md`）

| 范围 | 所属模块 |
|------|---------|
| -100 ~ -199 | OrderProcessor |
| -200 ~ -299 | PositionProcessor |
| -300 ~ -399 | FundtableProcessor |
| -400 ~ -579 | Store 层（含各 Store、账户级 Processor、历史表 Store、DbManager）|
| -600 ~ -699 | TradeProcessor 及组合委托处理层 |

---

## 10. 典型调用时序

### 10.1 交易日完整流程

```
om_init(work_dir)
    ↓
om_set_fund_config(fund)          // 初始化策略级资金
    ↓
om_set_account_fund_config(fund)  // 初始化账户级资金（跨策略汇总）
    ↓
om_trading_day_update(date)  // 交易日初始化
    │  // 内部校验：账户级资金 >= 策略级资金聚合值
    │  // 合约信息通过 om_add_fee_info 逐个传入（按持仓 codes 遍历）
    ↓
【盘中交易循环】
  ├─ om_handle_order(order, fee_info)  // 委托处理（策略级+账户级同步更新）
  ├─ om_handle_newprice(code, price)   // 行情刷新（策略级+账户级同步更新）
  ├─ om_set_query_scope(run_id, account_id, account_type)  // 可选：设置查询作用域
  ├─ om_query_order / om_query_fund / ...  // 可选：状态查询
  └─ ...
    ↓
om_handle_newprice(code, settlement_price)  // 传入结算价
    ↓
om_trading_day_end()  // 日终结算（策略级+账户级资金快照）
    ↓
【进入下一交易日】
    ↓
om_release()  // 系统关闭
```

---

## 11. 相关文档

| 主题 | 文档位置 | 层级 | 说明 |
|------|---------|------|------|
| **委托处理流程** | [`03-implementation/flows/order-flow.md`](../flows/order-flow.md) | L3 | 委托处理内部流程 |
| **成交处理流程** | [`03-implementation/flows/trade-flow.md`](../flows/trade-flow.md) | L3 | 成交处理流程 |
| **行情刷新流程** | [`03-implementation/flows/newprice-flow.md`](../flows/newprice-flow.md) | L3 | 行情驱动盈亏刷新 |
| **日终结算流程** | [`03-implementation/flows/settlement-flow.md`](../flows/settlement-flow.md) | L3 | 结算详细步骤 |
| **订单模型** | [`02-domain/order-lifecycle.md`](../../02-domain/order-lifecycle.md) | L2 | OmOrder 状态机与字段 |
| **资金模型** | [`02-domain/fund-model.md`](../../02-domain/fund-model.md) | L2 | 资金计算规则 |
| **Processor接口** | [`03-implementation/interfaces/processor-apis.md`](./processor-apis.md) | L3 | 内部 Processor 方法 |
| **字段要求** | [`04-reference/order-fields.md`](../../04-reference/order-fields.md) | L4 | 入参字段校验规则 |
| **枚举速查** | [`00-overview/quick-reference.md`](../../00-overview/quick-reference.md) | L0 | 枚举值/错误码速查 |
| **系统架构** | [`01-architecture/system-overview.md`](../../01-architecture/system-overview.md) | L1 | 模块职责与API总览 |
