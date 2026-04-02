# 快速参考手册

> 关键公式、枚举值、错误码速查表

---

## 1. 核心计算公式

### 1.1 保证金计算

```cpp
// 单手保证金（扩大一万倍）
margin_per_lot = price × multiply × margin_ratio / 10000

// 示例：price=5000000(500元), multiply=1, margin_ratio=1200(12%)
// margin = 5000000 × 1 × 1200 / 10000 = 600000 (60元)
```

### 1.2 手续费计算

```cpp
// 按金额计费：rate已扩大10万倍存储（如万1.5存为15）
fee_per_lot = price × multiply × rate / 100000

// 按手数计费：rate为每手费用（扩大10万倍存储）
fee_per_lot = rate / 10

// 示例：price=5000000, multiply=1, rate=15(万1.5)
// fee = 5000000 × 1 × 15 / 100000 = 750 (0.075元)
```

### 1.3 盈亏计算

```cpp
// 方向系数
dir_sign = (direction == PositionSide_Long) ? +1 : -1

// 单手浮动盈亏（扩大一万倍）
// 说明：last_price、hold_cost 已×10000，乘积 (last_price-hold_cost)×multiply×dir_sign 即已为×10000的金额，无需再除
float_pnl = (last_price - hold_cost) × multiply × dir_sign

// 单手实现盈亏（平仓时）
realized_pnl = (close_price - hold_cost) × multiply × dir_sign
```

### 1.4 权益计算

```cpp
// 总权益 = 保证金 + 可用资金 + 冻结资金 + 浮动盈亏
equity = margin + avail_cash + frozen_cash + pnl

// 注意：fee（累计手续费）已包含在avail_cash的历史变化中，不再重复扣减
```

### 1.5 冻结估算

```cpp
// 开仓委托冻结（单手）
frozen_per_lot = margin_per_lot + fee_per_lot
// 取今仓费率做保守估算

// 总冻结
frozen_total = frozen_per_lot × volume
```

---

## 2. 枚举值速查

### 2.1 OrderSide（买卖方向）

| 值 | 枚举名 | 用途 | isOpen | isTodayClose | isPreDayClose |
|----|--------|------|--------|--------------|---------------|
| 3 | Long_Open | 多头开仓 | ✓ | ✗ | ✗ |
| 5 | Short_Open | 空头开仓 | ✓ | ✗ | ✗ |
| 4 | Long_Close | 多头平仓 | ✗ | ✗ | ✗ |
| 6 | Short_Close | 空头平仓 | ✗ | ✗ | ✗ |
| 8 | Today_Long_Close | 平今多头 | ✗ | ✓ | ✗ |
| 10 | Today_Short_Close | 平今空头 | ✗ | ✓ | ✗ |
| 11 | PreDay_Long_Close | 平昨多头 | ✗ | ✗ | ✓ |
| 12 | PreDay_Short_Close | 平昨空头 | ✗ | ✗ | ✓ |

### 2.2 OrderStatus（委托状态）

**与 include/om_def.h 一致。**

| 值 | 状态 | 说明 | 是否终态 |
|----|------|------|----------|
| 1 | PendingNew | 待报 | ✗ |
| 2 | New | 已报 | ✗ |
| 3 | PartiallyFilled | 部成 | ✗ |
| 4 | Filled | 全成 | ✓ |
| 5 | PendingCancel | 撤单待报 | ✗ |
| 6 | Canceling | 已报待撤 | ✗ |
| 7 | CancelFilled | 全撤 | ✓ |
| 8 | PartiallyCanceled | 部成部撤 | ✓ |
| 9 | Rejected | 废单 | ✓ |

### 2.3 PositionSide（持仓方向）

**与 include/om_def.h 一致。**

| 值 | 枚举名 | 方向系数 | 说明 |
|----|--------|----------|------|
| 1 | Long | +1 | 多仓 |
| 2 | Short | -1 | 空仓 |
| 3 | Short_Covered | -1 | 备兑空仓（期权专用） |

### 2.4 Exchange（交易所）

| 值 | 交易所 | 平仓要求 |
|----|--------|----------|
| 1 | SHFE | 必须指定平今/平昨 |
| 2 | DCE | 不区分今昨 |
| 3 | CZCE | 不区分今昨 |
| 4 | CFFEX | 不区分今昨 |
| 5 | INE | 必须指定平今/平昨 |
| 6 | GFEX | 不区分今昨 |

---

## 3. 错误码速查

### 3.1 通用错误码（-1 ~ -99）

| 错误码 | 值 | 说明 |
|--------|-----|------|
| OM_Ok | 0 | 成功 |
| OM_InvalidArg | -1 | 参数无效 |
| FundtableStore_DupKey | -2 | fundtable 主键重复 |
| OM_NotInited | -8 | 未初始化 |
| OM_AlreadyInited | -9 | 重复初始化 |
| OM_MissingSettlementPrice | -10 | 日终结算缺少合约结算价 |
| OM_MissingFeeInfo | -11 | 日终结算缺少合约费率缓存 |
| OM_SettlementPnlMismatch | -12 | 日终结算盈亏与盘中不一致 |
| OM_FundCheckFailed | -13 | 交易日初始化账户级资金校验失败 |

### 3.2 OrderProcessor错误码（-100 ~ -199）

| 错误码 | 值 | 说明 |
|--------|-----|------|
| OrderProc_InvalidArg | -101 | 参数为空 |
| OrderProc_FeeCodeInvalid | -102 | 费率信息无效 |
| OrderProc_Internal | -103 | 内部逻辑错误（不应出现的状态） |
| OrderProc_InvalidState | -104 | 增量计算异常 |
| OrderProc_InvalidMarginRatio | -105 | 保证金率无效 |
| OrderProc_InvalidExchange | -106 | 交易所与平仓方向组合非法 |

### 3.3 PositionProcessor错误码（-200 ~ -299）

| 错误码 | 值 | 说明 |
|--------|-----|------|
| PositionProc_InsufficientPosition | -204 | 持仓不足 |
| PositionProc_InvalidSideForMarket | -205 | 交易所与方向组合非法 |

### 3.4 FundtableProcessor错误码（-300 ~ -399）

| 错误码 | 值 | 说明 |
|--------|-----|------|
| FundtableProc_NotFound | -303 | 资金记录不存在 |

### 3.5 Store层错误码（-400 ~ -579）

| 错误码 | 值 | 说明 |
|--------|-----|------|
| OrderStore_InvalidArg | -401 | 参数非法 |
| OrderStore_SqlError | -402 | SQL 执行错误 |
| OrderStore_NotFound | -403 | 按主键未找到委托（om_query_order） |
| PositionUnitStore_NotFound | -413 | 持仓单元不存在 |
| ContractStatStore_NotFound | -423 | 合约统计不存在 |
| FundtableStore_NotFound | -433 | 资金记录不存在 |
| TradeStore_InvalidArg | -461 | OmTrade 参数无效 |
| TradeStore_SqlError | -462 | SQL 执行错误 |
| TradeStore_DupKey | -464 | OmTrade 主键重复 |
| AccountFundtableStore_NotFound | -473 | 账户资金记录不存在 |
| AccountContractStatStore_NotFound | -513 | 账户级合约统计不存在（om_query_account_contract_stat） |
| AccountFundtableProc_NotFound | -493 | 账户资金记录不存在 |
| AccountPositionProc_InsufficientPosition | -524 | 账户级持仓不足 |
| CombinationUnitStore_InvalidArg | -531 | 组合持仓参数无效 |
| OrderHisStore_InvalidArg | -561 | OrderHis 参数无效 |
| TradeHisStore_InvalidArg | -571 | TradeHis 参数无效 |

### 3.6 数据库管理层（-580 ~ -589）

| 错误码 | 值 | 说明 |
|--------|-----|------|
| DbManager_OpenFailed | -581 | 数据库文件打开/创建失败 |
| DbManager_TxError | -582 | 事务操作失败（BEGIN/COMMIT） |

### 3.7 TradeProcessor错误码（-600 ~ -699）

| 错误码 | 值 | 说明 |
|--------|-----|------|
| TradeProc_InvalidArg | -601 | OmTrade 参数非法 |
| TradeProc_NotFound | -602 | 关联委托不存在（当前未使用，扩展预留） |
| TradeProc_StoreError | -603 | 入库阶段数据库操作失败 |
| TradeProc_DuplicateKey | -604 | OmTrade 主键重复 |
| OM_ComboLegCodeMismatch | -610 | 组合腿合约代码不匹配 |
| OM_ComboLegVolumeMismatch | -611 | 组合两腿成交量不一致 |
| OM_ComboTradeNotFound | -612 | 组合委托无对应成交 |
| OM_ComboInvalidFormat | -613 | 组合委托格式非法 |

---

## 4. 数值精度约定

| 字段类型 | 存储类型 | 精度 | 示例 |
|---------|---------|------|------|
| 价格(price, hold_cost) | int64_t | 扩大10000倍 | 3500.25 → 35002500 |
| 金额(margin, fee, pnl) | int64_t | 扩大10000倍 | 100000.00 → 1000000000 |
| 保证金率 | int32_t | 扩大10000倍 | 0.12 → 1200 |
| 手续费率 | int32_t | 扩大100000倍 | 0.00015 → 15 |
| 数量(volume) | int32_t | 原值 | 10 → 10 |
| 日期(oper_date) | int32_t | YYYYMMDD | 20260310 |
| 时间(update_time) | int64_t | HHMMSSmmm | 143025500 |

---

## 5. 常用SQL速查

### 5.1 查询未平仓持仓

```sql
-- 策略级未平仓
SELECT * FROM position_unit 
WHERE run_id=? AND account_id=? AND account_type=? AND strategy_id=?
  AND code=? AND direction=? AND close_date=0
ORDER BY open_date ASC, open_time ASC;

-- 账户级未平仓
SELECT * FROM account_position_unit 
WHERE run_id=? AND account_id=? AND account_type=?
  AND code=? AND direction=? AND close_date=0;
```

### 5.2 查询资金状态

```sql
-- 策略级资金
SELECT * FROM fundtable 
WHERE run_id=? AND account_id=? AND account_type=? AND strategy_id=?;

-- 账户级资金
SELECT * FROM accountfundtable 
WHERE run_id=? AND account_id=? AND account_type=?;
```

---

## 6. 命名规范速查

| 对象 | 风格 | 示例 |
|------|------|------|
| 类名 | PascalCase | OrderProcessor |
| 函数(C++) | camelCase | handleOrder |
| 函数(C API) | snake_case + om_前缀 | om_handle_order |
| 变量/成员 | snake_case | filled_volume |
| 成员变量 | snake_case + 下划线结尾 | order_store_ |
| 枚举类型 | PascalCase | OrderStatus |
| 枚举值 | PascalCase + 下划线 | OrderStatus_Filled |
| 宏 | 全大写 + 下划线 | OM_API |

---

## 7. 文件组织速查

| 层级 | 路径 | 内容 |
|------|------|------|
| include/ | om_data_types.h | 结构体定义 |
| include/ | om_manager_api.h | 对外API（核心） |
| include/ | om_hft_api.h | HFT 适配 API |
| include/ | om_query.h | 查询 API |
| include/ | hft_structs.h | HFT 结构体（HftOrder/HftTrade/HftCodeInfo） |
| include/ | om_error.h | 错误码 |
| common/ | om_compat.h | C++11 兼容层（内部） |
| core/ | *_processor.h/.cc | 核心计算 |
| data/ | *_store.h/.cc | 数据持久化 |
| kit/ | query_kit*.h/.cc | 套件模块（查询套件池等，可扩展其他套件） |
| service/ | om_service.h/.cc | 业务编排 |
| test/ | test_scenario*.h | 测试场景 |
