# 全局约定

> 命名规范、错误处理、数值精度、线程安全等全局约定

---

## 1. 命名规范

| 对象 | 风格 | 示例 |
|------|------|------|
| 类名 | PascalCase | `OrderProcessor`, `FundtableStore` |
| 函数名（C++） | camelCase | `handleOrder()`, `calcMargin()` |
| 函数名（C API） | snake_case + `om_` 前缀 | `om_init()`, `om_handle_order()` |
| 变量名 / 结构体成员 | snake_case | `filled_volume`, `trading_date` |
| 类成员变量 | snake_case + 下划线结尾 | `order_store_`, `trading_date_` |
| 枚举类型名 | PascalCase | `OrderStatus`, `OrderSide` |
| 枚举值 | PascalCase + 下划线分隔 | `OrderStatus_Filled`, `OrderSide_Long_Open` |
| 文件名 | snake_case | `order_processor.h`, `fundtable_store.cc` |
| 宏名 | 全大写 + 下划线 | `OM_SDK_EXPORTS`, `LEN_ID` |
| 头文件 guard | 全大写 + `_H` 后缀 | `OM_DATA_TYPES_H` |

---

## 2. 错误处理策略

### 2.1 统一使用错误码返回

**全局统一使用错误码返回，不使用异常。**

- 所有函数返回 `int`，0 表示成功，负数表示错误
- 错误码按模块分段分配，避免冲突：

| 错误码范围 | 所属模块 |
|-----------|---------|
| 0 | 成功（`OM_Ok = 0`） |
| -1 ~ -99 | 通用/系统级 |
| -100 ~ -199 | 委托处理（OrderProc） |
| -200 ~ -299 | 持仓处理（PositionProc） |
| -300 ~ -399 | 资金处理（FundtableProc） |
| -400 ~ -499 | 数据层 Store（含账户级资金 Store -470~-489、AccountFundtableProcessor -490~-499） |
| -500 ~ -579 | 账户级持仓相关（AccountPositionUnitStore、AccountContractStatStore、AccountPositionProcessor、CombinationUnitStore 等历史表 Store） |
| -600 ~ -619 | 成交处理（TradeProc）及组合委托处理 |

### 2.2 错误码定义位置

所有错误码统一定义在 `include/om_error.h`

### 2.3 错误日志规范

所有 Store 在 SQL 执行失败时，调用 `LOG_ERROR` 记录：
- 失败的 SQL 操作名（如 "OrderStore::upsert"）
- `sqlite3_errmsg` 错误信息
- 相关参数（主键字段值）

---

## 3. 内部数据结构风格

| 使用场景 | 风格 | 说明 |
|---------|------|------|
| 对外暴露（include/） | C 风格 typedef struct | 保证 ABI 兼容 |
| 内部模块间传递（common/） | C++ class/struct | 可使用 std::string、std::vector |

---

## 4. 数值精度约定

**价格、金额字段统一扩大一万倍（×10000）存储为整数，避免浮点精度问题。**

| 字段类型 | 存储类型 | 精度 | 示例 |
|---------|---------|------|------|
| 价格（price, hold_cost, close_price） | int64_t | 扩大 10000 倍 | 3500.25 → 35002500 |
| 金额（frozen, margin, fee, avail_cash） | int64_t | 扩大 10000 倍 | 100000.00 → 1000000000 |
| 保证金率（margin_long1 等） | int32_t | 扩大 10000 倍 | 0.12 → 1200 |
| 手续费率（open_today 等） | int32_t | **扩大 100000 倍** | 万1.5(0.00015) → 15 |
| 数量（volume, filled_volume） | int32_t | 原值 | 10 → 10 |
| 日期（oper_date, open_date） | int32_t | YYYYMMDD | 20260310 |
| 时间（update_time） | int64_t | HHMMSSmmm | 143025500 |

> 注：手续费率扩大十万倍（×100000）存储，计算后除回十万倍，结果仍为×10000格式。

---

## 5. 线程安全策略

**当前整个系统按单线程设计开发。** 调用方保证不并发调用 API 接口，内部不加锁。

> 注：LogManager 内部使用 std::mutex 保护，属于防御性编程，不影响整体单线程设计。

---

## 6. 日志规范

使用自研 LogManager（`utils/log_manager.h`），单例模式，跨平台。

| 项目 | 规范 |
|------|------|
| 日志级别 | `LogLevel_Debug(0)`, `LogLevel_Info(1)`, `LogLevel_Warn(2)`, `LogLevel_Error(3)` |
| 输出格式 | `[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] message` |
| 输出目标 | 控制台 + 文件双输出，可独立开关 |
| 文件切分 | 按日切分，文件名 `trade_mgr_YYYYMMDD.log`（存放于 `work_dir/logs/`） |
| 便捷宏 | `LOG_DEBUG(fmt, ...)`, `LOG_INFO(fmt, ...)`, `LOG_WARN(fmt, ...)`, `LOG_ERROR(fmt, ...)` |
| 错误输出 | ERROR 级别输出到 stderr，其余输出到 stdout |

---

## 7. 内存管理与智能指针约定

**原则：内部 C++ 类使用智能指针管理所有权，C 风格对外接口保持裸指针。**

| 场景 | 推荐用法 | 说明 |
|------|---------|------|
| 组件拥有权（一对一） | `std::unique_ptr<T>` | 如 `OmService` 拥有的 Store / Processor 成员 |
| 非拥有借用引用 | 裸指针 `T*` | 如 Processor 构造参数中引用 Store |
| 共享所有权 | `std::shared_ptr<T>` | 当前系统暂无此场景 |
| 批量查询结果 | `std::vector<T>` | Store query 方法返回 |
| 对外 C 接口参数 | 裸指针 / 值传递 | 保持 C ABI 兼容 |

**析构顺序规则**：`release()` 中先 `reset` Processor（逆依赖顺序），再 `reset` Store。

---

## 8. 导出宏定义（跨平台）

```cpp
#if defined(OM_USE_STATIC_LIBRARIE)
    #define OM_API                              // 静态库：无导出
#elif defined(_WIN32)
    #ifdef OM_SDK_EXPORTS
        #define OM_API __declspec(dllexport)     // Windows 动态库：导出
    #else
        #define OM_API __declspec(dllimport)     // Windows 调用方：导入
    #endif
#else
    #define OM_API __attribute__((visibility("default")))  // Linux
#endif
```

---

## 9. 事务分层约定

**事务控制权集中在 service 层**，core 层和 data 层不得自行调用事务方法。

| 层级 | 是否可以调用事务 | 说明 |
|------|----------------|------|
| service | ✓ | 事务控制中心 |
| core | ✗ | 不得调用 BEGIN/COMMIT/ROLLBACK |
| data | ✗ | 不得调用 BEGIN/COMMIT/ROLLBACK |

**SQLite 嵌套事务说明**：SQLite 不支持原生嵌套 `BEGIN`，若 core/data 层擅自发出 `BEGIN`，第二条 `BEGIN` 静默失败会破坏事务边界。

---

## 10. 文档规范

- 设计文档随代码一起纳入 Git 版本管理
- 修改设计文档时，在文档头部的修订记录中追加条目
- 设计文档的修改必须先于代码修改（先改设计，再改代码）
- 编码阶段发现设计问题时，在文档底部「编码反馈」章节标注

---

## 11. 相关文档

| 主题 | 位置 |
|------|------|
| 详细设计规范 | `04-reference/DOCUMENT_STANDARD.md` |
| 错误码完整列表 | `00-overview/quick-reference.md` §错误码速查 |
| 模块职责 | `01-architecture/module-*.md` |
