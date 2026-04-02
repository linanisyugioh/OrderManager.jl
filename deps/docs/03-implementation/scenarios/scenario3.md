# 场景3设计文档：指定平今/平昨区分测试

## 1. 场景概述

### 1.1 测试目标
验证上期所(SHFE)合约在指定平今(`OrderSide_Today_Long_Close`)和指定平昨(`OrderSide_PreDay_Long_Close`)时：
1. 手续费计算正确性（平今费率 vs 平昨费率）
2. 持仓单元选择正确性（今仓/昨仓队列）
3. 资金表和持仓表数据一致性
4. 权益守恒验证

### 1.2 业务背景
- **上期所规则**：平仓时必须指定平今或平昨，两者费率通常不同（平今费率往往高于平昨）
- **其他交易所**：如DCE使用`Long_Close`，系统自动优先平昨再平今，无需指定

### 1.3 测试范围
| 验证点 | 说明 |
|--------|------|
| 手续费计算 | 平今使用`close_today`费率，平昨使用`close_preday`费率 |
| 持仓选择 | 平今必须匹配今仓持仓单元，平昨必须匹配昨仓持仓单元 |
| 资金表 | avail_cash, margin, fee, pnl, frozen_cash 各字段准确性 |
| 持仓表 | PositionUnit的open_date区分今昨，close_date回填正确性 |
| 合约统计 | ContractStat的today_long_volume/yesterday_long_volume正确性 |
| 权益守恒 | 最终权益 = 初始权益 - 总手续费 + 实现盈亏 |

---

## 2. 测试数据设计

### 2.1 合约参数
```c
#define S3_TEST_CODE "SHFE.ag2506"           /* 上期所白银期货 */
#define S3_TEST_MULTIPLY       15             /* 合约乘数15克/手 */
#define S3_TEST_MARGIN_RATIO   1200           /* 保证金率12%(×10000) */
#define S3_TEST_FEE_TYPE       1              /* 按比例收费 */

/* 差异化费率设计 - 核心测试点 */
#define S3_TEST_OPEN_RATE        10            /* 开仓万1 (×100000) */
#define S3_TEST_CLOSE_TODAY    150            /* 平今万15 (×100000) - 高频惩罚性费率 */
#define S3_TEST_CLOSE_PREDAY    10            /* 平昨万1 (×100000) - 正常费率 */
```

### 2.2 价格参数
```c
#define S3_TEST_OPEN_PRICE     5000000         /* 开仓价5000元/千克(×10000) */
#define S3_TEST_CLOSE_PRICE    5100000         /* 平仓价5100元/千克(×10000) */
#define S3_TEST_YESTERDAY_PRICE 4900000        /* 昨仓开仓价4900元/千克 */
```

### 2.3 持仓设计
```c
#define S3_TEST_VOLUME_YESTERDAY  2            /* 昨仓2手 */
#define S3_TEST_VOLUME_TODAY      2            /* 今仓2手 */
#define S3_TEST_VOLUME_CLOSE_TODAY  1          /* 平今1手 */
#define S3_TEST_VOLUME_CLOSE_PREDAY 1          /* 平昨1手 */
```

### 2.4 手续费预期计算

#### 平今手续费（万15费率）
```
单手平今费 = 平仓价 × 乘数 × 平今费率 / 100000
          = 5,100,000 × 15 × 150 / 100000
          = 114,750（×10000）
```

#### 平昨手续费（万1费率）
```
单手平昨费 = 平仓价 × 乘数 × 平昨费率 / 100000
          = 5,100,000 × 15 × 10 / 100000
          = 7,650（×10000）
```

#### 盈亏计算
```
今仓单手盈亏 = (5100 - 5000) × 15 = 1,500（×10000）
昨仓单手盈亏 = (5100 - 4900) × 15 = 3,000（×10000）
```

---

## 3. 测试步骤设计

### 步骤1: 系统初始化
- **操作**: `om_init("./test_data_scenario3")`
- **验证点**:
  - 数据库创建成功
  - 持仓表为空

### 步骤2: 设置资金账户
- **操作**: `om_set_fund_config(100万元)`
- **验证点**:
  - `avail_cash = 10,000,000,000`（×10000）
  - 其他字段为0

### 步骤3: 交易日更新（第1交易日）
- **操作**: `om_trading_day_update(20260312)` + `om_add_fee_info` 传入 S3 合约基础信息
- **说明**: 设置昨仓的开仓交易日

### 步骤4: 创建昨仓（模拟前交易日开仓）
- **操作**: 直接插入PositionUnit记录，模拟前交易日开仓
  - `open_date = 20260312`
  - `open_price = 4900000`
  - `volume = 2手`
  - `direction = PositionSide_Long`
- **验证点**:
  - 持仓表：2条昨仓记录
  - ContractStat: `yesterday_long_volume = 2`

### 步骤5: 交易日更新（第2交易日，当前交易日）
- **操作**: `om_trading_day_update(20260313)` + `om_add_fee_info` 传入合约基础信息
- **费率配置**:
  ```c
  fee.open_today = 10;
  fee.open_preday = 10;
  fee.close_today = 150;    /* 平今万15 */
  fee.close_preday = 10;    /* 平昨万1 */
  ```

### 步骤6: 开仓创建今仓
- **操作**: `om_handle_order(Filled)` 开仓2手
  - `side = OrderSide_Long_Open`
  - `price = 5000000`
- **验证点**:
  - 持仓表：新增2条今仓记录（`open_date = 20260313`）
  - 持仓表：保留2条昨仓记录（`open_date = 20260312`）
  - ContractStat:
    - `today_long_volume = 2`
    - `yesterday_long_volume = 2`
  - 资金表：`margin`, `fee`正确扣除

### 步骤7: 指定平今1手 - 委托
- **操作**: `om_handle_order(PendingNew)` 平今委托
  - `side = OrderSide_Today_Long_Close`
  - `volume = 1`
- **验证点**:
  - ContractStat: `today_long_frozen = 1`
  - 持仓表：今仓记录未删除（PendingNew不处理持仓）

### 步骤8: 指定平今1手 - 成交
- **操作**: `om_handle_order(Filled)` 平今成交
  - `side = OrderSide_Today_Long_Close`
  - `filled_volume = 1`
  - `price = 5100000`
- **核心验证点**:
  - **手续费验证**: 订单`fee`字段应为114,750（平今费率）
  - **持仓表验证**: 1条今仓记录被平仓（回填`close_date`, `close_price`）
  - **持仓表验证**: 昨仓记录不受影响
  - **资金表验证**:
    - `fee`累加平今手续费114,750
    - `avail_cash`增加保证金释放和实现盈亏
    - `pnl`保持为0（仅记录浮动盈亏，需通过价格更新触发）
  - ContractStat:
    - `today_long_volume = 1`（2-1=1）
    - `today_long_frozen = 0`

### 步骤9: 指定平昨1手 - 委托
- **操作**: `om_handle_order(PendingNew)` 平昨委托
  - `side = OrderSide_PreDay_Long_Close`
  - `volume = 1`
- **验证点**:
  - ContractStat: `yesterday_long_frozen = 1`

### 步骤10: 指定平昨1手 - 成交
- **操作**: `om_handle_order(Filled)` 平昨成交
  - `side = OrderSide_PreDay_Long_Close`
  - `filled_volume = 1`
  - `price = 5100000`
- **核心验证点**:
  - **手续费验证**: 订单`fee`字段应为7,650（平昨费率）
  - **持仓表验证**: 1条昨仓记录被平仓
  - **持仓表验证**: 剩余今仓记录不受影响
  - **资金表验证**:
    - `fee`累加平昨手续费7,650
    - `avail_cash`正确更新
    - `pnl`保持为0（仅记录浮动盈亏，需通过价格更新触发）
  - ContractStat:
    - `yesterday_long_volume = 1`（2-1=1）
    - `yesterday_long_frozen = 0`

### 步骤11: 验证权益守恒
- **预期最终权益计算**:
  ```
  初始权益 = 10,000,000,000

  开仓2手费用:
    - 开仓手续费: 5,000,000 × 15 × 10 / 100000 × 2 = 15,000
    - 保证金占用: 5,000,000 × 15 × 1200 / 10000 × 2 = 18,000,000

  平今1手费用和盈亏:
    - 平今手续费: 5,100,000 × 15 × 150 / 100000 × 1 = 114,750
    - 实现盈亏: (5,100,000 - 5,000,000) × 15 × 1 = 1,500,000

  平昨1手费用和盈亏:
    - 平昨手续费: 5,100,000 × 15 × 10 / 100000 × 1 = 7,650
    - 实现盈亏: (5,100,000 - 4,900,000) × 15 × 1 = 3,000,000

  总手续费 = 15,000 + 114,750 + 7,650 = 137,400
  总实现盈亏 = 1,500,000 + 3,000,000 = 4,500,000

  预期最终权益 = 10,000,000,000 - 137,400 + 4,500,000
              = 10,004,362,600
  ```

### 步骤12: 最终状态验证
- **持仓表验证**:
  - 今仓剩余: 1手未平仓（`close_date = 0`）
  - 昨仓剩余: 1手未平仓（`close_date = 0`）
  - 已平仓今仓: 1手（`close_date = 20260313`）
  - 已平仓昨仓: 1手（`close_date = 20260313`）
- **ContractStat验证**:
  - `today_long_volume = 1`
  - `yesterday_long_volume = 1`
  - `today_long_frozen = 0`
  - `yesterday_long_frozen = 0`
- **资金表验证**:
  - `fee = 137,400`
  - `margin` = 1手今仓保证金 = 9,000,000
  - `pnl` = 0（本场景未触发价格更新，浮动盈亏需通过om_update_price计算）

---

## 4. 数据验证详细设计

### 4.1 资金表验证函数
```c
static inline int s3_verifyFundtable(
    int64_t expected_avail,      /* 预期可用资金 */
    int64_t expected_margin,     /* 预期保证金 */
    int64_t expected_frozen,     /* 预期冻结资金 */
    int64_t expected_fee,        /* 预期总手续费 */
    int64_t expected_pnl,        /* 预期浮动盈亏 */
    const char* step             /* 步骤名称 */
);
```

### 4.2 持仓表验证函数
```c
/* 按开仓日期分别验证今仓/昨仓数量 */
static inline int s3_verifyPositionByDate(
    int32_t open_date,           /* 开仓日期 */
    int expected_count,          /* 预期未平仓数量 */
    int expected_closed,         /* 预期已平仓数量 */
    const char* step
);

/* 验证持仓单元字段准确性 */
static inline int s3_verifyPositionFields(
    int64_t position_id,         /* 持仓ID */
    int32_t expected_open_date,  /* 预期开仓日期 */
    int64_t expected_open_price, /* 预期开仓价 */
    int32_t expected_close_date, /* 预期平仓日期（0表示未平仓） */
    int64_t expected_fee,        /* 预期手续费 */
    int64_t expected_pnl,        /* 预期盈亏 */
    const char* step
);
```

### 4.3 合约统计验证函数
```c
static inline int s3_verifyContractStat(
    int32_t expected_today_long,       /* 今仓多头持仓 */
    int32_t expected_today_frozen,       /* 今仓多头冻结 */
    int32_t expected_yesterday_long,     /* 昨仓多头持仓 */
    int32_t expected_yesterday_frozen,   /* 昨仓多头冻结 */
    const char* step
);
```

### 4.4 订单验证函数
```c
static inline int s3_verifyOrder(
    const char* order_id,
    int32_t expected_status,
    int32_t expected_filled_volume,
    int64_t expected_fee,        /* 验证订单手续费正确性 */
    const char* step
);
```

---

## 5. 风险点和边界条件

### 5.1 已考虑边界
| 边界条件 | 处理方案 |
|---------|---------|
| 平今/平昨费率相同 | 测试设计中费率不同（150 vs 10），确保可区分 |
| 部分成交 | 本次测试使用全部成交，后续可扩展部分成交场景 |
| 手续费计算溢出 | 使用int64_t，测试数据量小不会溢出 |

### 5.2 后续扩展建议
1. **部分平今/平昨**: 测试1手分多次平仓
2. **跨交易日测试**: 实际跨天测试昨仓识别
3. **反向开仓测试**: 今仓盈利/亏损不同场景
4. **套保持仓测试**: `hedge_flag`对保证金的影响

---

## 6. 文件结构

```
test/
├── test_scenario3.h          /* 本场景测试代码 */
└── test.cc                  /* 测试入口（新增scenario3调用） */

docs/
└── 03-implementation/scenarios/scenario3.md  /* 本文档 */
```

---

## 7. 总结

本场景测试通过设计差异化的平今/平昨费率（万15 vs 万1），验证了：
1. 系统能正确区分今仓/昨仓持仓单元
2. 手续费计算使用对应费率
3. 资金表、持仓表、合约统计表数据一致性
4. 权益守恒公式成立

该测试填补了现有测试场景（scenario1、scenario2）中平今平昨区分验证的空白。
