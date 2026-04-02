# OmOrder 字段要求

> OmOrder 结构体字段详细要求与校验规则
>
> 本文档定义 OmOrder 结构体中各字段的必填要求、取值范围、校验规则

---

## 1. 字段分类总览

| 分类 | 字段数量 | 说明 |
|------|----------|------|
| 主键字段（必填） | 6 | 联合唯一标识一条委托记录 |
| 业务必填字段 | 5-7 | 业务处理必需字段，根据开平仓类型有所不同 |
| 系统计算字段 | 3 | 由系统维护，入参应为0 |
| 成交相关字段 | 3 | 成交时填写，用于计算trade_price |
| 可选字段 | 10+ | 根据业务场景可选填写的字段 |

---

## 2. 主键字段（6字段联合主键）

| 字段名 | 类型 | 长度 | 必填 | 说明 | 校验规则 |
|--------|------|------|------|------|----------|
| `order_id` | char[] | LEN_ID(64) | ✓ | 委托唯一标识 | strlen > 0，不能为NULL或空字符串 |
| `oper_date` | int32 | - | ✓ | 委托日期 | YYYYMMDD格式，必须与当前交易日一致 |
| `strategy_id` | char[] | LEN_CODE(32) | ✓ | 策略ID | strlen > 0，影响资金/持仓作用域定位 |
| `run_id` | char[] | LEN_ID(64) | ✓ | 实例ID | strlen > 0，影响资金/持仓作用域定位 |
| `account_id` | char[] | LEN_ACCOUNT_ID(64) | ✓ | 账户ID | strlen > 0，影响资金/持仓作用域定位 |
| `account_type` | int32 | - | ✓ | 账户类型 | > 0，影响资金/持仓作用域定位 |

**作用域（Scope）概念**：4维度定位资金/持仓
- `run_id` + `account_id` + `account_type` + `strategy_id`
- 账户级数据不含 `strategy_id` 维度

---

## 3. 业务必填字段

### 3.1 通用必填（所有委托）

| 字段名 | 类型 | 必填 | 说明 | 校验规则 |
|--------|------|------|------|----------|
| `code` | char[] | ✓ | 合约代码 | strlen > 0，应与 FeeCodeInfo.code 一致 |
| `side` | int32 | ✓ | 买卖方向 | 必须是合法 OrderSide 枚举值（见下表） |
| `status` | int32 | ✓ | 委托状态 | OrderStatus 枚举值 |
| `volume` | int32 | ✓ | 委托数量 | > 0 |

### 3.2 开仓必填

| 字段名 | 类型 | 必填 | 说明 | 校验规则 |
|--------|------|------|------|----------|
| `price` | int64 | ✓ | 委托价格 | > 0，扩大一万倍存储（如500元存为5000000） |

### 3.3 平仓必填

| 字段名 | 类型 | 必填 | 说明 | 校验规则 |
|--------|------|------|------|----------|
| `market` | int32 | ✓ | 交易所 | 1-6有效枚举（SHFE=1, DCE=2, CZCE=3, CFFEX=4, INE=5, GFEX=6） |

**注意**：平仓时 `market` + `side` 组合必须合法
- SHFE, INE：必须使用 Today_*_Close 或 PreDay_*_Close
- DCE, CZCE, CFFEX, GFEX：使用 Long_Close 或 Short_Close

---

## 4. OrderSide 枚举值

| 值 | 枚举名 | 中文名 | 类型 | 持仓方向 | 说明 |
|----|--------|--------|------|----------|------|
| 3 | Long_Open | 多头开仓 | 开仓 | 多头 | 开多仓 |
| 5 | Short_Open | 空头开仓 | 开仓 | 空头 | 开空仓 |
| 4 | Long_Close | 多头平仓 | 平仓 | 多头 | 平多仓（不分今昨，非SHFE/INE） |
| 6 | Short_Close | 空头平仓 | 平仓 | 空头 | 平空仓（不分今昨，非SHFE/INE） |
| 8 | Today_Long_Close | 平今多头 | 平仓 | 多头 | 平今多仓（SHFE/INE专用） |
| 10 | Today_Short_Close | 平今空头 | 平仓 | 空头 | 平今空仓（SHFE/INE专用） |
| 11 | PreDay_Long_Close | 平昨多头 | 平仓 | 多头 | 平昨多仓（SHFE/INE专用） |
| 12 | PreDay_Short_Close | 平昨空头 | 平仓 | 空头 | 平昨空仓（SHFE/INE专用） |

---

## 5. OrderStatus 枚举值

| 值 | 枚举名 | 中文名 | 是否终态 | 说明 |
|----|--------|--------|----------|------|
| 1 | PendingNew | 待报 | ✗ | 已接收待发送交易所 |
| 2 | New | 已报 | ✗ | 已发送到交易所 |
| 3 | PartiallyFilled | 部成 | ✗ | 部分成交 |
| 4 | Filled | 全成 | ✓ | 全部成交 |
| 5 | PendingCancel | 撤单待报 | ✗ | 撤单请求已发 |
| 6 | Canceling | 已报待撤 | ✗ | 等待撤单回报 |
| 7 | CancelFilled | 全撤 | ✓ | 全部撤单 |
| 8 | PartiallyCanceled | 部成部撤 | ✓ | 部分成交后撤单 |
| 9 | Rejected | 废单 | ✓ | 被交易所拒绝 |

**终态行为**：
- 开仓委托终态时，释放全部剩余冻结资金
- 平仓委托终态时，无额外操作（平仓不冻结资金）

---

## 6. 系统计算字段

| 字段名 | 类型 | 入参要求 | 说明 | 维护方式 |
|--------|------|----------|------|----------|
| `frozen` | int64 | 填0 | 冻结资金 | 系统根据委托状态计算并维护 |
| `fee` | int64 | 填0 | 累计手续费 | 成交时累加计算 |
| `margin_ratio` | int32 | 填0 | 保证金率 | 首次开仓时从 FeeCodeInfo 选择并缓存 |

**注意**：broker 报文中这三个字段始终为 0，由系统自维护。

---

## 7. 成交相关字段

| 字段名 | 类型 | 必填条件 | 说明 | 计算公式 |
|--------|------|----------|------|----------|
| `filled_volume` | int32 | 成交时必填 | 累计已成交手数 | 非本次增量，是累计值 |
| `filled_turnover` | int64 | filled_volume>0 时必填 | 累计已成交金额 | 扩大一万倍，= Σ(成交均价 × 乘数 × 手数) |
| `cancel_volume` | int32 | 撤单时必填 | 撤单数量 | 累计撤单数量 |

**关键公式**：
```
trade_price = delta_filled_turnover / delta_filled_volume / multiply
delta_filled_turnover = order.filled_turnover - stored_order.filled_turnover
delta_filled_volume = order.filled_volume - stored_order.filled_volume
```

**注意**：filled_turnover 必须正确，否则导致 trade_price 错误，进而影响 margin/fee 计算。

---

## 8. 可选字段

| 字段名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `cl_order_id` | char[] | "" | 客户端委托ID |
| `date` | int32 | 0 | 业务日期 |
| `order_type` | int32 | 0 | 订单类型 |
| `contract_multiply` | int32 | 0 | 合约乘数（建议填写，用于计算） |
| `hedge_flag` | int32 | 0 | 套保标识（影响保证金率选择） |
| `marketdata_time` | int64 | 0 | 行情时间 |
| `create_time` | int64 | 0 | 创建时间 HHMMSSmmm |
| `update_time` | int64 | 0 | 更新时间 HHMMSSmmm |
| `finish_time` | int64 | 0 | 完成时间 HHMMSSmmm |
| `security` | char[] | "" | 证券名称 |
| `err_code` | int32 | 0 | 错误码 |
| `err_msg` | char[] | "" | 错误信息 |

---

## 9. 数值精度约定

| 字段类型 | 存储类型 | 精度 | 示例 |
|---------|---------|------|------|
| 价格(price, hold_cost) | int64_t | 扩大10000倍 | 3500.25 → 35002500 |
| 金额(margin, fee, pnl, frozen, avail_cash) | int64_t | 扩大10000倍 | 100000.00 → 1000000000 |
| 保证金率(margin_ratio) | int32_t | 扩大10000倍 | 0.12 → 1200 |
| 手续费率(rate) | int32_t | 扩大100000倍 | 0.00015 → 15 |
| 数量(volume) | int32_t | 原值 | 10 → 10 |
| 日期(oper_date) | int32_t | YYYYMMDD | 20260310 |
| 时间(update_time) | int64_t | HHMMSSmmm | 143025500 |

---

## 10. 校验清单

### 10.1 通用校验（所有委托）

| 校验项 | 规则 | 错误码 |
|--------|------|--------|
| order_id 非空 | strlen > 0 | OM_InvalidArg |
| oper_date 匹配 | == trading_date_ | OM_InvalidArg |
| code 非空 | strlen > 0 | OM_InvalidArg |
| side 合法 | inferDirection != 0 | OrderProc_InvalidArg |
| volume > 0 | > 0 | OrderProc_InvalidArg |

### 10.2 开仓委托校验

| 校验项 | 规则 | 错误码 |
|--------|------|--------|
| price > 0 | > 0 | （建议增强） |
| FeeCodeInfo.multiply > 0 | > 0 | OrderProc_InvalidArg |
| 保证金率可获取 | selectMarginRatio != 0 | OrderProc_InvalidMarginRatio |

### 10.3 平仓委托校验

| 校验项 | 规则 | 错误码 |
|--------|------|--------|
| market 合法 | 1-6 有效枚举 | （建议增强） |
| market + side 组合合法 | 见第3.3节 | PositionProc_InvalidSideForMarket |
| 持仓量充足 | volume <= available | PositionProc_InsufficientPosition |

---

## 11. 相关文档

| 主题 | 位置 |
|------|------|
| OmOrder 结构体完整定义 | `../02-domain/order-lifecycle.md` |
| 状态机详细说明 | `../02-domain/order-lifecycle.md` §3 |
| 买卖方向详细说明 | `../02-domain/order-lifecycle.md` §4 |
| 委托处理流程 | `../03-implementation/flows/order-flow.md` |
| 计算公式 | `../02-domain/calc-formulas.md` |
| 枚举速查 | `../00-overview/quick-reference.md` |

---

