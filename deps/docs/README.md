# Order Manager 渐进式文档架构

> 本文档架构设计用于增强大模型对项目的理解，支持分层加载上下文

---

## 文档架构概览

```
docs/
├── 00-overview/              # L0: 常驻上下文
│   ├── index.md              # 项目入口：核心概念、术语表
│   ├── quick-reference.md    # 快速参考：公式、枚举、错误码
│   └── navigation.md         # 导航地图：按任务类型定位文档
│
├── 01-architecture/          # L1: 架构层
│   ├── system-overview.md    # 系统架构总览
│   ├── module-core.md        # core模块职责（核心计算）
│   ├── module-data.md        # data模块职责（数据持久化）
│   ├── module-service.md     # service模块职责（业务编排）
│   ├── module-kit.md         # kit模块职责（扩展套件）
│   └── conventions.md        # 全局约定（命名、错误码、精度）
│
├── 02-domain/                # L2: 领域层
│   ├── order-lifecycle.md    # 订单状态机、字段定义
│   ├── position-model.md     # 策略级持仓模型
│   ├── fund-model.md         # 策略级资金模型
│   ├── account-position.md   # 账户级持仓模型
│   ├── account-fund.md       # 账户级资金模型
│   ├── combination-order.md  # 组合委托与普通委托区别
│   ├── combination-position.md # 组合持仓模型（保证金优惠）
│   ├── trade-lifecycle.md    # 成交字段定义与生命周期
│   └── calc-formulas.md      # 核心计算公式汇总
│
├── 03-implementation/        # L3: 实现层
│   ├── flows/                # 业务流程
│   │   ├── order-flow.md         # 委托处理流程
│   │   ├── trade-flow.md         # 成交处理流程
│   │   ├── newprice-flow.md      # 行情刷新流程
│   │   ├── dayinit-flow.md       # 交易日初始化
│   │   ├── settlement-flow.md    # 日终结算流程
│   │   └── combo-order-leg-split.md # 组合委托拆单流程
│   ├── interfaces/           # 接口定义
│   │   ├── store-apis.md
│   │   ├── processor-apis.md
│   │   └── public-apis.md
│   ├── scenarios/            # 测试场景
│   │   └── README.md
│   └── current-iteration.md  # 【动态】当前迭代状态
│
├── 04-reference/             # L4: 参考资料
│   ├── DOCUMENT_STANDARD.md  # 文档规范
│   ├── order-fields.md       # Order字段要求
│   └── decisions/            # 技术决策记录
│
└── .cursor/
    └── rules/
        └── order-manager.md  # Cursor规则文件
```

---

## 四层文档使用指南

### L0 概览层（常驻上下文）

**文档**：`00-overview/`

**使用场景**：
- 首次进入项目，快速了解概况
- 日常开发中快速查找公式/枚举
- 不确定该读哪个文档时查看导航

**核心内容**：
- 项目一句话描述
- 核心概念和术语表
- 关键公式速查
- 枚举值速查
- 按任务类型的文档导航

---

### L1 架构层（模块职责）

**文档**：`01-architecture/`

**使用场景**：
- 了解模块分工和职责边界
- 理解模块间依赖关系
- 查阅全局约定（命名、错误码）

**核心内容**：
- 五层架构说明（api/service/core/kit/data）
- 每个模块的职责和设计原则
- 模块依赖关系图
- 全局命名规范、错误处理策略、数值精度约定

**模块说明**：
- **core 层**：核心计算逻辑，委托处理、持仓管理、资金计算
- **kit 层**：扩展功能套件（如查询套件），与 core 层平行，可插拔扩展

---

### L2 领域层（业务模型）

**文档**：`02-domain/`

**使用场景**：
- **编码实现时必读**
- 查看数据结构字段定义
- 理解计算公式和业务规则

**核心内容**：
- Order/PositionUnit/Fundtable 字段定义
- 今仓/昨仓区分规则
- FIFO平仓匹配规则
- 保证金/盈亏/手续费计算公式
- 日终结算模型（逐日盯市）

---

### L3 实现层（流程和接口）

**文档**：`03-implementation/`

**使用场景**：
- 理解具体业务流程
- 查看类/方法详细接口
- 编写测试场景
- **查看当前迭代任务**

**核心内容**：
- **flows/**：委托处理、成交处理、行情刷新、日终结算流程
- **interfaces/**：Store/Processor/对外API 接口定义
- **scenarios/**：场景1-14测试设计文档
- **current-iteration.md**：当前TODO、重要决策

**测试场景文档**（14个完整场景）：
- **场景1-3**：基础开平仓（SHFE平今/平昨区分）
- **场景4-6**：日终结算、交易日初始化、多日完整流程
- **场景7-8**：单账户双策略、平仓委托撤单
- **场景9**：OmTrade成交数据处理
- **场景10-12**：组合委托开平仓、组合持仓优先级、结算未终态委托
- **场景13-14**：查询接口完整测试、HFT适配接口测试

---

### L4 参考层（规范和决策）

**文档**：`04-reference/`

**使用场景**：
- 查阅文档编写规范
- 查看字段校验规则
- 了解历史技术决策

**核心内容**：
- 文档编写规范
- Order字段要求
- 技术决策记录（ADR）

---

## 按任务类型的文档阅读路径

| 任务类型 | 推荐阅读顺序 |
|----------|-------------|
| **新成员入职** | L0-index → L0-navigation → L1-system-overview → L2-order-lifecycle |
| **修改订单处理** | L3-current-iteration → L2-order-lifecycle → L3-flows/order-flow → L1-module-core |
| **修改持仓计算** | L3-current-iteration → L2-position-model → L2-calc-formulas → L1-module-core |
| **修改资金计算** | L3-current-iteration → L2-fund-model → L2-calc-formulas → L1-module-core |
| **修改数据层** | L3-current-iteration → L1-module-data → L3-interfaces/store-apis |
| **使用查询套件** | L3-current-iteration → L1-module-kit → kit/query_kit_pool.h |
| **修改日终结算** | L3-current-iteration → L2-fund-model §日终结算 → L3-flows/settlement-flow |
| **新增测试场景** | L0-quick-reference → L3-scenarios/scenario*.md → L2-calc-formulas |
| **修改组合委托** | L3-current-iteration → L2-combination-order → L3-flows/combo-order-leg-split |
| **修改对外API** | L3-current-iteration → L1-system-overview → L3-interfaces/public-apis |

---

## Cursor 规则文件

**位置**：`.cursor/rules/order-manager.md`

**作用**：
- 自动加载项目核心上下文到AI对话
- 提供文档阅读指引
- 规范代码风格和错误处理

**使用方式**：Cursor IDE 会自动读取该规则文件。

---

## 原文档备份

原有 docs/ 目录下的文档已备份到 `old_docs/`：

```
old_docs/
├── account_fundtable_design.md
├── account_position_design.md
├── fundtable_snapshot_design.md
├── order_field_requirements.md
├── DOCUMENT_STANDARD.md
├── architecture/overview.md
├── modules/mod_core.md, mod_data.md, mod_service.md
└── tests/scenario1-6_design.md
```

---

## 文档修订记录

---

## 维护说明

1. **current-iteration.md 需持续更新**：记录当前开发任务和重要决策
2. **编码前必读**：根据任务类型查阅对应文档（见「按任务类型的文档阅读路径」）
3. **渐进式加载**：遵循 L0 → L1 → L2 → L3 的顺序加载上下文，避免一次性加载全部文档
4. **发现问题时**：在对应文档的「编码反馈」章节记录问题
5. **新增功能时**：
   - 先更新设计文档，再编写代码
   - 按层级添加到对应目录
   - 更新相关文档的「相关文档」章节建立双向链接
6. **文档引用规范**：使用统一格式 `[文档名](../../path/to/file.md)` 建立可点击链接
