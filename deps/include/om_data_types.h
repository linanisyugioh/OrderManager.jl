/**
 * @brief 交易管理核心类型定义
 */

#ifndef OM_DATA_TYPES_H
#define OM_DATA_TYPES_H

#include <cstdint>
#include <stdint.h>

/* ========== 长度宏定义 ========== */
#define LEN_ID         64   /**< ID 类字段长度，含 '\0' */
#define LEN_ACCOUNT_ID 64   /**< 账户ID长度 */
#define LEN_ERR_MSG    128  /**< 错误信息长度 */
#define LEN_CODE       32   /**< 标的/合约代码长度 */
#define LEN_PRODUCT    32   /**< 品种长度 */
#define LEN_SECURITY   64   /**< 证券名称长度 */


/* ========== 成交记录结构体（数据库/持久化） ========== */
/** 成交表对应结构体，主键：order_id + trade_date + strategy_id + run_id + account_id + account_type + match_seqno */
typedef struct t_OmTrade {
    char    order_id[LEN_ID];           /**< 量化后台生成的ID，作为委托的唯一标识（主键） */
    int32_t trade_date;                  /**< 成交日期（主键），对于夜盘填交易归属日 */
    char    strategy_id[LEN_CODE];      /**< 策略ID（主键） */
    char    run_id[LEN_ID];              /**< 实例ID（主键） */
    char    account_id[LEN_ACCOUNT_ID]; /**< 资金账户ID（主键） */
    int32_t account_type;                /**< 资金账户类型（主键） */
    char    match_seqno[LEN_ID];        /**< 成交序号（主键），交易所返回的唯一标识 */
    int32_t match_type;                  /**< 成交回报类型，区分成交回报类型 */
    char    code[LEN_CODE];             /**< 标的代码或合约代码 */
    char    product[LEN_PRODUCT];       /**< 品种，期货、期权特有 */
    int32_t market;                      /**< 市场/交易所 */
    char    cl_order_id[LEN_CODE];      /**< 用户自定义的order_id */
    int32_t side;                        /**< 买卖方向，参考 OrderSide 枚举 */
    int32_t volume;                      /**< 成交数量（手数） */
    int64_t price;                       /**< 成交价格（包含滑点），扩大一万倍 */
    int64_t filled_turnover;            /**< 合约价值（现货是交易额，期货是合约价值），扩大一万倍 */
    int64_t fee;                         /**< 手续费，扩大一万倍 */
    int32_t order_volume;               /**< 委托量 */
    int64_t order_price;                /**< 委托价，扩大一万倍 */
    int64_t slippage;                   /**< 成交滑点，扩大一万倍 */
    int32_t date;                        /**< 实际发生交易日期，真实时间 */
    int32_t transact_time;               /**< 交易时间，格式HHMMSSmmm */
} OmTrade;

/* ========== 订单记录结构体（数据库/持久化） ========== */
/** 委托表对应结构体，主键：order_id + oper_date + strategy_id + run_id + account_id + account_type */
typedef struct t_OmOrder {
    char order_id[LEN_ID];              /**< 量化后台生成的ID，作为委托的唯一标识(主键) */
    int32_t oper_date;                  /**< 委托日期（主键），期货夜盘填交易归属日，格式YYYYMMDD */
    char strategy_id[LEN_CODE];         /**< 策略ID（主键） */
    char run_id[LEN_ID];                /**< 实例ID（主键） */
    char account_id[LEN_ACCOUNT_ID];    /**< 账户ID（主键） */
    int32_t account_type;               /**< 账户类型（主键） */
    char cl_order_id[LEN_CODE];        /**< 客户自定义的order_id */
    int32_t date;                       /**< 实际委托日期，真实时间 */
    int32_t market;                     /**< 标的物市场，例如：CZE, SHFE等 */
    char code[LEN_CODE];                 /**< 标的代码或合约代码 ，例如：SHFE.ag1905, SHFE.au1905*/
    char product[LEN_PRODUCT];          /**< 品种，期货期权特有，例如：ag，au */
    int32_t status;                     /**< 订单状态（枚举变量） */
    int32_t order_type;                 /**< 订单类型（枚举变量） */
    int32_t side;                       /**< 买卖方向（枚举变量），期货申请组合/拆分新增39、40 */
    int32_t margin_ratio;               /**< 保证金率，扩大一万倍 */
    int32_t volume;                     /**< 订单数量 */
    int64_t price;                      /**< 订单价格，扩大一万倍 */
    int32_t contract_multiply;         /**< 合约乘数 */
    int32_t filled_volume;              /**< 订单累计完成量 */
    int64_t filled_turnover;            /**< 订单累计完成的价值（股票交易额/期货合约价值） */
    int64_t frozen;                     /**< 该笔委托冻结的资金, 该数据为空，由本系统计算 */
    int64_t fee;                        /**< 手续费，扩大一万倍, 该数据为空，由本系统计算 */
    int32_t cancel_volume;               /**< 撤单数量 */
    int32_t cancel_flag;                /**< 撤单标识 */
    int64_t marketdata_time;            /**< 触发订单的行情时间 */
    int32_t hedge_flag;                 /**< 组合投机套保标识 */
    int64_t create_time;                /**< 订单创建时间，精确到毫秒，格式HHmmSSsss */
    int64_t update_time;                /**< 订单创建时间，精确到毫秒，格式HHmmSSsss */
    int64_t finish_time;                /**< 订单创建时间，精确到毫秒，格式HHmmSSsss */
    char security[LEN_SECURITY];        /**< 证券名称 */
    int32_t err_code;                   /**< 订单错误码 */
    char err_msg[LEN_ERR_MSG];          /**< 订单错误原因 */
} OmOrder;

/** 合约级别的持仓统计，用于快速查询持仓量/冻结量而无需遍历 position_unit 表。
 *  同时维护今仓/昨仓、多头/空头的 volume 和 frozen_volume，
 *  以支持不同交易所的平仓规则（上期所/能源所需区分平今/平昨，其他交易所无需区分）
 */
typedef struct t_ContractStat {
    char run_id[LEN_ID];               /**< 实例ID（作用域） */
    char account_id[LEN_ACCOUNT_ID];   /**< 账户ID（作用域） */
    int32_t account_type;              /**< 账户类型（作用域） */
    char strategy_id[LEN_CODE];        /**< 策略ID（作用域） */
    char code[LEN_CODE];               /**< 合约代码 */

    /* 多头持仓 */
    int32_t today_long_volume;         /**< 今仓多头持仓量（手数） */
    int32_t today_long_frozen;         /**< 今仓多头冻结量（已挂平今单但未成交） */
    int32_t yesterday_long_volume;     /**< 昨仓多头持仓量（手数） */
    int32_t yesterday_long_frozen;     /**< 昨仓多头冻结量（已挂平昨单但未成交） */

    /* 空头持仓 */
    int32_t today_short_volume;        /**< 今仓空头持仓量（手数） */
    int32_t today_short_frozen;        /**< 今仓空头冻结量（已挂平今单但未成交） */
    int32_t yesterday_short_volume;    /**< 昨仓空头持仓量（手数） */
    int32_t yesterday_short_frozen;    /**< 昨仓空头冻结量（已挂平昨单但未成交） */
} ContractStat;

/* ========== 持仓单元（按手，position_unit 表） ========== */
/** 每手一条，用于今/昨、多/空四条队列；持久化时带 scope（run_id 等）；开仓时平仓相关填 0/空，平仓后可回填；id 与表主键一致，插入时填 0 由 DB 自增，查询/更新时使用 */
typedef struct t_PositionUnit {
    int64_t id;                   /**< 主键，与 position_unit 表 id 一致；插入时 0，batchAdd 后回填 */
    char run_id[LEN_ID];                /**< 实例ID（主键） */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID（主键） */
    int32_t account_type;               /**< 资金账户类型（主键） */
    char strategy_id[LEN_CODE];         /**< 策略ID（主键） */
    char code[LEN_CODE];                /**< 合约代码，开仓时从 Order.code 冗余写入，用于按合约查询持仓 */
    char  order_id[LEN_ID];       /**< 开仓委托 ID */
    int32_t direction;           /**< 持仓方向，取 PositionSide 枚举值 */
    int64_t hold_cost;           /**< 持仓价格，扩大一万倍；开仓时为成交价，每日结算时更新为当日结算价；平仓实现盈亏用 hold_cost 与 close_price 计算 */
    int32_t open_date;           /**< 开仓日期 YYYYMMDD，区分今/昨 */
    int32_t open_time;           /**< 开仓时间 HHmmSSsss(毫秒) */
    int64_t open_price;          /**< 开仓价格，扩大一万倍，开仓后不变 */
    char  close_order_id[LEN_ID]; /**< 平仓委托 ID，未平仓为空 */
    int64_t close_price;         /**< 平仓价，扩大一万倍 */
    int32_t close_date;          /**< 平仓日期 YYYYMMDD */
    int32_t close_time;          /**< 平仓时间 HHmmSSsss(毫秒) */
    int64_t fee;                 /**< 开仓手续费+平仓手续费（扩大一万倍）：开仓时设开仓手续费，平仓时累加平仓手续费 */
    int64_t margin;              /**< 保证金（扩大一万倍）：开仓当天初始值为开仓时计算的保证金；每日结算时用结算价重算 */
    int64_t pnl;                 /**< 盈亏扩大一万倍：未平仓(close_date=0)为浮动盈亏，已平仓为平仓盈亏 */
    int32_t contract_multiply;   /**< 合约乘数，开仓时写入，之后不变 */
} PositionUnit;

/* ========== 批量平仓参数结构体 ========== */
/** 用于 batchUpdateClose 批量更新平仓字段 */
typedef struct t_PositionCloseParam {
    int64_t id;              /**< PositionUnit ID */
    int64_t fee;             /**< 开仓费 + 平仓费（扩大一万倍） */
    int64_t pnl;             /**< 平仓盈亏（扩大一万倍） */
} PositionCloseParam;

/* ========== 持仓单元历史表（position_unit_his 表）========== */
/** 持仓单元历史记录，每次平仓时将被平的持仓写入历史表
 *  与 PositionUnit 结构一致，增加 open_id 字段关联原持仓，id 为自增主键表示平仓顺序
 */
typedef struct t_PositionUnitHis {
    int64_t id;                         /**< 主键，自增，表示平仓顺序 */
    int64_t open_id;                    /**< 关联原 PositionUnit.id */
    char run_id[LEN_ID];                /**< 实例ID */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID */
    int32_t account_type;               /**< 资金账户类型 */
    char strategy_id[LEN_CODE];         /**< 策略ID */
    char code[LEN_CODE];                /**< 合约代码 */
    char order_id[LEN_ID];              /**< 开仓委托 ID */
    int32_t direction;                  /**< 持仓方向，取 PositionSide 枚举值 */
    int64_t hold_cost;                  /**< 持仓价格，扩大一万倍 */
    int32_t open_date;                  /**< 开仓日期 YYYYMMDD */
    int32_t open_time;                  /**< 开仓时间 HHmmSSsss(毫秒) */
    int64_t open_price;                 /**< 开仓价格，扩大一万倍 */
    char close_order_id[LEN_ID];        /**< 平仓委托 ID */
    int64_t close_price;                /**< 平仓价，扩大一万倍 */
    int32_t close_date;                 /**< 平仓日期 YYYYMMDD */
    int32_t close_time;                 /**< 平仓时间 HHmmSSsss(毫秒) */
    int64_t fee;                        /**< 开仓手续费+平仓手续费（扩大一万倍） */
    int64_t margin;                     /**< 保证金（扩大一万倍） */
    int64_t pnl;                        /**< 平仓盈亏（扩大一万倍） */
    int32_t contract_multiply;          /**< 合约乘数 */
} PositionUnitHis;


/* ========== 费率模块：合并结构体（手续费率 + 合约/保证金参数） ========== */
/** 一个 code 一条记录，一次查询取齐费率与保证金，避免查两次；仅内存存储，不持久化；C 风格 */
typedef struct t_FeeCodeInfo {
    char code[LEN_CODE];                /**< 合约代码，唯一标识，以 NUL 结尾 */

    /* 手续费率（原 Fee），扩大十万倍（×100000）存储，计算后除回十万倍 */
    int32_t fee_type;                   /**< 费用类型：1=RatioByMoney，2=RatioByVolume */
    int32_t open_today;                  /**< 当天开仓费率（×100000） */
    int32_t open_preday;                 /**< 长线开仓费率（×100000） */
    int32_t close_today;                 /**< 短线平仓费率（×100000） */
    int32_t close_preday;                /**< 长线平仓费率（×100000） */

    /* 合约/保证金（原 CodeInfo），费率/价格类扩大一万倍 */
    int32_t margin_long1;                /**< 投机多头保证金率 */
    int32_t margin_long2;                /**< 套保多头保证金率 */
    int32_t margin_short1;               /**< 投机空头保证金率 */
    int32_t margin_short2;               /**< 套保空头保证金率 */
    int32_t multiply;                    /**< 合约乘数 */
    int32_t price_tick;                  /**< 最小价格变动单位 */
    int32_t presettleprice;              /**< 昨结算价 */
    int32_t max_margin_side;             /**< 是否支持大额单边（0/1 或枚举） */
} FeeCodeInfo;

/* ========== 资金表记录结构体（数据库/持久化） ========== */
/** 资金表对应结构体，主键：run_id + account_id + account_type + strategy_id；用于实时记录资金变化 */
typedef struct t_Fundtable {
    char run_id[LEN_ID];                /**< 实例ID（主键） */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID（主键） */
    int32_t account_type;               /**< 资金账户类型（主键） */
    char strategy_id[LEN_CODE];         /**< 策略ID（主键） */
    int32_t currency;                   /**< 货币类型 */
    int64_t frozen_cash;                /**< 策略的冻结资金（扩大一万倍）。现货：持仓市值+委托冻结；期货：浮动盈利/亏损+委托冻结 */
    int64_t margin;                     /**< 策略保证金（扩大一万倍） */
    int64_t fee;                        /**< 手续费（扩大一万倍） */
    int64_t pnl;                        /**< 仅浮动盈亏，扩大一万倍；平仓盈亏转可用（入 avail_cash） */
    int64_t avail_cash;                 /**< 策略的可用资金，扣除手续费、含现金红利（扩大一万倍） */
    int64_t start_cash;                 /**< 当日策略的起始资金（扩大一万倍） */
    int64_t minimum_cash;               /**< 当日策略的最低可用资金，用于评估（扩大一万倍） */
    int64_t equity;                     /**< 当日策略总权益=保证金+可用资金+冻结资金+浮动盈亏，已扣手续费（扩大一万倍） */
    int64_t start_equity;               /**< 当日策略的起始权益（扩大一万倍） */
    int64_t minimum_equity;             /**< 当日策略的最小权益，用于评估最大回撤（扩大一万倍） */
} Fundtable;

/* ========== 资金表历史快照记录结构体（数据库/持久化） ========== */
typedef struct t_FundtableHis {
    char run_id[LEN_ID];                /**< 实例ID（主键） */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID（主键） */
    int32_t account_type;               /**< 资金账户类型（主键） */
    char strategy_id[LEN_CODE];         /**< 策略ID（主键） */
    int32_t oper_date;                  /**< 委托日期（主键），期货夜盘填交易归属日，格式YYYYMMDD */
    int32_t currency;                   /**< 货币类型 */
    int64_t frozen_cash;                /**< 策略的冻结资金（扩大一万倍）。现货：持仓市值+委托冻结；期货：浮动盈利/亏损+委托冻结 */
    int64_t margin;                     /**< 策略保证金（扩大一万倍） */
    int64_t fee;                        /**< 手续费（扩大一万倍） */
    int64_t pnl;                        /**< 仅浮动盈亏，扩大一万倍；平仓盈亏转可用（入 avail_cash） */
    int64_t avail_cash;                 /**< 策略的可用资金，扣除手续费、含现金红利（扩大一万倍） */
    int64_t start_cash;                 /**< 当日策略的起始资金（扩大一万倍） */
    int64_t minimum_cash;               /**< 当日策略的最低可用资金，用于评估（扩大一万倍） */
    int64_t equity;                     /**< 当日策略总权益=保证金+可用资金+冻结资金+浮动盈亏，已扣手续费（扩大一万倍） */
    int64_t start_equity;               /**< 当日策略的起始权益（扩大一万倍） */
    int64_t minimum_equity;             /**< 当日策略的最小权益，用于评估最大回撤（扩大一万倍） */
} FundtableHis;

/* ========== 账户资金表记录结构体 ========== */
typedef struct t_AccountFundtable {
    char run_id[LEN_ID];                /**< 实例ID（主键） */
    char account_id[LEN_ACCOUNT_ID];   /**< 资金账户ID（主键） */
    int32_t account_type;               /**< 资金账户类型（主键） */
    int32_t currency;                   /**< 货币类型 */
    int64_t account_frozen;             /**< 策略的冻结资金（扩大一万倍）。现货：持仓市值+委托冻结；期货：浮动盈利/亏损+委托冻结 */
    int64_t fee;                        /**< 手续费（扩大一万倍） */
    int64_t bouns;                      /**< 现金红利（扩大一万倍） */
    int64_t account_pnl;                /**< 浮动盈亏（扩大一万倍），平仓盈亏转可用 */
    int64_t account_start_cash;                 /**< 当日策略的起始资金（扩大一万倍） */
    int64_t account_cash;                 /**< 策略的可用资金，扣除手续费、含现金红利（扩大一万倍） */
    int64_t account_mincash;               /**< 当日策略的最低可用资金，用于评估（扩大一万倍） */
    int64_t account_margin;                     /**< 策略保证金（扩大一万倍） */
    int64_t account_start_equity;               /**< 当日策略的起始权益（扩大一万倍） */
    int64_t account_equity;                     /**< 当日策略总权益=保证金+可用资金+冻结资金+浮动盈亏，已扣手续费（扩大一万倍） */
    int64_t account_minequity;             /**< 当日策略的最小权益，用于评估最大回撤（扩大一万倍） */
} AccountFundtable;


/* ========== 账户资金表历史快照记录结构体 ========== */
typedef struct t_AccountFundtableHis {
    char run_id[LEN_ID];                /**< 实例ID（主键） */
    char account_id[LEN_ACCOUNT_ID];   /**< 资金账户ID（主键） */
    int32_t account_type;               /**< 资金账户类型（主键） */
    int32_t oper_date;                  /**< 委托日期（主键），期货夜盘填交易归属日，格式YYYYMMDD */
    int32_t currency;                   /**< 货币类型 */
    int64_t account_frozen;             /**< 策略的冻结资金（扩大一万倍）。现货：持仓市值+委托冻结；期货：浮动盈利/亏损+委托冻结 */
    int64_t fee;                        /**< 手续费（扩大一万倍） */
    int64_t bouns;                      /**< 现金红利（扩大一万倍） */
    int64_t account_pnl;                /**< 浮动盈亏（扩大一万倍），平仓盈亏转可用 */
    int64_t account_start_cash;                 /**< 当日策略的起始资金（扩大一万倍） */
    int64_t account_cash;                 /**< 策略的可用资金，扣除手续费、含现金红利（扩大一万倍） */
    int64_t account_mincash;               /**< 当日策略的最低可用资金，用于评估（扩大一万倍） */
    int64_t account_margin;                     /**< 策略保证金（扩大一万倍） */
    int64_t account_start_equity;               /**< 当日策略的起始权益（扩大一万倍） */
    int64_t account_equity;                     /**< 当日策略总权益=保证金+可用资金+冻结资金+浮动盈亏，已扣手续费（扩大一万倍） */
    int64_t account_minequity;             /**< 当日策略的最小权益，用于评估最大回撤（扩大一万倍） */
} AccountFundtableHis;

/* ========== 账户级持仓单元表结构体（数据库/持久化） ========== */
/** 每手一条，用于今/昨、多/空四条队列；持久化时带 scope（run_id 等）；开仓时平仓相关填 0/空，平仓后可回填；id 与表主键一致，插入时填 0 由 DB 自增，查询/更新时使用
 *  与 PositionUnit 结构一致，但无 strategy_id 字段（账户级跨策略汇总）
 */
typedef struct t_AccountPositionUnit {
    int64_t id;                         /**< 主键，与 account_position_unit 表 id 一致；插入时 0，batchAdd 后回填 */
    char run_id[LEN_ID];                /**< 实例ID */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID */
    int32_t account_type;               /**< 资金账户类型 */
    char code[LEN_CODE];                /**< 合约代码，开仓时从 Order.code 冗余写入，用于按合约查询持仓 */
    char order_id[LEN_ID];              /**< 开仓委托 ID */
    int32_t direction;                  /**< 持仓方向，取 PositionSide 枚举值 */
    int64_t hold_cost;                  /**< 持仓价格，扩大一万倍；开仓时为成交价，每日结算时更新为当日结算价；平仓实现盈亏用 hold_cost 与 close_price 计算 */
    int32_t open_date;                  /**< 开仓日期 YYYYMMDD，区分今/昨 */
    int32_t open_time;                  /**< 开仓时间 HHmmSSsss(毫秒) */
    int64_t open_price;                 /**< 开仓价格，扩大一万倍，开仓后不变 */
    char close_order_id[LEN_ID];        /**< 平仓委托 ID，未平仓为空 */
    int64_t close_price;                /**< 平仓价，扩大一万倍 */
    int32_t close_date;                 /**< 平仓日期 YYYYMMDD */
    int32_t close_time;                 /**< 平仓时间 HHmmSSsss(毫秒) */
    int64_t fee;                        /**< 开仓手续费+平仓手续费（扩大一万倍）：开仓时设开仓手续费，平仓时累加平仓手续费 */
    int64_t margin;                     /**< 保证金（扩大一万倍）：开仓当天初始值为开仓时计算的保证金；每日结算时用结算价重算 */
    int64_t pnl;                        /**< 盈亏扩大一万倍：未平仓(close_date=0)为浮动盈亏，已平仓为平仓盈亏 */
    int32_t contract_multiply;          /**< 合约乘数，开仓时写入，之后不变 */
    int64_t combination_id;             /**< 组合ID，0表示未参与组合；非0表示来自组合委托拆腿或保证金优惠申请，值为关联 CombinationUnit.id */
} AccountPositionUnit;

/* ========== 账户级批量平仓参数结构体 ========== */
/** 用于 batchUpdateClose 批量更新平仓字段 */
typedef struct t_AccountPositionCloseParam {
    int64_t id;              /**< AccountPositionUnit ID */
    int64_t fee;             /**< 开仓费 + 平仓费（扩大一万倍） */
    int64_t pnl;             /**< 平仓盈亏（扩大一万倍） */
} AccountPositionCloseParam;

/* ========== 账户级持仓单元历史表（account_position_unit_his 表）========== */
/** 账户级持仓单元历史记录，每次平仓时将被平的持仓写入历史表
 *  与 AccountPositionUnit 结构一致，增加 open_id 字段关联原持仓，id 为自增主键表示平仓顺序
 */
typedef struct t_AccountPositionUnitHis {
    int64_t id;                         /**< 主键，自增，表示平仓顺序 */
    int64_t open_id;                    /**< 关联原 AccountPositionUnit.id */
    char run_id[LEN_ID];                /**< 实例ID */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID */
    int32_t account_type;               /**< 资金账户类型 */
    char code[LEN_CODE];                /**< 合约代码 */
    char order_id[LEN_ID];              /**< 开仓委托 ID */
    int32_t direction;                  /**< 持仓方向，取 PositionSide 枚举值 */
    int64_t hold_cost;                  /**< 持仓价格，扩大一万倍 */
    int32_t open_date;                  /**< 开仓日期 YYYYMMDD */
    int32_t open_time;                  /**< 开仓时间 HHmmSSsss(毫秒) */
    int64_t open_price;                 /**< 开仓价格，扩大一万倍 */
    char close_order_id[LEN_ID];        /**< 平仓委托 ID */
    int64_t close_price;                /**< 平仓价，扩大一万倍 */
    int32_t close_date;                 /**< 平仓日期 YYYYMMDD */
    int32_t close_time;                 /**< 平仓时间 HHmmSSsss(毫秒) */
    int64_t fee;                        /**< 开仓手续费+平仓手续费（扩大一万倍） */
    int64_t margin;                     /**< 保证金（扩大一万倍） */
    int64_t pnl;                        /**< 平仓盈亏（扩大一万倍） */
    int32_t contract_multiply;          /**< 合约乘数 */
    int64_t combination_id;             /**< 组合ID，0表示未参与组合 */
} AccountPositionUnitHis;

/** 组合持仓单元，用于组合持仓的查询和统计，组合只在账户级拥有 */
typedef struct t_CombinationUnit {
    int64_t id;                         /**< 主键. 组合ID，与 combination_unit 表 id 一致；插入时 0，batchAdd 后回填 */
    char run_id[LEN_ID];                /**< 实例ID（主键） */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID（主键） */
    int32_t account_type;               /**< 资金账户类型（主键） */
    char order_id[LEN_ID];              /**< 组合委托ID*/
    char code[LEN_CODE];                /**< 组合合约代码 */
    int32_t side;                       /**< 组合方向，1=多头，2=空头，参考 PositionSide 枚举 */
    int64_t position_unit_id_a;         /**< 持仓单元A，即第一腿 ID */
    int64_t position_unit_id_b;         /**< 持仓单元B，即第二腿 ID */
    int64_t margin;                     /**< 组合优惠保证金（扩大一万倍），即优惠了多少，实际保证金=两个持仓的保证金-本优惠保证金 */
    int32_t existed_flag;               /**< 是否存在，0=不存在，1=存在，用于表示组合是否还存在*/
    int64_t create_time;                /**< 创建时间，精确到毫秒，格式YYYYMMDDHHmmSSsss */
    int64_t break_time;                 /**< 拆分时间，精确到毫秒，格式YYYYMMDDHHmmSSsss */
} CombinationUnit;

/* ========== 组合持仓单元历史表（combination_unit_his 表）========== */
/** 组合持仓单元历史记录，日终结算时将existed_flag=0的记录移动到历史表
 *  与 CombinationUnit 结构一致，增加 oper_date 字段表示结算日期
 */
typedef struct t_CombinationUnitHis {
    int64_t id;                         /**< 主键，自增 */
    int64_t combination_id;             /**< 原组合ID，对应CombinationUnit.id */
    char run_id[LEN_ID];                /**< 实例ID */
    char account_id[LEN_ACCOUNT_ID];    /**< 资金账户ID */
    int32_t account_type;               /**< 资金账户类型 */
    char order_id[LEN_ID];              /**< 组合委托ID */
    char code[LEN_CODE];                /**< 组合合约代码 */
    int32_t side;                       /**< 组合方向，1=多头，2=空头 */
    int64_t position_unit_id_a;         /**< 持仓单元A ID */
    int64_t position_unit_id_b;         /**< 持仓单元B ID */
    int64_t margin;                     /**< 组合优惠保证金（扩大一万倍） */
    int32_t existed_flag;               /**< 是否存在（移动到历史时保存原值0） */
    int64_t create_time;                /**< 创建时间，精确到毫秒 */
    int64_t break_time;                 /**< 拆分时间，精确到毫秒 */
    int32_t oper_date;                  /**< 操作日期（结算日期YYYYMMDD） */
} CombinationUnitHis;

/* ========== 账户级合约统计表结构体（数据库/持久化） ========== */
/** 合约级别的持仓统计，用于快速查询持仓量/冻结量而无需遍历 account_position_unit 表。
 *  同时维护今仓/昨仓、多头/空头的 volume 和 frozen_volume，
 *  以支持不同交易所的平仓规则（上期所/能源所需区分平今/平昨，其他交易所无需区分）
 *  与 ContractStat 结构一致，但无 strategy_id 字段（账户级跨策略汇总）
 */
typedef struct t_AccountContractStat {
    char run_id[LEN_ID];                /**< 实例ID（作用域） */
    char account_id[LEN_ACCOUNT_ID];    /**< 账户ID（作用域） */
    int32_t account_type;               /**< 账户类型（作用域） */
    char code[LEN_CODE];                /**< 合约代码 */

    /* 多头持仓 */
    int32_t today_long_volume;          /**< 今仓多头持仓量（手数） */
    int32_t today_long_frozen;          /**< 今仓多头冻结量（已挂平今单但未成交） */
    int32_t yesterday_long_volume;      /**< 昨仓多头持仓量（手数） */
    int32_t yesterday_long_frozen;      /**< 昨仓多头冻结量（已挂平昨单但未成交） */

    /* 空头持仓 */
    int32_t today_short_volume;         /**< 今仓空头持仓量（手数） */
    int32_t today_short_frozen;         /**< 今仓空头冻结量（已挂平今单但未成交） */
    int32_t yesterday_short_volume;     /**< 昨仓空头持仓量（手数） */
    int32_t yesterday_short_frozen;     /**< 昨仓空头冻结量（已挂平昨单但未成交） */
} AccountContractStat;

/* ========== 账户级盈亏变化输出结构 ========== */
/** updateFloatingPnl 的单账户作用域输出：该账户作用域本次浮盈总变化量
 *  多空方向合并计算后写入同一 AccountFundtable，故不区分 direction。
 */
typedef struct t_AccountScopePnlDelta {
    char    run_id[LEN_ID];
    char    account_id[LEN_ACCOUNT_ID];
    int32_t account_type;
    int64_t delta_pnl;                  /**< 本次浮盈变化量（多空合计，×10000）= Σ新pnl − Σ旧pnl */
} AccountScopePnlDelta;

#endif /* OM_DATA_TYPES_H */
