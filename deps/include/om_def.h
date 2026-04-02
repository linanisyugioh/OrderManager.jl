/**
 * @file om_def.h
 * @brief 交易管理常量与枚举定义
 */

#ifndef OM_DEF_H
#define OM_DEF_H

/* ========== 导出宏 ========== */
#if defined(OM_USE_STATIC_LIBRARIE)
#define OM_API
#elif defined(_WIN32)
#ifdef OM_SDK_EXPORTS
#define OM_API __declspec(dllexport)
#else
#define OM_API __declspec(dllimport)
#endif
#else
#define OM_API __attribute__((visibility("default")))
#endif

/* ========== 枚举定义 ========== */

/** 委托状态 */
typedef enum {
    OrderStatus_PendingNew = 1,      /**< 订单待报 */
    OrderStatus_New = 2,            /**< 订单已报 */
    OrderStatus_PartiallyFilled = 3, /**< 订单部分成交 */
    OrderStatus_Filled = 4,         /**< 订单全部成交 */
    OrderStatus_PendingCancel = 5,  /**< 撤单待报 */
    OrderStatus_Canceling = 6,       /**< 已报待撤 */
    OrderStatus_CancelFilled = 7,    /**< 订单已撤销 */
    OrderStatus_PartiallyCanceled = 8, /**< 部成部撤 */
    OrderStatus_Rejected = 9        /**< 订单已拒绝 */
} OrderStatus;

/** 订单类型 */
typedef enum {
    OrderType_LMT = 1,   /**< 限价委托 */
    OrderType_BOC = 2,   /**< 对手方最优价格，深圳证券交易所 */
    OrderType_BOP = 3,   /**< 本方最优价格，深圳证券交易所 */
    OrderType_B5TC = 4,  /**< 最优五档剩余转撤销，上海/深圳证券交易所 */
    OrderType_B5TL = 5,  /**< 最优五档剩余转限价，上海证券交易所 */
    OrderType_IOC = 6,   /**< 即时成交剩余转撤销，深圳证券交易所 */
    OrderType_AON = 7,   /**< 全额成交或撤销，深圳证券交易所 */
    OrderType_ALMT = 9,  /**< 竞价限价，香港证券交易所 */
    OrderType_ELMT = 10, /**< 增强限价，香港证券交易所 */
    OrderType_OLMT = 11  /**< 零股限价，香港证券交易所 */
} OrderType;

/** 买卖方向 */
typedef enum {
    OrderSide_Bid = 1,                    /**< 证券、基金、债券普通买入 */
    OrderSide_Ask = 2,                    /**< 证券、基金、债券普通卖出 */
    OrderSide_Long_Open = 3,              /**< 期货、期权多头开仓 */
    OrderSide_Long_Close = 4,             /**< 期货、期权多头平仓 */
    OrderSide_Short_Open = 5,             /**< 期货、期权空头开仓 */
    OrderSide_Short_Close = 6,            /**< 期货、期权空头平仓 */
    OrderSide_Today_Long_Close = 8,       /**< 上期所期货今仓多头平仓 */
    OrderSide_Today_Short_Close = 10,     /**< 上期所期货今仓空头平仓 */
    OrderSide_PreDay_Long_Close = 11,    /**< 上期所期货昨仓多头平仓 */
    OrderSide_PreDay_Short_Close = 12,    /**< 上期所期货昨仓空头平仓 */
    OrderSide_Margin_Bid = 13,            /**< 证券、基金、债券融资买入 */
    OrderSide_Margin_Ask = 14,            /**< 证券、基金、债券融券卖出 */
    OrderSide_Short_CoveredOpen = 15,     /**< 期权备兑开仓 */
    OrderSide_Short_CoveredClose = 16,    /**< 期权备兑平仓 */
    OrderSide_ETF_Create = 17,            /**< ETF申购 */
    OrderSide_ETF_Redeem = 18,            /**< ETF赎回 */
    OrderSide_Reverse_Repurchase = 19,    /**< 逆回购 */
    OrderSide_Margin_PayBack_Sell = 20,   /**< 买券还券 */
    OrderSide_Margin_PayBack_Buy = 21,    /**< 卖券还款 */
    OrderSide_Margin_PayBack_Stock = 22,  /**< 现券还券 */
    OrderSide_Margin_PayBack_Cash = 23,   /**< 直接还款 */
    OrderSide_Margin_MortgageIn = 24,     /**< 担保品转入 */
    OrderSide_Margin_MortgageOut = 25,    /**< 担保品转出 */
    OrderSide_Repurchase = 26,            /**< 正回购 */
    OrderSide_IPO_Bid = 27,               /**< 新股申购 */
    OrderSide_AHFPT_Bid = 28,             /**< 科创板盘后定价买入 */
    OrderSide_AHFPT_Ask = 29,             /**< 科创板盘后定价卖出 */
    OrderSide_Margin_MoreStockTrans = 30, /**< 余券划转 */
    OrderSide_Allotment_Shares = 31,      /**< 配股认购 */
    OrderSide_ETF_Create_OTC = 33,        /**< 场外ETF申购 */
    OrderSide_ETF_Redeem_OTC = 34,        /**< 场外ETF赎回 */
    OrderSide_Bond_Swap = 35,             /**< 债券转股 */
    OrderSide_Bond_Sell_Back = 36,        /**< 债券回售 */
    OrderSide_Pledge_In = 37,             /**< 质押式回购入库 */
    OrderSide_Pledge_Out = 38             /**< 质押式回购出库 */
} OrderSide;

/** 持仓类型 */
typedef enum {
    PositionSide_Long = 1,        /**< 多仓 */
    PositionSide_Short = 2,       /**< 空仓 */
    PositionSide_Short_Covered = 3 /**< 备兑空仓 */
} PositionSide;

/** 成交回报类型 */
typedef enum {
    TradeReportType_Normal = 1,      /**< 普通回报 */
    TradeReportType_Cancel = 2,      /**< 撤单回报 */
    TradeReportType_Abolish = 3,     /**< 普通废单回报 */
    TradeReportType_InsideCancel = 4, /**< 内部撤单回报，还未到交易所便被撤下来 */
    TradeReportType_CancelAbolish = 5 /**< 撤单废单回报 */
} TradeReportType;

/** 资金账户类型 */
typedef enum {
    AccountType_Placeholder = 0, /**< 占位 */
    AccountType_Stock = 1,      /**< 股票 */
    AccountType_Futures = 2,    /**< 期货 */
    AccountType_Options = 3,    /**< 期权 */
    AccountType_Margin = 4,     /**< 融资融券 */
    AccountType_SHHK_Stock = 5, /**< 沪港通 */
    AccountType_SZHK_Stock = 6, /**< 深港通 */
    AccountType_ExtFund = 7,    /**< 场外基金 */
    AccountType_Gold = 8,       /**< 黄金 */
    AccountType_Forex = 9       /**< 外汇 */
} AccountType;

/** 撤单标识 */
typedef enum {
    CancelFlag_False = 1, /**< 假，不是撤单 */
    CancelFlag_True = 2   /**< 真，是撤单 */
} CancelFlag;

/** 投机套保标识 */
typedef enum {
    HedgeFlag_Placeholder = 0, /**< 占位 */
    HedgeFlag_Speculation = 1, /**< 投机 */
    HedgeFlag_Hedge = 2,      /**< 套保 */
    HedgeFlag_Arbitrage = 3    /**< 套利 */
} HedgeFlag;

/** 交易所枚举 */
typedef enum {
    Exchange_SHFE = 1,      /**< 上海期货交易所 */
    Exchange_DCE = 2,       /**< 大连商品交易所 */
    Exchange_CZCE = 3,      /**< 郑州商品交易所 */
    Exchange_CFFEX = 4,     /**< 中国金融期货交易所 */
    Exchange_INE = 5,       /**< 上海国际能源交易中心 */
    Exchange_GFEX = 6,      /**< 广州期货交易所 */
    Exchange_SSE = 7,       /**< 上海证券交易所 */
    Exchange_SZSE = 8,      /**< 深圳证券交易所 */
    Exchange_BSE = 9,       /**< 北京证券交易所 */
    Exchange_HKEX = 10     /**< 香港证券交易所 */
} Exchange;

#endif /* OM_DEF_H */
