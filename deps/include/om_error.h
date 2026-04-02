/**
 * @brief 返回码定义，避免为取返回码导致头文件循环引用
 * @note 仅定义错误码宏，无类型与依赖；需返回码的模块直接包含本头文件即可
 *
 * 分段规则：
 *   0           成功
 *   -1 ~ -99    通用/系统级
 *   -100 ~ -199 委托处理层 (OrderProc)
 *   -200 ~ -299 持仓处理层 (PositionProc)
 *   -300 ~ -399 资金处理层 (FundtableProc)
 *   -400 ~ -579 数据层 Store（各 Store / Processor，详见各子段注释）
 *   -580 ~ -589 数据库管理层 (DbManager)
 *   -600 ~ -699 成交处理层 (TradeProc) 及组合委托处理层
 */

#ifndef OM_ERROR_H
#define OM_ERROR_H

/* ===== 通用 / 系统级 (-1 ~ -99) ===== */
#define OM_Ok                   0
#define OM_InvalidArg           (-1)
#define FundtableStore_DupKey   (-2)    /**< fundtable 主键重复，om_set_fund_config 专用 */
#define OM_NotInited            (-8)    /**< service 未初始化 */
#define OM_AlreadyInited        (-9)    /**< service 已初始化，禁止重复 init */
#define OM_MissingSettlementPrice (-10) /**< 日终结算时缺少合约结算价缓存 */
#define OM_MissingFeeInfo         (-11) /**< 日终结算时缺少合约费率缓存 */
#define OM_SettlementPnlMismatch  (-12) /**< 日终结算时结算盈亏与盘中浮动盈亏不一致 */
#define OM_FundCheckFailed        (-13) /**< 交易日初始化时账户级资金校验失败（低于策略级聚合值） */

/* ===== 委托处理层 (-100 ~ -199) ===== */
#define OrderProc_InvalidArg        (-101)  /**< 参数非法（order/fee_info 字段缺失） */
#define OrderProc_FeeCodeInvalid    (-102)  /**< fee_code_info.code 与 order.code 不匹配 */
#define OrderProc_Internal          (-103)  /**< 内部逻辑错误（不应出现的状态） */
#define OrderProc_InvalidState      (-104)  /**< 委托状态流转非法（如已终态再次到达） */
#define OrderProc_InvalidMarginRatio (-105) /**< 保证金率选择失败（direction + hedge_flag 组合无效） */
#define OrderProc_InvalidExchange   (-106)  /**< 交易所与平仓方向组合非法（委托层检查用） */

/* ===== 持仓处理层 (-200 ~ -299) ===== */
#define PositionProc_InvalidArg             (-201)  /**< 参数非法 */
#define PositionProc_NotFound               (-202)  /**< 找不到对应持仓记录 */
#define PositionProc_StoreError             (-203)  /**< Store 操作失败 */
#define PositionProc_InsufficientPosition   (-204)  /**< 可用持仓量不足，平仓被拒 */
#define PositionProc_InvalidSideForMarket   (-205)  /**< 交易所与平仓方向组合非法（持仓层检查用） */

/* ===== 资金处理层 (-300 ~ -399) ===== */
#define FundtableProc_InvalidArg    (-301)  /**< 参数非法 */
#define FundtableProc_StoreError    (-302)  /**< Store 操作失败 */
#define FundtableProc_NotFound      (-303)  /**< 找不到对应 Fundtable 记录 */

/* ===== 数据层 Store (-400 ~ -579) ===== */

/* OrderStore */
#define OrderStore_InvalidArg       (-401)
#define OrderStore_SqlError         (-402)
#define OrderStore_NotFound         (-403)  /**< 按主键未找到委托记录 */

/* PositionUnitStore */
#define PositionUnitStore_InvalidArg    (-411)
#define PositionUnitStore_SqlError      (-412)
#define PositionUnitStore_NotFound      (-413)

/* ContractStatStore */
#define ContractStatStore_InvalidArg    (-421)
#define ContractStatStore_SqlError      (-422)
#define ContractStatStore_NotFound      (-423)

/* FundtableStore */
#define FundtableStore_InvalidArg       (-431)
#define FundtableStore_SqlError         (-432)
#define FundtableStore_NotFound         (-433)

/* FundtableHisStore */
#define FundtableHisStore_InvalidArg    (-451)
#define FundtableHisStore_SqlError      (-452)
#define FundtableHisStore_NotFound      (-453)
#define FundtableHisStore_DupKey        (-454)

/* ===== 数据层 TradeStore (-460 ~ -469) ===== */
#define TradeStore_InvalidArg       (-461)  /**< 参数无效（trade为空或主键字段缺失） */
#define TradeStore_SqlError         (-462)  /**< SQL执行错误 */
#define TradeStore_NotFound           (-463)  /**< queryByPrimaryKey未找到记录 */
#define TradeStore_DupKey             (-464)  /**< insert时主键已存在 */

/* ===== 成交处理层 (-600 ~ -699) ===== */
#define TradeProc_InvalidArg        (-601)  /**< Trade 参数非法（字段缺失或无效） */
#define TradeProc_NotFound          (-602)  /**< 关联的委托记录不存在 */
#define TradeProc_StoreError        (-603)  /**< Trade 存储操作失败 */
#define TradeProc_DuplicateKey      (-604)  /**< Trade 主键重复 */

/* ===== 组合委托处理层 (-610 ~ -619) ===== */
#define OM_ComboLegCodeMismatch     (-610)  /**< 成交合约代码与组合腿不匹配 */
#define OM_ComboLegVolumeMismatch   (-611)  /**< 两腿成交量不一致 */
#define OM_ComboTradeNotFound       (-612)  /**< 组合委托无对应成交 */
#define OM_ComboInvalidFormat       (-613)  /**< 组合委托格式非法 */

/* ===== 账户级数据层 Store (-470 ~ -529) ===== */

/* AccountFundtableStore (-470 ~ -479) */
#define AccountFundtableStore_InvalidArg    (-471)
#define AccountFundtableStore_SqlError      (-472)
#define AccountFundtableStore_NotFound      (-473)
#define AccountFundtableStore_DupKey        (-474)

/* AccountFundtableHisStore (-480 ~ -489) */
#define AccountFundtableHisStore_InvalidArg    (-481)
#define AccountFundtableHisStore_SqlError      (-482)
#define AccountFundtableHisStore_NotFound      (-483)
#define AccountFundtableHisStore_DupKey        (-484)

/* AccountFundtableProcessor (-490 ~ -499) */
#define AccountFundtableProc_InvalidArg     (-491)
#define AccountFundtableProc_StoreError     (-492)
#define AccountFundtableProc_NotFound       (-493)

/* AccountPositionUnitStore (-500 ~ -509) */
#define AccountPositionUnitStore_InvalidArg    (-501)
#define AccountPositionUnitStore_SqlError      (-502)
#define AccountPositionUnitStore_NotFound      (-503)

/* AccountContractStatStore (-510 ~ -519) */
#define AccountContractStatStore_InvalidArg    (-511)
#define AccountContractStatStore_SqlError      (-512)
#define AccountContractStatStore_NotFound      (-513)

/* AccountPositionProcessor (-520 ~ -529) */
#define AccountPositionProc_InvalidArg             (-521)
#define AccountPositionProc_StoreError             (-522)
#define AccountPositionProc_NotFound               (-523)
#define AccountPositionProc_InsufficientPosition   (-524)

/* CombinationUnitStore (-530 ~ -539) */
#define CombinationUnitStore_InvalidArg            (-531)
#define CombinationUnitStore_SqlError              (-532)

/* CombinationUnitHisStore (-533 ~ -534) */
#define CombinationUnitHisStore_InvalidArg         (-533)
#define CombinationUnitHisStore_SqlError           (-534)

/* PositionUnitHisStore (-540 ~ -549) */
#define PositionUnitHisStore_InvalidArg              (-541)
#define PositionUnitHisStore_SqlError                (-542)

/* AccountPositionUnitHisStore (-550 ~ -559) */
#define AccountPositionUnitHisStore_InvalidArg       (-551)
#define AccountPositionUnitHisStore_SqlError         (-552)

/* OrderHisStore (-560 ~ -569) */
#define OrderHisStore_InvalidArg                     (-561)
#define OrderHisStore_SqlError                       (-562)

/* TradeHisStore (-570 ~ -579) */
#define TradeHisStore_InvalidArg                     (-571)
#define TradeHisStore_SqlError                       (-572)

/* ===== 数据库管理层 (-580 ~ -589) ===== */
#define DbManager_OpenFailed        (-581)  /**< 数据库文件打开/创建失败 */
#define DbManager_TxError           (-582)  /**< 事务操作失败（BEGIN/COMMIT），db_ 为空或 SQLite 返回非 SQLITE_OK */

/* ===== QueryKitPool (-700 ~ -799) ===== */
#define QueryKitPool_NotInited      (-701)  /**< 套件池未初始化 */
#define QueryKitPool_AlreadyInited  (-702)  /**< 套件池重复初始化 */
#define QueryKitPool_InvalidArg     (-703)  /**< 参数非法 */
#define QueryKitPool_DbOpenFailed   (-704)  /**< 套件数据库连接打开失败 */
#define QueryKitPool_StoreInitFailed (-705) /**< 套件 Store 初始化失败 */

#endif /* OM_ERROR_H */
