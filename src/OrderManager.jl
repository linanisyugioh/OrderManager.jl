module OrderManager

using CBinding
using Pkg.Artifacts
import FinancialStruct.cFuOrder as cOrder      
import FinancialStruct.cFuTrade as cTrade  
import FinancialStruct:cFuCodeInfo as cCodeInfo

using FinancialStruct.cFeeCodeInfo
using FinancialStruct.cOmTrade
using FinancialStruct.cOmOrder
using FinancialStruct.cContractStat
using FinancialStruct.cAccountContractStat
#using FinancialStruct.cPositionUnit
#using FinancialStruct.cPositionCloseParam
#using FinancialStruct.cPositionUnitHis
using FinancialStruct.cFundtable
using FinancialStruct.cAccountFundtable
#using FinancialStruct.cFundtableHis
#using FinancialStruct.cAccountFundtableHis
#using FinancialStruct.cAccountPositionUnit
#using FinancialStruct.cAccountPositionCloseParam
#using FinancialStruct.cAccountPositionUnitHis
#using FinancialStruct.cCombinationUnit
#using FinancialStruct.cCombinationUnitHis
#using FinancialStruct.cAccountScopePnlDelta

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
@return 0 成功，否则为错误码
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

@param trading_date 交易日（YYYYMMDD），表示进入该交易日
@return 0 成功；否则为错误码
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

@param fee_info 合约基础信息（合约乘数、保证金率等）
@return 0 成功；否则为错误码
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
    om_trading_day_end()::Cint
交易日结束：触发日终结算，逐手计算结算盈亏并转入可用资金。

@return 0 成功；否则为错误码
"""
function om_trading_day_end()::Cint
    sym = Libc.Libdl.dlsym(lib, :om_trading_day_end)
    err = ccall(sym, Cint, ())
    return err
end
export om_trading_day_end

"""
    om_handle_order(order::OmOrder, fee_info::FeeCodeInfo)::Cint
接收最新委托，驱动 Order → Position → Fundtable 业务流程。

@param order 委托最新状态
@param fee_info 与该委托标的对应的手续费率与合约/保证金参数
@return 0 成功，否则为错误码
"""
function om_handle_order(order::cOmOrder, fee_info::cFeeCodeInfo)::Cint
    sym = Libc.Libdl.dlsym(lib, :om_handle_order)
    err = ccall(sym, Int32, (cOmOrder, cFeeCodeInfo), order, fee_info)
    return err
end
export om_handle_order

"""
    om_handle_newprice(code::String, last_price::Integer, pre_settlement_price::Integer, settlement_price::Integer)::Cint
更新合约最新价，刷新该合约下所有未平仓持仓的浮动盈亏。

@param code 合约代码
@param last_price 最新价，扩大一万倍
@param pre_settlement_price 昨结算价，扩大一万倍
@param settlement_price 今结算价，扩大一万倍；0表示盘中行情
@return 0 成功；否则为错误码
"""
function om_handle_newprice(code::String, last_price::Integer, pre_settlement_price::Integer, settlement_price::Integer)::Cint
    sym = Libc.Libdl.dlsym(lib, :om_handle_newprice)
    err = ccall(sym, Int32, (Ptr{UInt8}, Int64, Int64, Int64), code, last_price, pre_settlement_price, settlement_price)
    return err
end
export om_handle_newprice

"""
    om_set_fund_config(fund::Fundtable)::Cint
写入资金配置（建初值）：按 run_id、account_id、account_type、strategy_id 唯一确定一条资金记录。

@param fund 资金配置
@return 0 成功；否则为错误码
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
写入账户级资金配置（建初值）。

@param fund 账户级资金配置
@return 0 成功；否则为错误码
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

@param trade 成交数据
@return 0 成功；否则为错误码
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
设置查询作用域。

@param run_id 实例ID
@param account_id 账户ID
@param account_type 账户类型
@return 0 成功；否则为错误码
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
按主键查询单条委托。

@param order_id 委托ID
@param oper_date 委托日期YYYYMMDD
@param strategy_id 策略ID
@return 查询结果或 nothing
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
查询 strategy_id 下委托的 order_id 列表。

@param strategy_id 策略ID
@param status 0=未终态委托，1=已终态，2=所有状态
@param code 合约代码，空字符串表示所有合约
@param side 0=平，1=开，3=全部
@param bs 0=空，1=多，3=全部
@return 逗号分隔的 order_id 字符串
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
查询 strategy_id 下持仓的 code 列表。

@param strategy_id 策略ID
@param status 0=冻结，1=可用，2=全部
@param period 0=昨仓，1=今仓，2=全部
@param side 0=空，1=多，2=全部
@return 逗号分隔的 code 字符串
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
查询账户级持仓的 code 列表。

@return 逗号分隔的 code 字符串
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
查询合约统计。

@param strategy_id 策略ID
@param code 合约代码
@return 查询结果或 nothing
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
查询账户级合约统计。

@param code 合约代码
@return 查询结果或 nothing
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
查询策略级资金。

@param strategy_id 策略ID
@return 查询结果或 nothing
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
查询账户级资金。

@return 查询结果或 nothing
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
添加合约基础信息至缓存（HFT 适配版本）。

@param hft_code_info HFT 合约信息
@return 0 成功；否则为错误码
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

@param hft_order HFT 委托结构指针
@param hft_code_info 与该委托标的对应的 HFT 合约信息
@return 0 成功；否则为错误码
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

@param hft_trade HFT 成交结构指针
@return 0 成功；否则为错误码
"""
function om_handle_trade_hft(hft_trade::cTrade)::Cint
    hft_trade_r = Ref{cTrade}(hft_trade)
    sym = Libc.Libdl.dlsym(lib, :om_handle_trade_hft)
    err = ccall(sym, Int32, (Cptr{cTrade},), hft_trade_r)
    return err
end
export om_handle_trade_hft

end # module
