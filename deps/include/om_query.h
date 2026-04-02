/**
 * @file om_query.h
 * @brief 简化版查询接口：单条数据查询，使用系统缓存的run_id/account_id/account_type
 *
 * 使用说明：
 * 1. 先调用 om_set_query_scope 设置查询作用域（run_id, account_id, account_type）
 * 2. 然后调用各查询接口查询数据
 * 3. 所有接口只返回单条数据，避免内存分配和泄漏风险
 */

#ifndef OM_QUERY_H
#define OM_QUERY_H

#include "om_data_types.h"
#include "om_def.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 设置查询作用域（run_id, account_id, account_type）
 *
 * 设置后，所有简化版查询接口（om_query.h中的接口）都会使用此作用域进行查询。
 * 通常在系统初始化或账户切换时调用。
 *
 * @param run_id        实例ID（非空）
 * @param account_id    账户ID（非空）
 * @param account_type  账户类型
 * @return 0成功；OM_NotInited未初始化；OM_InvalidArg参数非法
 */
OM_API int om_set_query_scope(const char* run_id, const char* account_id,
                               int32_t account_type);

/**
 * @brief 获取当前缓存的查询作用域参数
 */
OM_API const char* om_get_query_run_id(void);
OM_API const char* om_get_query_account_id(void);
OM_API int32_t om_get_query_account_type(void);

/* ========== 委托查询 ========== */

/**
 * @brief 按主键查询单条委托（使用缓存作用域）
 * @param order_id      委托ID
 * @param oper_date     委托日期YYYYMMDD
 * @param strategy_id   策略ID
 * @param out           输出参数，查询结果（由调用方提供缓冲区）
 * @return 0成功；OM_NotInited未初始化；OM_InvalidArg参数非法；OrderStore_NotFound未找到
 */
OM_API int om_query_order(const char* order_id, int32_t oper_date,
                          const char* strategy_id, OmOrder* out);

/**
 * @brief 查询 strategy_id 下委托的 order_id 列表（使用缓存作用域）
 *
 * 每次查询将逗号分隔的 order_id 字符串写入 service 内该策略的缓存，通过 out_order_ids 返回
 * 指向该缓存的指针（string.c_str()）。返回格式：order_id 之间用 "," 分割，无数据时为空字符串。
 * 指针在下次对同一策略的相同参数查询或 om_release 前有效。调用方使用 strlen() 可获取字符串长度。
 *
 * @param strategy_id    策略ID（非空）
 * @param status         0=未终态委托，1=已终态，2=所有状态
 * @param code           合约代码，空指针表示所有合约
 * @param side           0=平，1=开，3=全部
 * @param bs             0=空，1=多，3=全部
 * @param out_order_ids  输出，成功时指向该策略的 order_id 列表字符串（不能为空指针）
 * @return 0成功；OM_NotInited未初始化；OM_InvalidArg参数非法
 */
OM_API int om_query_order_ids(const char* strategy_id, int status,
                              const char* code, int side, int bs,
                              const char** out_order_ids);

/* ========== 持仓查询 ========== */

/**
 * @brief 查询 strategy_id 下持仓的 code 列表（使用缓存作用域）
 *
 * 每次查询将逗号分隔的 code 字符串写入 service 内该策略的缓存，通过 out_codes 返回
 * 指向该缓存的指针。返回格式：code 之间用 "," 分割，无数据时为空字符串。
 * 指针在下次对同一策略的相同参数查询或 om_release 前有效。调用方使用 strlen() 可获取字符串长度。
 *
 * @param strategy_id  策略ID（非空）
 * @param status       0=冻结，1=可用，2=全部
 * @param period       0=昨仓，1=今仓，2=全部
 * @param side         0=空，1=多，2=全部
 * @param out_codes    输出，成功时指向该策略的 code 列表字符串（不能为空指针）
 * @return 0成功；OM_NotInited未初始化；OM_InvalidArg参数非法
 */
OM_API int om_query_position_codes(const char* strategy_id, int status, int period, int side,
                                   const char** out_codes);

/**
 * @brief 查询账户级持仓的 code 列表（使用缓存作用域）
 *
 * 返回账户下全部未平仓持仓的 distinct code，逗号分隔。
 * 用于交易日初始化后获取持仓 codes，遍历调用 om_add_fee_info 传入各合约 FeeCodeInfo。
 *
 * @param out_codes 输出，成功时指向缓存的 code 列表字符串（不能为空指针）
 * @return 0成功；OM_NotInited未初始化；OM_InvalidArg参数非法
 */
OM_API int om_query_account_position_codes(const char** out_codes);

/**
 * @brief 查询合约统计（使用缓存作用域）
 * @param strategy_id   策略ID
 * @param code          合约代码
 * @param out           输出参数，合约统计结果（由调用方提供缓冲区）
 * @return 0成功；OM_NotInited未初始化；OM_InvalidArg参数非法；ContractStatStore_NotFound未找到
 */
OM_API int om_query_contract_stat(const char* strategy_id, const char* code,
                                   ContractStat* out);

/**
 * @brief 查询账户级合约统计（使用缓存作用域）
 * @param code          合约代码
 * @param out           输出参数，合约统计结果（由调用方提供缓冲区）
 * @return 0成功；OM_NotInited未初始化；OM_InvalidArg参数非法；AccountContractStatStore_NotFound未找到
 */
OM_API int om_query_account_contract_stat(const char* code,
                                           AccountContractStat* out);

/* ========== 资金查询 ========== */

/**
 * @brief 查询策略级资金（使用缓存作用域）
 * @param strategy_id   策略ID
 * @param out           输出参数，资金查询结果（由调用方提供缓冲区）
 * @return 0成功；OM_NotInited未初始化；OM_InvalidArg参数非法；FundtableStore_NotFound未找到
 */
OM_API int om_query_fund(const char* strategy_id, Fundtable* out);

/**
 * @brief 查询账户级资金（使用缓存作用域）
 * @param out           输出参数，资金查询结果（由调用方提供缓冲区）
 * @return 0成功；OM_NotInited未初始化；OM_InvalidArg参数非法；AccountFundtableStore_NotFound未找到
 */
OM_API int om_query_account_fund(AccountFundtable* out);

#ifdef __cplusplus
}
#endif

#endif /* OM_QUERY_H */
