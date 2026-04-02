/**
 * @file hft_structs.h
 * @brief HFT 兼容结构体定义（仅结构体，无枚举）
 *
 * 与 hft_data_types.h/trade_def.h 中的 Order/Trade 内存布局一致，
 * 供 om_handle_order_hft / om_handle_trade_hft 使用。
 * 枚举统一使用本系统 om_def.h 中的定义，此处用基础类型避免重复定义。
 */

#ifndef OM_HFT_STRUCTS_H
#define OM_HFT_STRUCTS_H

#include <stdint.h>

#define HFT_LEN_ID         32
#define HFT_LEN_SYMBOL     32
#define HFT_LEN_COM_ID     32
#define HFT_LEN_PLATE      4
#define HFT_LEN_ERR_MSG    128
#define HFT_LEN_ACCOUNT_ID 64

/** HFT Order 结构体（与 t_Order 布局一致，枚举字段用 int16_t/int32_t） */
typedef struct t_HftOrder {
    char    strategy_id[HFT_LEN_ID];
    char    run_id[HFT_LEN_ID];
    char    order_id[HFT_LEN_ID];
    char    cl_order_id[HFT_LEN_ID];
    char    symbol[HFT_LEN_SYMBOL];

    char    account_id[HFT_LEN_ACCOUNT_ID];
    int16_t account_type;

    int32_t date;
    int32_t trade_seqno;
    int16_t order_status;
    int16_t order_type;
    int16_t side;
    int16_t credit_type;
    int32_t volume;
    int64_t price;
    int32_t filled_volume;
    uint64_t filled_turnover;
    int64_t filled_price;
    int64_t filled_market_value;
    int32_t margin_ratio;
    int64_t marketdata_time;
    int64_t create_time;
    int64_t update_time;
    int64_t finish_time;

    int16_t cancel_flag;
    int32_t cancel_volume;
    int32_t cancel_cnt;
    int64_t order_fee;
    int32_t hedge_flag;
    char    comb_id[HFT_LEN_COM_ID];
    char    plate[HFT_LEN_PLATE];

    int32_t err_code;
    char    err_msg[HFT_LEN_ERR_MSG];
} HftOrder;

/** HFT Trade 结构体（与 t_Trade 布局一致） */
typedef struct t_HftTrade {
    char    strategy_id[HFT_LEN_ID];
    char    run_id[HFT_LEN_ID];
    char    order_id[HFT_LEN_ID];
    char    cl_order_id[HFT_LEN_ID];
    char    symbol[HFT_LEN_SYMBOL];
    char    account_id[HFT_LEN_ACCOUNT_ID];
    int16_t account_type;
    int32_t date;
    int32_t trade_seqno;
    int16_t side;
    int16_t order_type;
    int16_t exec_type;
    char    exec_id[HFT_LEN_ID];
    int32_t volume;
    int64_t price;
    uint64_t turnover;
    int64_t market_value;
    int64_t order_price;
    int32_t order_volume;
    int64_t transact_time;
} HftTrade;

/**
 * 代码基本信息
 */
 typedef struct t_HftCodeInfo {
    char symbol[24];           // 证券代码（带交易所代码）
    int32_t sec_type;          // 代码类型,1：股票,2:期货,3:期权
    char sec_name[24];         // 代码中文名称，编码为utf8
    uint32_t date;             // 日期 YYYYMMDD 
    uint32_t high_limited;     // 涨停价，扩大10000倍
    uint32_t low_limited;      // 跌停价，扩大10000倍
    int32_t multiplier;        // 合约乘数
    int32_t margin_ratio;      // 保证金比率，扩大10000倍
    int32_t price_tick;        // 价格变更单位，扩大10000倍
    int64_t capital;           // 流通股本数, 获取期货当天codeinfo时，表示是否支持大额单边，1支持，0不支持
    uint32_t cap_change_date;  // 股本变动日期
    uint32_t trade_date_in;    // 上市日期YYYYMMDD
    uint32_t trade_date_out;   // 退市日期YYYYMMDD(最后一个交易日)
    uint8_t is_halt;           // 是否停牌，1：停牌，0：正常交易
  
    // 保证金 ， 
    uint32_t margin_unit;         // 单位保证金	N16(2)  ，扩大10000倍
    int32_t margin_ratio_param1;  // 保证金计算比例参数一	N6(2)，扩大10000倍
                                  // 保证金计算参数，获取期货当天codeinfo时，表示多头保证金率
    int32_t margin_ratio_param2;  // 保证金计算比例参数二	N6(2)，扩大10000倍
                                  // 保证金计算参数，获取期货当天codeinfo时，表示空头保证金率
    // 手续费                              
    int32_t close_pre_commission_ratio;  // 平昨仓手续费比例，扩大100000倍
    int32_t close_pre_commission;        // 平昨仓手续费，每手，扩大100000倍
  
    int32_t close_today_commission_ratio;// 平今仓手续费比例，扩大100000倍
    int32_t close_today_commission;      // 平今仓手续费，每手，扩大100000倍
  
    int32_t open_commission_ratio;       // 开仓手续费比例，扩大100000倍
    int32_t open_commission;             // 开仓手续费，每手，扩大100000倍
  } HftCodeInfo;

#endif /* OM_HFT_STRUCTS_H */
