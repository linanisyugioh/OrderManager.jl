# 测试场景文档目录

> 从 old_docs/tests/ 迁移的测试场景设计文档

---

## 场景列表

| 场景 | 文档 | 测试内容 | 关键验证点 |
|------|------|----------|-----------|
| 场景1 | scenario1.md | 开仓→平仓完整流程（SHFE平今） | 权益守恒、FIFO平仓 |
| 场景2 | scenario2.md | 平昨仓流程（DCE Long_Close） | 今昨仓区分、持仓价更新 |
| 场景3 | scenario3.md | 指定平今/平昨区分测试 | 差异化费率、今仓/昨仓选择 |
| 场景4 | scenario4.md | 日终结算完整流程 | pnl 兑现、hold_cost 更新、margin 重算 |
| 场景5 | scenario5.md | 交易日初始化（tradingDayUpdate） | order 清空、已平仓删除、ContractStat 重建 |
| 场景6 | scenario6.md | 多日完整流程 | 快照创建、盈亏兑现、多日循环 |
| 场景7 | scenario7.md | 单账户双策略开平 | 策略级+账户级双轨校验 |
| 场景8 | scenario8.md | 平仓委托撤单 | onCloseOrderCancel、LIFO 释放、今仓先放 |
| 场景9 | scenario9.md | Trade成交数据处理 | 字段校验、入库、查询、主键冲突 |
| 场景10 | scenario10.md | 组合委托开平仓流程 | 拆腿、配对、策略/账户级一致性 |
| 场景11 | scenario11.md | 组合持仓与普通持仓混合平仓优先级 | combination_id 优先级、组合打破 |
| 场景12 | scenario12.md | 日终结算未终态委托处理 | PendingNew/PartiallyFilled 日终自动处理 |
| 场景13 | scenario13.md | 查询接口完整测试 | om_query.h 所有查询接口验证 |
| 场景14 | scenario14.md | HFT 适配接口使用与期望值验证 | om_add_fee_info_hft、om_handle_order_hft、om_handle_trade_hft 及查询结果 |
## 使用说明

每个场景文档包含：
1. 场景概述和测试目标
2. 测试数据设计（合约参数、价格、账户）
3. 详细步骤和期望值
4. 计算公式验证
5. 边界情况检查

## 编写新场景的步骤

1. 复制 scenario1.md 作为模板
2. 修改场景概述和测试目标
3. 设计测试数据（合约、价格、手数）
4. 列出详细步骤和期望值
5. 验证计算公式
6. 在 `test/` 目录实现对应测试代码

## 相关文档

- 计算公式：`02-domain/calc-formulas.md`
- 委托流程：`03-implementation/flows/order-flow.md`
- 成交流程：`03-implementation/flows/trade-flow.md`
- 字段定义：`02-domain/order-lifecycle.md`、`02-domain/trade-lifecycle.md`
