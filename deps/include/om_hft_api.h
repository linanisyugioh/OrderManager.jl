/**
 * @file om_hft_api.h
 * @brief HFT 数据结构适配接口：支持传入 HFT 兼容的 Order/Trade 使用本系统
 *
 * 使用 hft_structs.h 中的 HftOrder/HftTrade（仅结构体，无枚举）。
 * 枚举统一使用本系统 om_def.h。HFT 的 Order/Trade 与 HftOrder/HftTrade 布局一致，可强转。
 */

#ifndef OM_HFT_API_H
#define OM_HFT_API_H

#include "om_def.h"
#include "hft_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 添加合约基础信息至缓存（HFT 适配，对应 om_add_fee_info）。
 * 将 HftCodeInfo 转换为 FeeCodeInfo 后调用 om_add_fee_info。
 *
 * @param hft_code_info HFT 合约信息（symbol、乘数、保证金率、手续费等）
 * @return OM_Ok 成功；OM_NotInited 未初始化；OM_InvalidArg 参数非法
 */
 OM_API int om_add_fee_info_hft(const HftCodeInfo* hft_code_info);

/**
 * 接收 HFT 委托回报，驱动 Order → Position → Fundtable 业务流程。
 * 等价于 om_handle_order，但入参为 HFT 的 Order 与 HftCodeInfo 结构。
 *
 * 转换规则：
 *   - HFT symbol (市场.合约ID) → OM code
 *   - 从 symbol 解析市场前缀映射为 Exchange 枚举（SHFE/DCE/CZCE/CFFEX/INE/GFEX 等）
 *   - 从 symbol 解析品种（合约代码中首个数字前的字母部分）填入 product
 *   - order_status → status, order_type → order_type, side → side（枚举值一致）
 *   - hft_code_info 内部转换为 FeeCodeInfo 后调用 om_handle_order
 *
 * @param hft_order HFT 委托结构指针（HftOrder，与 hft Order 布局一致）
 * @param hft_code_info 与该委托标的（symbol）对应的 HFT 合约信息（手续费、保证金等）
 * @return 同 om_handle_order，0 成功，否则为错误码
 */
OM_API int om_handle_order_hft(const HftOrder* hft_order, const HftCodeInfo* hft_code_info);

/**
 * 接收 HFT 成交回报，写入成交记录至 trade 表。
 * 等价于 om_handle_trade，但入参为 HFT 的 Trade 结构。
 * 【当前实现】仅校验字段后直接入库，不参与持仓计算和资金计算。
 *
 * 转换规则：
 *   - HFT exec_id → OM match_seqno
 *   - HFT exec_type → OM match_type
 *   - HFT turnover → OM filled_turnover（HFT 未乘合约乘数，从系统 fee 缓存获取乘数后补乘）
 *   - transact_time：HFT 微秒 → OM 毫秒（/1000）
 *
 * 【前置】须先通过 om_add_fee_info_hft 将合约信息缓存，否则返回 OM_MissingFeeInfo
 *
 * @param hft_trade HFT 成交结构指针（HftTrade，与 hft Trade 布局一致）
 * @return 同 om_handle_trade，0 成功；
 *         OM_MissingFeeInfo 缓存中未找到该合约（symbol）的费率信息
 */
OM_API int om_handle_trade_hft(const HftTrade* hft_trade);

#ifdef __cplusplus
}
#endif

#endif /* OM_HFT_API_H */
