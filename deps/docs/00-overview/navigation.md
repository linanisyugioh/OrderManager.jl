# 文档导航地图

> 根据当前任务快速定位所需文档

---

## 1. 按任务类型导航

### 🔹 新成员入职

**阅读顺序**：

1. `index.md`（项目入口）- 了解项目概况
2. `demo-two-days-flow.md`（两日Demo）- **快速上手：通过完整示例理解系统**
3. `01-architecture/system-overview.md` - 了解系统架构
4. `02-domain/order-lifecycle.md` - 了解订单生命周期
5. `quick-reference.md` - 记住关键公式和枚举值

---

### 🔹 修改订单处理逻辑

**相关文档**：

1. `02-domain/order-lifecycle.md` - 订单状态机、字段定义
2. `02-domain/combination-order.md` - 组合委托与普通委托的区别
3. `03-implementation/flows/combo-order-leg-split.md` - 组合委托拆分为单腿委托实现规范
4. `03-implementation/flows/order-flow.md` - 委托处理完整流程
5. `03-implementation/interfaces/processor-apis.md` - OrderProcessor接口
6. `quick-reference.md` - OrderSide枚举、错误码

**关键文件**：

- `core/order_processor.h/.cc`
- `core/order_context.h`

---

### 🔹 修改成交处理逻辑

**相关文档**：

1. `02-domain/trade-lifecycle.md` - OmTrade字段定义、与OmOrder的区别
2. `03-implementation/flows/combo-order-leg-split.md` - 组合委托成交后拆分为单腿委托
3. `03-implementation/interfaces/public-apis.md` - om_handle_trade接口
4. `quick-reference.md` - 计算公式、枚举值

**关键文件**：

- `include/om_data_types.h` - OmTrade结构体
- `include/om_manager_api.h` - om_handle_trade声明

---

### 🔹 修改持仓计算逻辑

**相关文档**：

1. `02-domain/position-model.md` - PositionUnit/ContractStat字段定义
2. `02-domain/account-position.md` - 账户级持仓模型
3. `02-domain/combination-position.md` - 组合持仓模型（保证金优惠）
4. `03-implementation/interfaces/processor-apis.md` - PositionProcessor接口
5. `03-implementation/scenarios/` - 测试场景参考

**关键文件**：

- `core/position_processor.h/.cc`
- `core/account_position_processor.h/.cc`

---

### 🔹 修改资金计算逻辑

**相关文档**：

1. `02-domain/fund-model.md` - Fundtable字段、权益计算
2. `02-domain/account-fund.md` - 账户级资金模型
3. `02-domain/calc-formulas.md` - 保证金/盈亏/手续费公式
4. `03-implementation/interfaces/processor-apis.md` - FundtableProcessor接口

**关键文件**：

- `core/fundtable_processor.h/.cc`
- `core/account_fundtable_processor.h/.cc`
- `core/calc_helper.h`

---

### 🔹 修改 service 层

**相关文档**：

1. `01-architecture/module-service.md` - service 模块职责、OmService 编排逻辑
2. `01-architecture/system-overview.md` - 系统架构总览
3. `03-implementation/flows/dayinit-flow.md` - 交易日初始化流程
4. `03-implementation/flows/settlement-flow.md` - 日终结算流程

**关键文件**：

- `service/om_service.h/.cc` - 主服务类
- `service/trading_day_init_service.h/.cc` - 交易日初始化
- `service/trading_day_end_service.h/.cc` - 交易日结束
- `service/combo_order_service.h/.cc` - 组合委托服务

---

### 🔹 修改数据存储层

**相关文档**：

1. `01-architecture/module-data.md` - data模块职责、SQLite 性能优化
2. `02-domain/*-model.md` - 各模型字段定义
3. `03-implementation/interfaces/store-apis.md` - Store接口汇总
4. `04-reference/DOCUMENT_STANDARD.md` - 文档规范

**关键文件**：

- `data/db_manager.h/.cc` - 数据库连接、PRAGMA 设置
- `data/*_store.h/.cc`

---

### 🔹 使用查询套件进行并发查询

**相关文档**：

1. `01-architecture/module-kit.md` - Kit层（查询套件池）架构设计
2. `01-architecture/system-overview.md` - 系统架构总览（含kit层定位）

**关键文件**：

- `kit/query_kit_pool.h/.cc` - 查询套件池实现
- `kit/query_kit.h/.cc` - 查询套件实现
- `service/om_service.cc` - 查询接口使用示例

**设计要点**：

- **kit 层定位**：与 core 层平行的扩展层，为系统提供可插拔的附加功能
- 查询套件提供独立的SQLite连接，与写入操作完全隔离
- 套件池在`tradingDayUpdate`时初始化，在`tradingDayEnd`时释放
- 支持多线程并发查询，默认池大小为5
- 可在 kit 层扩展其他类型套件（如缓存套件、监控套件等），无需修改 core 层核心计算逻辑

---

### 🔹 理解日终结算流程

**相关文档**：

1. `02-domain/fund-model.md` §日终结算 - 结算模型详解
2. `03-implementation/flows/settlement-flow.md` - 结算流程
3. `02-domain/calc-formulas.md` - 结算盈亏公式

**关键文件**：

- `service/om_service.cc` - tradingDayEnd()

---

### 🔹 新增测试场景

**相关文档**：

1. `03-implementation/scenarios/` - 参考已有场景文档
2. `quick-reference.md` - 计算公式、枚举值
3. `04-reference/order-fields.md` - OmOrder字段要求

**关键文件**：

- `test/test_scenario*.h`
- `test/test.cc`

---

### 🔹 修改对外API

**相关文档**：

1. `01-architecture/system-overview.md` §对外API总览
2. `03-implementation/interfaces/public-apis.md`
3. `04-reference/order-fields.md` - 入参字段要求

**关键文件**：

- `include/om_manager_api.h` - 核心 API
- `include/om_hft_api.h` - HFT 适配
- `include/om_query.h` - 查询 API
- `include/hft_structs.h` - HFT 结构体
- `api/` 目录

---

## 2. 文档层级速查


| 需求     | 查阅层级  | 具体文档                                     |
| ------ | ----- | ---------------------------------------- |
| 了解项目概况 | L0    | `index.md`, `quick-reference.md`         |
| 了解模块职责 | L1    | `01-architecture/module-*.md`            |
| 了解业务模型 | L2    | `02-domain/*-model.md`                   |
| 编码实现   | L2+L3 | `02-domain/` + `03-implementation/`      |
| 查公式/枚举 | L0/L2 | `quick-reference.md`, `calc-formulas.md` |
| 查规范/决策 | L4    | `04-reference/`                          |


---

## 3. 核心流程文档索引


| 流程     | 文档位置                                               | 关键类                         |
| ------ | -------------------------------------------------- | --------------------------- |
| 委托处理   | `03-implementation/flows/order-flow.md`            | OrderProcessor              |
| 组合委托拆腿 | `03-implementation/flows/combo-order-leg-split.md` | ComboOrderService           |
| 成交处理   | `03-implementation/flows/trade-flow.md`            | TradeProcessor              |
| 行情刷新   | `03-implementation/flows/newprice-flow.md`         | PositionProcessor           |
| 交易日初始化 | `03-implementation/flows/dayinit-flow.md`          | OmService::tradingDayUpdate |
| 日终结算   | `03-implementation/flows/settlement-flow.md`       | OmService::tradingDayEnd    |


---

## 4. 架构模块文档索引


| 模块        | 文档位置                                 | 说明             |
| --------- | ------------------------------------ | -------------- |
| 系统总览      | `01-architecture/system-overview.md` | 整体架构           |
| service 层 | `01-architecture/module-service.md`  | 业务编排、OmService |
| core 层    | `01-architecture/module-core.md`     | 核心计算           |
| kit 层     | `01-architecture/module-kit.md`      | 查询套件等扩展        |
| data 层    | `01-architecture/module-data.md`     | 数据持久化          |


---

## 5. 模型文档索引


| 模型    | 文档位置                                | 数据结构                                     |
| ----- | ----------------------------------- | ---------------------------------------- |
| 订单    | `02-domain/order-lifecycle.md`      | OmOrder                                  |
| 成交    | `02-domain/trade-lifecycle.md`      | OmTrade                                  |
| 策略级持仓 | `02-domain/position-model.md`       | PositionUnit, ContractStat               |
| 策略级资金 | `02-domain/fund-model.md`           | Fundtable                                |
| 账户级持仓 | `02-domain/account-position.md`     | AccountPositionUnit, AccountContractStat |
| 账户级资金 | `02-domain/account-fund.md`         | AccountFundtable                         |
| 组合委托  | `02-domain/combination-order.md`    | OmOrder（组合委托场景）                          |
| 组合持仓  | `02-domain/combination-position.md` | CombinationUnit                          |


---

## 6. 接口文档索引


| 接口类型       | 文档位置                                             | 说明     |
| ---------- | ------------------------------------------------ | ------ |
| Store层     | `03-implementation/interfaces/store-apis.md`     | CRUD操作 |
| Processor层 | `03-implementation/interfaces/processor-apis.md` | 业务计算   |
| 对外API      | `03-implementation/interfaces/public-apis.md`    | C接口    |


---

## 7. 场景测试文档索引


| 场景   | 文档位置                                        | 测试内容                                                     |
| ---- | ------------------------------------------- | -------------------------------------------------------- |
| 场景1  | `03-implementation/scenarios/scenario1.md`  | 开仓→平仓（SHFE平今）                                            |
| 场景2  | `03-implementation/scenarios/scenario2.md`  | 平昨仓流程                                                    |
| 场景3  | `03-implementation/scenarios/scenario3.md`  | 指定平今/平昨区分测试                                              |
| 场景4  | `03-implementation/scenarios/scenario4.md`  | 日终结算完整流程                                                 |
| 场景5  | `03-implementation/scenarios/scenario5.md`  | 交易日初始化（tradingDayUpdate）                                 |
| 场景6  | `03-implementation/scenarios/scenario6.md`  | 多日完整流程                                                   |
| 场景7  | `03-implementation/scenarios/scenario7.md`  | 单账户双策略开平                                                 |
| 场景8  | `03-implementation/scenarios/scenario8.md`  | 平仓委托撤单（LIFO释放）                                           |
| 场景9  | `03-implementation/scenarios/scenario9.md`  | OmTrade成交数据处理                                            |
| 场景10 | `03-implementation/scenarios/scenario10.md` | 组合委托开平仓流程                                                |
| 场景11 | `03-implementation/scenarios/scenario11.md` | 组合持仓与普通持仓混合平仓优先级                                         |
| 场景12 | `03-implementation/scenarios/scenario12.md` | 日终结算未终态委托处理                                              |
| 场景13 | `03-implementation/scenarios/scenario13.md` | om_query.h 对外查询接口完整测试                                    |
| 场景14 | `03-implementation/scenarios/scenario14.md` | HFT 适配接口使用与期望值验证                                         |
| 场景15 | `03-implementation/scenarios/scenario15.md` | 行情更新接口 om_handle_newprice 性能测试（100 合约×100 手×10 次更新，耗时统计） |


---

## 8. 决策记录索引


| 类型  | 文档位置                                                                                 | 内容        |
| --- | ------------------------------------------------------------------------------------ | --------- |
| 决策  | `03-implementation/flows/settlement-flow.md` §Step 1、`02-domain/fund-model.md` §资金历史 | 日终结算前快照设计 |


---

## 9. 当前迭代指引

**当前迭代文档**：`current-iteration.md`（本文档同级目录）

该文档包含：

- 当前迭代目标
- 进行中的TODO清单
- 最近的重要决策
- 待解决问题

**编码前必读**：在开始新任务前，先查看current-iteration.md了解当前上下文。

---

## 10. 大模型使用建议

当作为AI助手协助开发时，建议按以下顺序加载上下文：

1. **常驻上下文**：`index.md`, `quick-reference.md`
2. **任务相关**：根据上述导航选择对应L1/L2/L3文档
3. **动态上下文**：`00-overview/current-iteration.md`（获取当前状态）

**避免**：一次性加载全部文档，会导致关键信息被淹没。