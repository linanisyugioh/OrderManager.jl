/**
 * @file manager_api.h
 * @brief 动态库对外接口：委托回报等统一入口
 */

#ifndef OM_MANAGER_API_H
#define OM_MANAGER_API_H

#include "om_data_types.h"
#include "om_def.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化管理实例：初始化日志、数据库、各 Store 与 service 组件。
 * @param work_dir 工作目录（必须可写）；日志写入 work_dir/logs，数据库为 work_dir/om.db
 * @return OM_Ok 成功，否则见 om_error.h 中 OM_*
 */
OM_API int om_init(const char* work_dir);

/**
 * 释放管理实例：关闭数据库连接、释放资源；与 om_init 配对调用
 */
 OM_API void om_release(void);

/**
 * 交易日更新（新交易日开始，开盘前调用）：进入指定交易日；清空 order 表，并移除 position 表中所有已平仓的记录。
 *
 * 【FeeCodeInfo 说明】合约基础信息（乘数、保证金率等）不再由此接口传入，而是通过 om_add_fee_info 逐个传入。
 * 建议流程：交易日初始化成功后，调用 om_set_query_scope 设置账户作用域，再调用 om_query_account_position_codes
 * 获取持仓 codes，遍历调用 om_add_fee_info 将各合约的 FeeCodeInfo 传入。
 *
 * @param trading_date 交易日（YYYYMMDD），表示进入该交易日
 * @return OM_Ok 成功；OM_NotInited 未初始化；OM_InvalidArg 参数非法；其他 Store 错误码
 */
OM_API int om_trading_day_update(int trading_date);

/**
 * 添加合约基础信息至缓存（供日终结算等使用）。
 * 可在 om_trading_day_update 成功后，按持仓 codes 遍历调用，逐个传入各合约的 FeeCodeInfo。
 *
 * @param fee_info 合约基础信息（合约乘数、保证金率等），手续费字段可留空
 * @return OM_Ok 成功；OM_NotInited 未初始化；OM_InvalidArg 参数非法
 */
OM_API int om_add_fee_info(const FeeCodeInfo* fee_info);

/**
 * 交易日结束：触发日终结算，逐手计算结算盈亏并转入可用资金，
 * 更新持仓价为结算价，重算保证金。
 *
 * 【结算价要求】
 * 日终结算前必须通过 om_handle_newprice 传入所有持仓合约的当日结算价。
 * 系统会重新计算结算盈亏，并与盘中累计的浮动盈亏进行对比。
 * 若两者不一致（差值 > 误差阈值），将记录 ERROR 日志并返回 OM_SettlementPnlMismatch，
 * 但结算流程仍按重新计算的结果完成。
 *
 * @return OM_Ok 成功；
 *         OM_NotInited 未初始化；
 *         OM_MissingSettlementPrice 缺少结算价缓存；
 *         OM_MissingFeeInfo 缺少费率缓存；
 *         OM_SettlementPnlMismatch 结算盈亏与盘中盈亏不一致（警告，结算仍完成）
 */
 OM_API int om_trading_day_end(void);

/**
 * 接收最新委托，驱动 Order → Position → Fundtable 业务流程。费率和手续费等合约基本信息不由本系统维护，由调用方随该笔委托一并传入。
 * 内部调用 OmService::instance().handleOrder(in_order, fee_code_info)。
 *
 * Order 必填字段详见 docs/04-reference/order-fields.md，摘要：
 *   - 主键：order_id, oper_date, strategy_id, run_id, account_id, account_type
 *   - 业务：code, market, side, status, volume, price（开仓时）
 *   - 成交时：filled_volume, filled_turnover（filled_turnover = 均价×multiply×filled_volume）
 *   - 系统计算字段（frozen/fee/margin_ratio）入参填 0
 *
 * @param in_order 委托最新状态（按值传入，内部转交业务流程类处理）
 * @param fee_code_info 与该委托标的（in_order.code）对应的手续费率与合约/保证金参数，由调用方传入，用于本笔委托的 frozen/margin/fee 计算
 * @return 0 成功，否则为错误码（不重复）：
 *   OM_NotInited(-8) 未初始化；
 *   OrderProc_InvalidArg(-101)/OrderProc_FeeCodeInvalid(-102)/OrderProc_Internal(-103)/
 *   OrderProc_InvalidState(-104)/OrderProc_InvalidMarginRatio(-105)/OrderProc_InvalidExchange(-106)；
 *   PositionProc_InvalidArg(-201)/PositionProc_NotFound(-202)/PositionProc_StoreError(-203)/
 *   PositionProc_InsufficientPosition(-204)/PositionProc_InvalidSideForMarket(-205)；
 *   FundtableProc_InvalidArg(-301)/FundtableProc_StoreError(-302)/FundtableProc_NotFound(-303)。
 * 详见 om_error.h
 */
 OM_API int om_handle_order(OmOrder in_order, FeeCodeInfo fee_code_info);

/**
 * 更新合约最新价，刷新该合约下所有未平仓持仓的浮动盈亏（pnl）；
 * 同时写入最新价和结算价缓存供开仓和日终结算使用。
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
 * @param code 合约代码，与 position/position_unit 的 code 一致，非空
 * @param last_price 最新价，扩大一万倍
 * @param pre_settlement_price 昨结算价，扩大一万倍
 * @param settlement_price 今结算价，扩大一万倍；0表示盘中行情（无效）
 * @return OM_Ok 成功；OM_NotInited 未初始化；OM_InvalidArg 参数非法
 */
 OM_API int om_handle_newprice(const char* code, int64_t last_price,
                               int64_t pre_settlement_price, int64_t settlement_price);

/**
 * 写入资金配置（建初值）：按 run_id、account_id、account_type、strategy_id 唯一确定一条资金记录，写入 data_store 供业务处理使用。
 * 写入前检测数据库中是否已存在对应主键记录；已存在则返回错误，不覆盖。
 * @param fund 资金配置，主键及金额等字段须有效（avail_cash、start_cash、equity 等扩大一万倍）
 * @return OM_Ok(0) 成功；OM_NotInited(-8) 未初始化；OM_InvalidArg(-1) 参数非法；
 *         FundtableStore_DupKey(-2) 已存在同主键记录；其他 FundtableStore_* 见 om_error.h
 */
 OM_API int om_set_fund_config(const Fundtable* fund);

/**
 * 写入账户级资金配置（建初值）：按 run_id、account_id、account_type 唯一确定一条账户级资金记录。
 * 账户级资金是跨策略的汇总，用于风控和总权益计算。
 * 写入前检测数据库中是否已存在对应主键记录；已存在则返回错误，不覆盖。
 * @param fund 账户级资金配置，主键及金额等字段须有效（account_cash、account_start_cash、account_equity 等扩大一万倍）
 * @return OM_Ok(0) 成功；OM_NotInited(-8) 未初始化；OM_InvalidArg(-1) 参数非法；
 *         AccountFundtableStore_DupKey(-474) 已存在同主键记录；其他 AccountFundtableStore_* 见 om_error.h
 */
 OM_API int om_set_account_fund_config(const AccountFundtable* fund);

/**
 * 接收成交回报，写入成交记录至 trade 表。
 *
 * 【当前实现】仅校验字段后直接入库，不参与持仓计算和资金计算，也不校验关联 Order 是否存在。
 * 与 om_handle_order 的区别：
 *   - om_handle_order：委托状态驱动，处理 PendingNew → Filled 全流程（含持仓、资金）
 *   - om_handle_trade：成交回报驱动，当前仅写入成交明细
 *
 * Trade 必填字段详见 docs/02-domain/trade-lifecycle.md，摘要：
 *   - 主键（7字段）：order_id, trade_date, strategy_id, run_id, account_id, account_type, match_seqno
 *   - 业务：code, side, volume, price
 *   - 辅助：filled_turnover, fee, order_volume, order_price, slippage, date, transact_time, match_type
 *
 * @param trade 成交数据（按值传入，内部转交业务流程类处理）
 * @return 0 成功，否则为错误码：
 *   OM_NotInited(-8) 未初始化；
 *   TradeProc_InvalidArg(-601)/TradeProc_NotFound(-602)/TradeProc_StoreError(-603)/TradeProc_DuplicateKey(-604)
 * 详见 om_error.h
 */
 OM_API int om_handle_trade(OmTrade trade);

#ifdef __cplusplus
}
#endif

#endif /* OM_MANAGER_API_H */
