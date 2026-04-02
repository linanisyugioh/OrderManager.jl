# 核心计算公式汇总

> 保证金、手续费、盈亏、冻结资金等所有计算公式

---

## 1. 数值精度说明

| 字段类型 | 存储类型 | 精度 |
|---------|---------|------|
| 价格/金额 | int64_t | 扩大 10000 倍 |
| 保证金率 | int32_t | 扩大 10000 倍 |
| 手续费率 | int32_t | **扩大 100000 倍** |

---

## 2. 保证金计算

### 2.1 单手保证金

```cpp
// 公式
margin_per_lot = price × multiply × margin_ratio / 10000

// 参数
//   price: 价格（扩大一万倍）
//   multiply: 合约乘数（实际值）
//   margin_ratio: 保证金率（扩大一万倍，如12%存为1200）

// 示例
price = 5000000        // 500元/克
multiply = 1           // 合约乘数
margin_ratio = 1200     // 12%

margin = 5000000 × 1 × 1200 / 10000 = 600000  // 60元（扩大一万倍）
```

### 2.2 保证金率选择

```cpp
int32_t selectMarginRatio(const FeeCodeInfo& fee_info,
                          int32_t direction, int32_t hedge_flag) {
    bool is_hedge = (hedge_flag == HedgeFlag_Hedge);  // HedgeFlag_Hedge = 2
    if (direction == PositionSide_Long) {
        return is_hedge ? fee_info.margin_long2 : fee_info.margin_long1;
    }
    if (direction == PositionSide_Short) {
        return is_hedge ? fee_info.margin_short2 : fee_info.margin_short1;
    }
    return 0;  // 无效
}
```

**当前实现**：与 `core/calc_helper.h` 中 `selectMarginRatio` 一致。非 HedgeFlag_Hedge(2) 时按投机处理（margin_long1/margin_short1），与 `include/om_def.h` HedgeFlag 枚举一致。

---

## 3. 手续费计算

### 3.1 按金额计费

```cpp
// 公式
fee_per_lot = price × multiply × rate / 100000

// 参数
//   rate: 费率（扩大十万倍，如万1.5存为15）
//   结果仍为扩大一万倍格式

// 示例
price = 5000000
multiply = 1
rate = 15              // 万1.5

fee = 5000000 × 1 × 15 / 100000 = 750  // 0.075元（扩大一万倍）
```

### 3.2 按手数计费

```cpp
// 公式
// rate: 每手费用（扩大十万倍存储），结果 fee_per_lot 为扩大一万倍金额
fee_per_lot = rate / 10

// 示例
rate = 250000          // 每手2.5元，扩大十万倍存储（2.5 × 100000）
fee = 250000 / 10 = 25000  // 2.5元（扩大一万倍）
```

### 3.3 费率选择

开仓费率：
- `open_today`: 今仓开仓费率
- 开仓时默认使用 `open_today`

平仓费率：
- `close_today`: 平今仓费率
- `close_preday`: 平昨仓费率
- 根据持仓类型选择：今仓用 `close_today`，昨仓用 `close_preday`

---

## 4. 盈亏计算

### 4.1 方向系数

```cpp
int direction_sign(int32_t direction) {
    return (direction == PositionSide_Long) ? +1 : -1;
}
// Long:  价涨盈利 → +1
// Short: 价跌盈利 → -1
```

### 4.2 单手浮动盈亏

```cpp
// 公式（last_price、hold_cost 已×10000，乘积即已为×10000的金额，实现中不再除以10000）
float_pnl = (last_price - hold_cost) × multiply × dir_sign

// 参数
//   last_price: 最新价（扩大一万倍）
//   hold_cost: 持仓价（扩大一万倍）
//   multiply: 合约乘数
//   dir_sign: 方向系数（+1或-1）

// 示例
last_price = 5050000   // 505元
hold_cost = 5000000    // 500元
multiply = 1
dir_sign = +1          // 多头

float_pnl = (5050000 - 5000000) × 1 × 1 = 50000  // 5元（扩大一万倍），与 CalcHelper::calcPnl 一致
```

### 4.3 单手实现盈亏（平仓时）

```cpp
// 公式（同上，价格已×10000，结果即×10000，实现中不除10000）
realized_pnl = (close_price - hold_cost) × multiply × dir_sign

// 与浮动盈亏的区别：使用 close_price 而非 last_price
```

### 4.4 行情盈亏变化计算（基于ContractStat总持仓量 - 性能优化版）

**适用场景**：行情刷新（om_handle_newprice），替代逐手计算，大幅提升性能。

**核心思想**：不逐手查询PositionUnit，而是从ContractStat获取总持仓量，基于价差计算盈亏变化。

```cpp
// 公式：盈亏变化 = 净持仓量 × 合约乘数 × 价差
// 
// 净持仓量 = 多头总持仓 - 空头总持仓
//         = (today_long + yesterday_long) - (today_short + yesterday_short)
// 
// 价差 = 最新价 - 上次价（都扩大一万倍）
// 
// 方向系数隐含在净持仓量中：
//   - 净多头（正数）：价格上涨盈利
//   - 净空头（负数）：价格下跌盈利

int64_t calcPnlDelta(const ContractStat& stat, int64_t price_diff, int32_t multiply) {
    int64_t long_volume  = (int64_t)stat.today_long_volume + stat.yesterday_long_volume;
    int64_t short_volume = (int64_t)stat.today_short_volume + stat.yesterday_short_volume;
    int64_t net_position = long_volume - short_volume;  // 正=净多头，负=净空头
    
    // 盈亏变化 = 净持仓 × 乘数 × 价差
    // 结果自动为扩大一万倍（因为price_diff已扩大一万倍）
    int64_t delta_pnl = net_position * multiply * price_diff;
    
    return delta_pnl;
}

// 示例
ContractStat stat = {
    .today_long_volume = 30,      // 今仓多头30手
    .yesterday_long_volume = 20, // 昨仓多头20手
    .today_short_volume = 10,     // 今仓空头10手
    .yesterday_short_volume = 5   // 昨仓空头5手
};
int64_t price_diff = 50000;   // 价格上涨5元（5050000 - 5000000）
int32_t multiply = 1;         // 合约乘数

// 计算
net_position = (30+20) - (10+5) = 35手（净多头）
delta_pnl = 35 × 1 × 50000 = 1750000  // 盈利175元（扩大一万倍）
```

**等价性证明**：

基于ContractStat的价差法与逐手计算法等价：

```
逐手计算法：
Σ(new_pnl - old_pnl) 
= Σ[(last_price - hold_cost) × multiply × dir_sign - (prev_price - hold_cost) × multiply × dir_sign]
= Σ[(last_price - prev_price) × multiply × dir_sign]
= (last_price - prev_price) × multiply × Σ(dir_sign)
= price_diff × multiply × (long_volume - short_volume)  ← 与价差法一致
```

**公式速查**：
| 计算项 | 公式 |
|--------|------|
| 净持仓量 | `(today_long + yesterday_long) - (today_short + yesterday_short)` |
| 盈亏变化 | `net_position × multiply × price_diff`（结果已扩大一万倍） |
| 总盈亏 | `Fundtable.pnl + delta_pnl` |

---

## 5. 冻结资金估算

### 5.1 单手冻结估算

```cpp
// 公式
frozen_per_lot = margin_per_lot + fee_per_lot

// 说明
//   取开仓费率计算（保守估算）
//   margin_per_lot: 单手保证金
//   fee_per_lot: 单手开仓手续费

// 示例
margin_per_lot = 600000   // 60元
fee_per_lot = 500         // 0.05元

frozen_per_lot = 600000 + 500 = 600500  // 60.05元（扩大一万倍）
```

### 5.2 总冻结估算

```cpp
frozen_total = frozen_per_lot × volume
```

---

## 6. 权益计算

### 6.1 总权益公式

```cpp
// 公式
equity = margin + avail_cash + frozen_cash + pnl

// 说明
//   margin: 保证金（所有持仓占用）
//   avail_cash: 可用资金
//   frozen_cash: 冻结资金（挂单未成交部分）
//   pnl: 浮动盈亏（所有未平仓持仓）
//   fee: 累计手续费（已包含在 avail_cash 的历史变化中，不再重复扣减）
```

### 6.2 资金变化守恒

**开仓成交**：
```
equity(后) = equity(前) - fee
```

**平仓成交**：
```
equity(后) = equity(前) + (close_price - last_price) × multiply × dir_sign - fee
```

---

## 7. 日终结算公式

### 7.1 结算盈亏计算

```cpp
// 逐手计算（last_price、hold_cost 已×10000，结果已为×10000，无需再除）
settlement_pnl_per_lot = (settlement_price - hold_cost) × multiply × dir_sign

// 汇总
settlement_pnl = Σ(settlement_pnl_per_lot)
```

### 7.2 结算后资金更新

```cpp
// Step 1: 盈亏兑现
avail_cash += settlement_pnl
pnl = 0

// Step 2: 持仓价更新
hold_cost = settlement_price

// Step 3: 保证金重算
new_margin_per_lot = calcMargin(settlement_price, multiply, margin_ratio)
margin_delta = (new_margin_per_lot - old_margin) × volume

avail_cash -= margin_delta
margin += margin_delta
```

---

## 8. 公式速查表

| 计算项 | 公式 |
|--------|------|
| 单手保证金 | `price × multiply × margin_ratio / 10000` |
| 单手手续费 | `price × multiply × rate / 100000`（按金额）<br>`rate / 10`（按手数） |
| 单手浮动盈亏 | `(last_price - hold_cost) × multiply × dir_sign`（结果已×10000） |
| 单手实现盈亏 | `(close_price - hold_cost) × multiply × dir_sign`（结果已×10000） |
| 单手冻结 | `margin_per_lot + fee_per_lot` |
| 总权益 | `margin + avail_cash + frozen_cash + pnl` |
| 结算盈亏 | `(settlement_price - hold_cost) × multiply × dir_sign`（结果已×10000） |
| **行情盈亏变化（高性能版）** | `net_position × multiply × price_diff`<br>其中 `net_position = (today_long + yesterday_long) - (today_short + yesterday_short)`<br>`price_diff = last_price - prev_price` |

---

## 9. 相关文档

| 主题 | 位置 |
|------|------|
| 资金模型 | `02-domain/fund-model.md` |
| 持仓模型 | `02-domain/position-model.md` |
| 日终结算流程 | `03-implementation/flows/settlement-flow.md` |
| core模块职责 | `01-architecture/module-core.md` |
| 枚举速查 | `00-overview/quick-reference.md` |
| 快速导航 | `00-overview/navigation.md` |
