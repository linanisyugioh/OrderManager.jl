module OrderManager

using CBinding
using Pkg.Artifacts
import FinancialStruct.cFuOrder as cOrder      
import FinancialStruct.cFuTrade as cTrade  
import FinancialStruct.cFuCodeInfo as cCodeInfo

using FinancialStruct:cFeeCodeInfo
using FinancialStruct:cOmTrade
using FinancialStruct:cOmOrder
using FinancialStruct:cContractStat
using FinancialStruct:cAccountContractStat
using FinancialStruct:cPositionUnit
#using FinancialStruct:cPositionCloseParam
#using FinancialStruct:cPositionUnitHis
using FinancialStruct:cFundtable
using FinancialStruct:cAccountFundtable
#using FinancialStruct:cFundtableHis
#using FinancialStruct:cAccountFundtableHis
#using FinancialStruct:cAccountPositionUnit
#using FinancialStruct:cAccountPositionCloseParam
#using FinancialStruct:cAccountPositionUnitHis
#using FinancialStruct:cCombinationUnit
#using FinancialStruct:cCombinationUnitHis
#using FinancialStruct:cAccountScopePnlDelta
using FinancialStruct:cExternalPosition

# 使用 Artifacts 动态加载库文件
function __init__()
    # 确保 artifact 可用
    lib_dir = artifact"ordermanager_lib"
    # 根据平台设置库路径
    global dlfile
    if Sys.iswindows()
        dlfile = joinpath(lib_dir, "om.dll")
    elseif Sys.islinux()
        dlfile = joinpath(lib_dir, "libom.so")
    end
    # 验证库文件是否存在
    if !isfile(dlfile)
        @error "OrderManager library files not found. Please make sure the package is installed correctly."
    end
    global lib = Libc.Libdl.dlopen(dlfile)
end

# ==================== 辅助函数 ====================

"""将 NTuple 转换为 String"""
function tuple_to_string(t::NTuple{N, Cchar}) where N
    bytes = [t[i] for i in 1:N if t[i] != 0]
    return String(bytes)
end

"""将 String 转换为 NTuple"""
function string_to_tuple(s::String, len::Int)
    bytes = Vector{Cchar}(undef, len)
    for i in 1:len
        bytes[i] = i <= length(s) ? Cchar(s[i]) : Cchar(0)
    end
    return NTuple{len, Cchar}(bytes)
end

# ==================== Manager API ====================

"""
    om_init(work_dir::String)::Cint
初始化管理实例：初始化日志、数据库、各 Store 与 service 组件。

@param work_dir 工作目录（必须可写）；日志写入 work_dir/logs，数据库为 work_dir/om.db
@return 0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -8(OM_NotInited) service 未初始化；
        -9(OM_AlreadyInited) service 已初始化，禁止重复 init；
        -581(DbManager_OpenFailed) 数据库文件打开/创建失败；
        -582(DbManager_TxError) 事务操作失败
"""
function om_init(work_dir::String)::Cint
    sym = Libc.Libdl.dlsym(lib, :om_init)
    err = ccall(sym, Int32, (Ptr{UInt8},), work_dir)
    return err
end
export om_init

"""
    om_release()
释放管理实例：关闭数据库连接、释放资源；与 om_init 配对调用
"""
function om_release()::Cvoid
    sym = Libc.Libdl.dlsym(lib, :om_release)
    ccall(sym, Cvoid, ())
    nothing
end
export om_release

"""
    om_trading_day_update(trading_date::Integer)::Cint
交易日更新（新交易日开始，开盘前调用）：进入指定交易日；清空 order 表，并移除 position 表中所有已平仓的记录。

【FeeCodeInfo 说明】合约基础信息（乘数、保证金率等）不再由此接口传入，而是通过 om_add_fee_info 逐个传入。
建议流程：交易日初始化成功后，调用 om_set_query_scope 设置账户作用域，再调用 om_query_account_position_codes
获取持仓 codes，遍历调用 om_add_fee_info 将各合约的 FeeCodeInfo 传入。

@param trading_date 交易日（YYYYMMDD），表示进入该交易日
@return 0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -8(OM_NotInited) service 未初始化；
        -13(OM_FundCheckFailed) 交易日初始化时账户级资金校验失败（低于策略级聚合值）；
        -14(OM_PositionCheckFailed) 交易日初始化时账户级持仓校验失败（与策略级聚合不匹配）
"""
function om_trading_day_update(trading_date::Integer)::Cint
    sym = Libc.Libdl.dlsym(lib, :om_trading_day_update)
    err = ccall(sym, Cint, (Cint,), trading_date)
    return err
end
export om_trading_day_update

"""
    om_add_fee_info(fee_info::FeeCodeInfo)::Cint
添加合约基础信息至缓存（供日终结算等使用）。
可在 om_trading_day_update 成功后，按持仓 codes 遍历调用，逐个传入各合约的 FeeCodeInfo。

@param fee_info 合约基础信息（合约乘数、保证金率等），手续费字段可留空
@return 0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -8(OM_NotInited) service 未初始化
"""
function om_add_fee_info(fee_info::cFeeCodeInfo)::Cint
    fee_info_r = Ref{cFeeCodeInfo}(fee_info)
    sym = Libc.Libdl.dlsym(lib, :om_add_fee_info)
    err = ccall(sym, Int32, (Cptr{cFeeCodeInfo},), fee_info_r)
    return err
end

function om_add_fee_info(fee_infos::Vector{cFeeCodeInfo})::nothing
    for fee_info in fee_infos
        om_add_fee_info(fee_info)
    end
    return nothing
end
export om_add_fee_info

"""
    om_trading_day_end(force_settle_with_last_price::Integer=0)::String
交易日结束：触发日终结算，逐手计算结算盈亏并转入可用资金，
更新持仓价为结算价，重算保证金。

【增强功能】
1. 强制结算模式：当 force_settle_with_last_price=1 时，如果某个合约的结算价为 0 但最新价存在且非 0，
   系统将使用最新价代替结算价进行结算。此模式用于紧急场景（如交易所未提供结算价）。
2. 缺失代码报告：非强制模式下当返回 OM_MissingSettlementPrice 时，
   返回值中的 missing_codes 将包含缺失结算价合约列表（逗号分隔）。

【结算价要求】
日终结算前必须通过 om_handle_newprice 传入所有持仓合约的价格信息。
系统会重新计算结算盈亏，并与盘中累计的浮动盈亏进行对比。
若两者不一致（差值 > 误差阈值），将记录 ERROR 日志并返回 OM_SettlementPnlMismatch，
但结算流程仍按重新计算的结果完成。

@param force_settle_with_last_price 是否强制使用最新价结算
  - 0 (false)：严格模式，所有持仓合约必须存在且 settlement_price > 0
  - 1 (true)：强制模式，settlement_price 为 0 时自动使用 last_price 替代
@return 缺失结算价的合约代码列表（逗号分隔），仅在返回 OM_MissingSettlementPrice 时有效；
        其他情况返回空字符串

错误码（通过 err 值获取）：
  - 0(OM_Ok) 成功；
  - -1(OM_InvalidArg) 参数非法；
  - -8(OM_NotInited) service 未初始化；
  - -10(OM_MissingSettlementPrice) 缺少结算价（仅在非强制模式返回）；
  - -11(OM_MissingFeeInfo) 缺少费率缓存；
  - -12(OM_SettlementPnlMismatch) 结算盈亏与盘中盈亏不一致（警告，结算仍完成）
"""
function om_trading_day_end(force_settle_with_last_price::Integer=0)::String
    sym = Libc.Libdl.dlsym(lib, :om_trading_day_end)
    missing_codes_ptr = Ref{Ptr{UInt8}}()
    err = ccall(sym, Cint, (Cint, Ptr{Ptr{UInt8}}), force_settle_with_last_price, missing_codes_ptr)
    if err == 0
        missing_codes = ""
    else
        if missing_codes_ptr[] == C_NULL
            missing_codes = ""
        else
            missing_codes = unsafe_string(missing_codes_ptr[])
        end
    end
    return missing_codes
end
export om_trading_day_end

"""
    om_handle_order(order::OmOrder, fee_info::FeeCodeInfo)::Cint
接收最新委托，驱动 Order → Position → Fundtable 业务流程。
费率和手续费等合约基本信息不由本系统维护，由调用方随该笔委托一并传入。

Order 必填字段：
  - 主键：order_id, oper_date, strategy_id, run_id, account_id, account_type
  - 业务：code, market, side, status, volume, price（开仓时）
  - 成交时：filled_volume, filled_turnover（filled_turnover = 均价×multiply×filled_volume）
  - 系统计算字段（frozen/fee/margin_ratio）入参填 0

@param order 委托最新状态
@param fee_info 与该委托标的对应的手续费率与合约/保证金参数
@return 0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -8(OM_NotInited) service 未初始化；
        委托处理层(-100 ~ -199)：
          -101(OrderProc_InvalidArg) 参数非法（order/fee_info 字段缺失）；
          -102(OrderProc_FeeCodeInvalid) fee_code_info.code 与 order.code 不匹配；
          -103(OrderProc_Internal) 内部逻辑错误（不应出现的状态）；
          -104(OrderProc_InvalidState) 委托状态流转非法（如已终态再次到达）；
          -105(OrderProc_InvalidMarginRatio) 保证金率选择失败（direction + hedge_flag 组合无效）；
          -106(OrderProc_InvalidExchange) 交易所与平仓方向组合非法（委托层检查用）；
        持仓处理层(-200 ~ -299)：
          -201(PositionProc_InvalidArg) 参数非法；
          -202(PositionProc_NotFound) 找不到对应持仓记录；
          -203(PositionProc_StoreError) Store 操作失败；
          -204(PositionProc_InsufficientPosition) 可用持仓量不足，平仓被拒；
          -205(PositionProc_InvalidSideForMarket) 交易所与平仓方向组合非法（持仓层检查用）；
        资金处理层(-300 ~ -399)：
          -301(FundtableProc_InvalidArg) 参数非法；
          -302(FundtableProc_StoreError) Store 操作失败；
          -303(FundtableProc_NotFound) 找不到对应 Fundtable 记录
"""
function om_handle_order(order::cOmOrder, fee_info::cFeeCodeInfo)::Cint
    sym = Libc.Libdl.dlsym(lib, :om_handle_order)
    err = ccall(sym, Int32, (cOmOrder, cFeeCodeInfo), order, fee_info)
    return err
end
export om_handle_order

"""
    om_handle_newprice(code::String, last_price::Integer, pre_settlement_price::Integer, settlement_price::Integer)::Cint
更新合约最新价，刷新该合约下所有未平仓持仓的浮动盈亏（pnl）；
同时写入最新价和结算价缓存供开仓和日终结算使用。

【结算价逻辑】
1. 若 settlement_price > 0，表示收盘结算，使用结算价计算盈亏变化
   - 有缓存最新价时：价差 = settlement_price - 缓存最新价
   - 无缓存最新价时：价差 = settlement_price - pre_settlement_price
2. 若 settlement_price == 0，表示盘中行情，使用最新价计算盈亏变化
   - 有缓存最新价时：价差 = last_price - 缓存最新价
   - 无缓存最新价时：价差 = last_price - pre_settlement_price
3. 若缓存中已有 settlement_price > 0，直接返回，代表当天已终态

@param code 合约代码，与 position/position_unit 的 code 一致，非空
@param last_price 最新价，扩大一万倍
@param pre_settlement_price 昨结算价，扩大一万倍
@param settlement_price 今结算价，扩大一万倍；0表示盘中行情（无效）
@return 0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -8(OM_NotInited) service 未初始化
"""
function om_handle_newprice(code::String, last_price::Integer, pre_settlement_price::Integer, settlement_price::Integer)::Cint
    sym = Libc.Libdl.dlsym(lib, :om_handle_newprice)
    err = ccall(sym, Int32, (Ptr{UInt8}, Int64, Int64, Int64), code, last_price, pre_settlement_price, settlement_price)
    return err
end
export om_handle_newprice

"""
    om_set_fund_config(fund::Fundtable)::Cint
写入资金配置（建初值）：按 run_id、account_id、account_type、strategy_id 唯一确定一条资金记录，
写入 data_store 供业务处理使用。
写入前检测数据库中是否已存在对应主键记录；已存在则返回错误，不覆盖。

@param fund 资金配置，主键及金额等字段须有效（avail_cash、start_cash、equity 等扩大一万倍）
@return 0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -2(FundtableStore_DupKey) fundtable 主键重复；
        -8(OM_NotInited) service 未初始化；
        资金 Store 层(-430 ~ -439)：
          -431(FundtableStore_InvalidArg) 参数非法；
          -432(FundtableStore_SqlError) SQL执行错误；
          -433(FundtableStore_NotFound) 找不到对应记录
"""
function om_set_fund_config(fund::cFundtable)::Cint
    fund_r = Ref{cFundtable}(fund)
    sym = Libc.Libdl.dlsym(lib, :om_set_fund_config)
    err = ccall(sym, Cint, (Cptr{cFundtable},), fund_r)
    return err
end
export om_set_fund_config

"""
    om_set_account_fund_config(fund::AccountFundtable)::Cint
写入账户级资金配置（建初值）：按 run_id、account_id、account_type 唯一确定一条账户级资金记录。
账户级资金是跨策略的汇总，用于风控和总权益计算。
写入前检测数据库中是否已存在对应主键记录；已存在则返回错误，不覆盖。

@param fund 账户级资金配置，主键及金额等字段须有效（account_cash、account_start_cash、account_equity 等扩大一万倍）
@return 0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -8(OM_NotInited) service 未初始化；
        账户级资金 Store 层(-470 ~ -479)：
          -471(AccountFundtableStore_InvalidArg) 参数非法；
          -472(AccountFundtableStore_SqlError) SQL执行错误；
          -473(AccountFundtableStore_NotFound) 找不到对应记录；
          -474(AccountFundtableStore_DupKey) 主键重复
"""
function om_set_account_fund_config(fund::cAccountFundtable)::Cint
    fund_r = Ref{cAccountFundtable}(fund)
    sym = Libc.Libdl.dlsym(lib, :om_set_account_fund_config)
    err = ccall(sym, Cint, (Cptr{cAccountFundtable},), fund_r)
    return err
end
export om_set_account_fund_config

"""
    om_handle_trade(trade::OmTrade)::Cint
接收成交回报，写入成交记录至 trade 表。

【当前实现】仅校验字段后直接入库，不参与持仓计算和资金计算，也不校验关联 Order 是否存在。
与 om_handle_order 的区别：
  - om_handle_order：委托状态驱动，处理 PendingNew → Filled 全流程（含持仓、资金）
  - om_handle_trade：成交回报驱动，当前仅写入成交明细

Trade 必填字段：
  - 主键（7字段）：order_id, trade_date, strategy_id, run_id, account_id, account_type, match_seqno
  - 业务：code, side, volume, price
  - 辅助：filled_turnover, fee, order_volume, order_price, slippage, date, transact_time, match_type

@param trade 成交数据（按值传入，内部转交业务流程类处理）
@return 0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -8(OM_NotInited) service 未初始化；
        成交处理层(-600 ~ -699)：
          -601(TradeProc_InvalidArg) Trade 参数非法（字段缺失或无效）；
          -602(TradeProc_NotFound) 关联的委托记录不存在；
          -603(TradeProc_StoreError) Trade 存储操作失败；
          -604(TradeProc_DuplicateKey) Trade 主键重复；
        成交 Store 层(-460 ~ -469)：
          -461(TradeStore_InvalidArg) 参数无效（trade为空或主键字段缺失）；
          -462(TradeStore_SqlError) SQL执行错误；
          -463(TradeStore_NotFound) queryByPrimaryKey未找到记录；
          -464(TradeStore_DupKey) insert时主键已存在
"""
function om_handle_trade(trade::cOmTrade)::Cint
    sym = Libc.Libdl.dlsym(lib, :om_handle_trade)
    err = ccall(sym, Int32, (cOmTrade,), trade)
    return err
end
export om_handle_trade

# ==================== Query API ====================

"""
    om_set_query_scope(run_id::String, account_id::String, account_type::Integer)::Cint
设置查询作用域（run_id, account_id, account_type）。

设置后，所有简化版查询接口（om_query.h中的接口）都会使用此作用域进行查询。
通常在系统初始化或账户切换时调用。

@param run_id 实例ID（非空）
@param account_id 账户ID（非空）
@param account_type 账户类型
@return 0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -8(OM_NotInited) service 未初始化
"""
function om_set_query_scope(run_id::String, account_id::String, account_type::Integer)::Cint
    sym = Libc.Libdl.dlsym(lib, :om_set_query_scope)
    err = ccall(sym, Int32, (Ptr{UInt8}, Ptr{UInt8}, Int32), run_id, account_id, account_type)
    return err
end
export om_set_query_scope

"""
    om_get_query_run_id()::String
获取当前缓存的查询作用域 run_id。
"""
function om_get_query_run_id()::String
    sym = Libc.Libdl.dlsym(lib, :om_get_query_run_id)
    ptr = ccall(sym, Ptr{UInt8}, ())
    return unsafe_string(ptr)
end
export om_get_query_run_id

"""
    om_get_query_account_id()::String
获取当前缓存的查询作用域 account_id。
"""
function om_get_query_account_id()::String
    sym = Libc.Libdl.dlsym(lib, :om_get_query_account_id)
    ptr = ccall(sym, Ptr{UInt8}, ())
    return unsafe_string(ptr)
end
export om_get_query_account_id

"""
    om_get_query_account_type()::Cint
获取当前缓存的查询作用域 account_type。
"""
function om_get_query_account_type()::Cint
    sym = Libc.Libdl.dlsym(lib, :om_get_query_account_type)
    return ccall(sym, Int32, ())
end
export om_get_query_account_type

"""
    om_query_order(order_id::String, oper_date::Integer, strategy_id::String)::Union{OmOrder,Nothing}
按主键查询单条委托（使用缓存作用域）。

@param order_id 委托ID
@param oper_date 委托日期YYYYMMDD
@param strategy_id 策略ID
@return 查询结果或 nothing；nothing 表示未找到或出错

错误码：
  - 0(OM_Ok) 成功；
  - -1(OM_InvalidArg) 参数非法；
  - -8(OM_NotInited) service 未初始化；
  委托 Store 层(-400 ~ -409)：
    -401(OrderStore_InvalidArg) 参数非法；
    -402(OrderStore_SqlError) SQL执行错误；
    -403(OrderStore_NotFound) 按主键未找到委托记录
"""
function om_query_order(order_id::String, oper_date::Integer, strategy_id::String)::Union{cOmOrder,Nothing}
    out = Ref{cOmOrder}()
    sym = Libc.Libdl.dlsym(lib, :om_query_order)
    err = ccall(sym, Int32, (Ptr{UInt8}, Int32, Ptr{UInt8}, Cptr{cOmOrder}), order_id, oper_date, strategy_id, out)
    if err == 0
        return out[]
    else
        return nothing
    end
end
export om_query_order

"""
    om_query_order_ids(strategy_id::String, status::Integer, code::String="", side::Integer=3, bs::Integer=3)::String
查询 strategy_id 下委托的 order_id 列表（使用缓存作用域）。

每次查询将逗号分隔的 order_id 字符串写入 service 内该策略的缓存，通过返回值返回
指向该缓存的指针。返回格式：order_id 之间用 "," 分割，无数据时为空字符串。
指针在下次对同一策略的相同参数查询或 om_release 前有效。

@param strategy_id 策略ID（非空）
@param status 0=未终态委托，1=已终态，2=所有状态
@param code 合约代码，空字符串表示所有合约
@param side 0=平，1=开，3=全部
@param bs 0=空，1=多，3=全部
@return 逗号分隔的 order_id 字符串

错误码：
  - 0(OM_Ok) 成功；
  - -1(OM_InvalidArg) 参数非法；
  - -8(OM_NotInited) service 未初始化
"""
function om_query_order_ids(strategy_id::String, status::Integer, code::String, side::Integer, bs::Integer)::String
    out_ptr = Ref{Ptr{UInt8}}()
    sym = Libc.Libdl.dlsym(lib, :om_query_order_ids)
    code_ptr = isempty(code) ? C_NULL : pointer(code)
    err = ccall(sym, Int32, (Ptr{UInt8}, Cint, Ptr{UInt8}, Cint, Cint, Ptr{Ptr{UInt8}}), strategy_id, status, code_ptr, side, bs, out_ptr)
    if err == 0 && out_ptr[] != C_NULL
        return unsafe_string(out_ptr[])
    else
        return ""
    end
end
export om_query_order_ids

"""
    om_query_position_codes(strategy_id::String, status::Integer, period::Integer, side::Integer)::String
查询 strategy_id 下持仓的 code 列表（使用缓存作用域）。

每次查询将逗号分隔的 code 字符串写入 service 内该策略的缓存，通过返回值返回
指向该缓存的指针。返回格式：code 之间用 "," 分割，无数据时为空字符串。
指针在下次对同一策略的相同参数查询或 om_release 前有效。

@param strategy_id 策略ID（非空）
@param status 0=冻结，1=可用，2=全部
@param period 0=昨仓，1=今仓，2=全部
@param side 0=空，1=多，2=全部
@return 逗号分隔的 code 字符串

错误码：
  - 0(OM_Ok) 成功；
  - -1(OM_InvalidArg) 参数非法；
  - -8(OM_NotInited) service 未初始化
"""
function om_query_position_codes(strategy_id::String, status::Integer, period::Integer, side::Integer)::String
    out_ptr = Ref{Ptr{UInt8}}()
    sym = Libc.Libdl.dlsym(lib, :om_query_position_codes)
    err = ccall(sym, Int32, (Ptr{UInt8}, Int32, Int32, Int32, Ref{Ptr{UInt8}}), strategy_id, status, period, side, out_ptr)
    if err == 0 && out_ptr[] != C_NULL
        return unsafe_string(out_ptr[])
    else
        return ""
    end
end
export om_query_position_codes

"""
    om_query_account_position_codes()::String
查询账户级持仓的 code 列表（使用缓存作用域）。

返回账户下全部未平仓持仓的 distinct code，逗号分隔。
用于交易日初始化后获取持仓 codes，遍历调用 om_add_fee_info 传入各合约 FeeCodeInfo。

每次查询将逗号分隔的 code 字符串写入 service 缓存，通过返回值返回指向该缓存的指针。
返回格式：code 之间用 "," 分割，无数据时为空字符串。
指针在下次查询或 om_release 前有效。

@return 逗号分隔的 code 字符串

错误码：
  - 0(OM_Ok) 成功；
  - -1(OM_InvalidArg) 参数非法；
  - -8(OM_NotInited) service 未初始化
"""
function om_query_account_position_codes()::String
    out_ptr = Ref{Ptr{UInt8}}()
    sym = Libc.Libdl.dlsym(lib, :om_query_account_position_codes)
    err = ccall(sym, Int32, (Ref{Ptr{UInt8}},), out_ptr)
    if err == 0 && out_ptr[] != C_NULL
        return unsafe_string(out_ptr[])
    else
        return ""
    end
end
export om_query_account_position_codes

"""
    om_query_contract_stat(strategy_id::String, code::String)::Union{ContractStat,Nothing}
查询合约统计（使用缓存作用域）。

@param strategy_id 策略ID
@param code 合约代码
@return 查询结果或 nothing；nothing 表示未找到或出错

错误码：
  - 0(OM_Ok) 成功；
  - -1(OM_InvalidArg) 参数非法；
  - -8(OM_NotInited) service 未初始化；
  合约统计 Store 层(-420 ~ -429)：
    -421(ContractStatStore_InvalidArg) 参数非法；
    -422(ContractStatStore_SqlError) SQL执行错误；
    -423(ContractStatStore_NotFound) 找不到对应记录
"""
function om_query_contract_stat(strategy_id::String, code::String)::Union{cContractStat,Nothing}
    out = Ref{cContractStat}()
    sym = Libc.Libdl.dlsym(lib, :om_query_contract_stat)
    err = ccall(sym, Int32, (Ptr{UInt8}, Ptr{UInt8}, Cptr{cContractStat}), strategy_id, code, out)
    if err == 0
        return out[]
    else
        return nothing
    end
end
export om_query_contract_stat

"""
    om_query_account_contract_stat(code::String)::Union{AccountContractStat,Nothing}
查询账户级合约统计（使用缓存作用域）。

@param code 合约代码
@return 查询结果或 nothing；nothing 表示未找到或出错

错误码：
  - 0(OM_Ok) 成功；
  - -1(OM_InvalidArg) 参数非法；
  - -8(OM_NotInited) service 未初始化；
  账户级合约统计 Store 层(-510 ~ -519)：
    -511(AccountContractStatStore_InvalidArg) 参数非法；
    -512(AccountContractStatStore_SqlError) SQL执行错误；
    -513(AccountContractStatStore_NotFound) 找不到对应记录
"""
function om_query_account_contract_stat(code::String)::Union{cAccountContractStat,Nothing}
    out = Ref{cAccountContractStat}()
    sym = Libc.Libdl.dlsym(lib, :om_query_account_contract_stat)
    err = ccall(sym, Int32, (Ptr{UInt8}, Cptr{cAccountContractStat}), code, out)
    if err == 0
        return out[]
    else
        return nothing
    end
end
export om_query_account_contract_stat

"""
    om_query_fund(strategy_id::String)::Union{Fundtable,Nothing}
查询策略级资金（使用缓存作用域）。

@param strategy_id 策略ID
@return 查询结果或 nothing；nothing 表示未找到或出错

错误码：
  - 0(OM_Ok) 成功；
  - -1(OM_InvalidArg) 参数非法；
  - -8(OM_NotInited) service 未初始化；
  资金 Store 层(-430 ~ -439)：
    -431(FundtableStore_InvalidArg) 参数非法；
    -432(FundtableStore_SqlError) SQL执行错误；
    -433(FundtableStore_NotFound) 找不到对应记录
"""
function om_query_fund(strategy_id::String)::Union{cFundtable,Nothing}
    out = Ref{cFundtable}()
    sym = Libc.Libdl.dlsym(lib, :om_query_fund)
    err = ccall(sym, Int32, (Ptr{UInt8}, Cptr{cFundtable}), strategy_id, out)
    if err == 0
        return out[]
    else
        return nothing
    end
end
export om_query_fund

"""
    om_query_account_fund()::Union{AccountFundtable,Nothing}
查询账户级资金（使用缓存作用域）。

@return 查询结果或 nothing；nothing 表示未找到或出错

错误码：
  - 0(OM_Ok) 成功；
  - -1(OM_InvalidArg) 参数非法；
  - -8(OM_NotInited) service 未初始化；
  账户级资金 Store 层(-470 ~ -479)：
    -471(AccountFundtableStore_InvalidArg) 参数非法；
    -472(AccountFundtableStore_SqlError) SQL执行错误；
    -473(AccountFundtableStore_NotFound) 找不到对应记录
"""
function om_query_account_fund()::Union{cAccountFundtable,Nothing}
    out = Ref{cAccountFundtable}()
    sym = Libc.Libdl.dlsym(lib, :om_query_account_fund)
    err = ccall(sym, Int32, (Cptr{cAccountFundtable},), out)
    if err == 0
        return out[]
    else
        return nothing
    end
end
export om_query_account_fund

# ==================== HFT Adapter API ====================

"""
    om_add_fee_info_hft(hft_code_info::HftCodeInfo)::Cint
添加合约基础信息至缓存（HFT 适配，对应 om_add_fee_info）。
将 HftCodeInfo 转换为 FeeCodeInfo 后调用 om_add_fee_info。

@param hft_code_info HFT 合约信息（symbol、乘数、保证金率、手续费等）
@return 0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -8(OM_NotInited) service 未初始化
"""
function om_add_fee_info_hft(hft_code_info::cCodeInfo)::Cint
    hft_code_info_r = Ref{cCodeInfo}(hft_code_info)
    sym = Libc.Libdl.dlsym(lib, :om_add_fee_info_hft)
    err = ccall(sym, Int32, (Cptr{cCodeInfo},), hft_code_info_r)
    return err
end
export om_add_fee_info_hft

"""
    om_handle_order_hft(hft_order, hft_code_info)::Cint
接收 HFT 委托回报，驱动 Order → Position → Fundtable 业务流程。
等价于 om_handle_order，但入参为 HFT 的 Order 与 HftCodeInfo 结构。

转换规则：
  - HFT symbol (市场.合约ID) → OM code
  - 从 symbol 解析市场前缀映射为 Exchange 枚举（SHFE/DCE/CZCE/CFFEX/INE/GFEX 等）
  - 从 symbol 解析品种（合约代码中首个数字前的字母部分）填入 product
  - order_status → status, order_type → order_type, side → side（枚举值一致）
  - hft_code_info 内部转换为 FeeCodeInfo 后调用 om_handle_order

@param hft_order HFT 委托结构指针（HftOrder，与 hft Order 布局一致）
@param hft_code_info 与该委托标的（symbol）对应的 HFT 合约信息（手续费、保证金等）
@return 同 om_handle_order：
        0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -8(OM_NotInited) service 未初始化；
        委托处理层(-100 ~ -199)：-101 ~ -106；
        持仓处理层(-200 ~ -299)：-201 ~ -205；
        资金处理层(-300 ~ -399)：-301 ~ -303
"""
function om_handle_order_hft(hft_order::cOrder, hft_code_info::cCodeInfo)::Cint
    hft_order_r = Ref{cOrder}(hft_order)
    hft_code_info_r = Ref{cCodeInfo}(hft_code_info)
    sym = Libc.Libdl.dlsym(lib, :om_handle_order_hft)
    err = ccall(sym, Int32, (Cptr{cOrder}, Cptr{cCodeInfo}), hft_order_r, hft_code_info_r)
    return err
end
export om_handle_order_hft

"""
    om_handle_trade_hft(hft_trade)::Cint
接收 HFT 成交回报，写入成交记录至 trade 表。
等价于 om_handle_trade，但入参为 HFT 的 Trade 结构。
【当前实现】仅校验字段后直接入库，不参与持仓计算和资金计算。

转换规则：
  - HFT exec_id → OM match_seqno
  - HFT exec_type → OM match_type
  - HFT turnover → OM filled_turnover（HFT 未乘合约乘数，从系统 fee 缓存获取乘数后补乘）
  - transact_time：HFT 微秒 → OM 毫秒（/1000）

【前置】须先通过 om_add_fee_info_hft 将合约信息缓存，否则返回 OM_MissingFeeInfo

@param hft_trade HFT 成交结构指针（HftTrade，与 hft Trade 布局一致）
@return 同 om_handle_trade：
        0(OM_Ok) 成功；
        -1(OM_InvalidArg) 参数非法；
        -8(OM_NotInited) service 未初始化；
        -11(OM_MissingFeeInfo) 缓存中未找到该合约（symbol）的费率信息；
        成交处理层(-600 ~ -699)：-601 ~ -604；
        成交 Store 层(-460 ~ -469)：-461 ~ -464
"""
function om_handle_trade_hft(hft_trade::cTrade)::Cint
    hft_trade_r = Ref{cTrade}(hft_trade)
    sym = Libc.Libdl.dlsym(lib, :om_handle_trade_hft)
    err = ccall(sym, Int32, (Cptr{cTrade},), hft_trade_r)
    return err
end
export om_handle_trade_hft

# ==================== 资产校验 API ====================

"""
    om_verify_assets(run_id::String, account_id::String, account_type::Integer,
                     min_required_equity::Integer, positions::Vector{ExternalPosition})::Cint
资产校验接口

【功能说明】
校验本系统维护的资产数据是否满足外部系统的要求：
1. 资金校验：OM系统的账户权益 >= 传入的最低资金要求
2. 持仓校验：OM系统的持仓必须与外部持仓严格一致（无容忍度）

【校验逻辑详细说明】

1. 资金校验（底限校验）：
   - 查询OM系统的 account_equity（账户总权益）
   - 检查：account_equity >= min_required_equity
   - 若不满足，返回 AssetVerify_FundInsufficient
   - 日志记录：OM权益值、最低要求值、差额（min_required - account_equity）

2. 持仓校验（严格相等，无容忍度）：
   - 将OM系统的持仓按合约聚合（今仓+昨仓），区分多空
   - 逐合约比较：
     a. 外部有但OM没有 → AssetVerify_PositionMissingInOM
     b. OM有但外部没有 → AssetVerify_PositionMissingInExternal
     c. 多头数量不一致 → AssetVerify_PositionLongMismatch
     d. 空头数量不一致 → AssetVerify_PositionShortMismatch
   - 数量比较：|OM数量 - 外部数量| > 0 即视为不匹配

【错误码优先级】
当多种错误同时存在时，按以下优先级返回（仅返回一个错误码）：
1. AssetVerify_PositionMissingInOM（数据缺失最严重，可能漏单）
2. AssetVerify_PositionMissingInExternal
3. AssetVerify_PositionLongMismatch / AssetVerify_PositionShortMismatch
4. AssetVerify_FundInsufficient

【日志输出】
校验失败时，会在ERROR日志中记录：
- 资金不足：OM权益、最低要求、差额
- 持仓缺失：缺失的合约代码列表
- 持仓数量不匹配：合约代码、OM多头/空头、外部多头/空头、差异

@param run_id                实例ID（作用域标识）
@param account_id            资金账户ID（作用域标识）
@param account_type          资金账户类型（作用域标识）
@param min_required_equity   最低权益要求（扩大一万倍），OM系统资金必须 >= 此值
@param positions             外部持仓数据数组（ExternalPosition[]），必须与OM系统严格一致
@return 0(OM_Ok) 校验通过（资金充足且持仓完全匹配）；
        资产校验层(-800 ~ -899)：
          -801(AssetVerify_InvalidArg) 参数非法；
          -802(AssetVerify_NotInited) service未初始化；
          -803(AssetVerify_StoreError) Store查询失败；
        资金校验错误(-810 ~ -819)：
          -810(AssetVerify_FundInsufficient) 资金不足：OM权益 < 最低资金要求；
        持仓校验错误(-820 ~ -839)：
          -821(AssetVerify_PositionMissingInOM) 持仓缺失：外部有，OM系统没有；
          -822(AssetVerify_PositionMissingInExternal) 持仓缺失：OM系统有，外部没有；
          -823(AssetVerify_PositionLongMismatch) 多头持仓数量不一致；
          -824(AssetVerify_PositionShortMismatch) 空头持仓数量不一致
"""
function om_verify_assets(run_id::String, account_id::String, account_type::Integer,
                          min_required_equity::Integer, positions::Vector{cExternalPosition})::Cint
    sym = Libc.Libdl.dlsym(lib, :om_verify_assets)
    position_count = length(positions)
    err = ccall(sym, Cint,
                (Ptr{UInt8}, Ptr{UInt8}, Cint, Clonglong, Cptr{cExternalPosition}, Cint),
                run_id, account_id, account_type, min_required_equity, positions, position_count)
    return err
end
export om_verify_assets

end # module
