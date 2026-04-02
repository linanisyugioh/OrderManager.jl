using Pkg
using Pkg.Artifacts
using Pkg.BinaryPlatforms
using Tar
using CodecZlib
using TranscodingStreams
using SHA
using Downloads
using JSON

# GitHub 配置
const GITHUB_REPO = "linanisyugioh/OrderManager.jl"

# ========== 卸载已安装的 OrderManager 包（避免 artifact 占用）==========
println("🔧 检查并卸载已安装的 OrderManager 包...")
try
    Pkg.rm("OrderManager")
    println("✅ OrderManager 已卸载")
catch e
    println("ℹ️  OrderManager 未安装，跳过卸载")
end
println()

# 检查是否安装了 GitHub CLI
function check_gh_cli()
    try
        run(`gh --version`)
        return true
    catch
        return false
    end
end

# 检查 Release 是否已存在
function release_exists(version::String)
    tag = "V$(version)"
    try
        run(`gh release view $(tag) --repo $(GITHUB_REPO)`)
        return true
    catch
        return false
    end
end

# 删除已存在的 Release
function delete_release(version::String)
    tag = "V$(version)"
    println("🗑️  删除已存在的 Release $(tag)...")
    try
        run(`gh release delete $(tag) --repo $(GITHUB_REPO) --yes`)
        println("✅ Release $(tag) 已删除")
    catch e
        println("⚠️  删除 Release 失败: $e")
    end
end

# 使用 gh CLI 创建 Release（推荐方式）
function create_release_with_gh(version::String)
    tag = "V$(version)"
    # 检查是否已存在
    if release_exists(version)
        println("⚠️  Release $(tag) 已存在")
        print("是否删除并重新创建? (y/n): ")
        response = readline()
        if lowercase(response) == "y"
            delete_release(version)
        else
            println("⏭️  跳过创建 Release，直接上传文件")
            return tag
        end
    end
    # 创建 release
    cmd = `gh release create $(tag) --repo $(GITHUB_REPO) --title "Release $(tag)" --notes "OrderManager.jl binary release $(tag)"`
    run(cmd)
    return tag
end

# 使用 gh CLI 上传文件
function upload_with_gh(version::String, files::Vector{String})
    tag = "V$(version)"
    for file in files
        # 检查文件是否已存在
        filename = basename(file)
        try
            # 先尝试删除已存在的文件
            run(`gh release delete-asset $(tag) $(filename) --repo $(GITHUB_REPO) --yes`)
            println("🗑️  删除已存在的文件: $(filename)")
        catch
            # 文件不存在，忽略错误
        end
        # 上传文件
        cmd = `gh release upload $(tag) $(file) --repo $(GITHUB_REPO)`
        run(cmd)
        println("✅ 上传成功: $(filename)")
    end
end

# TODO: 修改为你的 DLL/SO 文件路径
# Windows DLL 文件列表
windows_dll_files = [
   "./release/win64/ordermanager.dll",
   # 添加其他需要的 DLL 文件
]

# Linux SO 文件列表
linux_so_files = [
   "./release/linux/libordermanager.so",
   # 添加其他需要的 SO 文件
]

# ========== 打包 Windows 版本 ==========
println("📦 打包 Windows 版本...")

windows_artifact_dir = create_artifact() do dir
    for src in windows_dll_files
        if isfile(src)
            cp(src, joinpath(dir, basename(src)); force=true)
        else
            @warn "文件不存在: $src"
        end
    end
end

tmp_dir = mktempdir()
for src in windows_dll_files
    if isfile(src)
        dst = joinpath(tmp_dir, basename(src))
        cp(src, dst; force=true)
        println("Copied: $dst")
    end
end

output_file = "ordermanager_windows_x64.tar"
Tar.create(tmp_dir, output_file)
println("\n✅ 成功生成: $(abspath(output_file))")

windows_tar_gz = "ordermanager_windows_x64.tar.gz"
open(windows_tar_gz, "w") do out
    stream = GzipCompressorStream(out)
    open(output_file, "r") do input
        write(stream, read(input))
    end
    close(stream)
end

println("✅ 成功生成: ", abspath(windows_tar_gz))

# 计算 SHA256
windows_sha256 = bytes2hex(open(windows_tar_gz, "r") do f
    sha256(f)
end)
println("\n📦 Windows 包信息:")
println("   文件: $windows_tar_gz")
println("   SHA256: $windows_sha256")

# ========== 打包 Linux 版本 ==========
println("\n📦 打包 Linux 版本...")

linux_artifact_dir = create_artifact() do dir
    for src in linux_so_files
        if isfile(src)
            cp(src, joinpath(dir, basename(src)); force=true)
        else
            @warn "文件不存在: $src"
        end
    end
end

tmp_dir = mktempdir()
for src in linux_so_files
    if isfile(src)
        dst = joinpath(tmp_dir, basename(src))
        cp(src, dst; force=true)
        println("Copied: $dst")
    end
end

output_file = "ordermanager_linux_x64.tar"
Tar.create(tmp_dir, output_file)
println("\n✅ 成功生成: $(abspath(output_file))")

linux_tar_gz = "ordermanager_linux_x64.tar.gz"
open(linux_tar_gz, "w") do out
    stream = GzipCompressorStream(out)
    open(output_file, "r") do input
        write(stream, read(input))
    end
    close(stream)
end

println("✅ 成功生成: ", abspath(linux_tar_gz))

# 计算 SHA256
linux_sha256 = bytes2hex(open(linux_tar_gz, "r") do f
    sha256(f)
end)
println("\n📦 Linux 包信息:")
println("   文件: $linux_tar_gz")
println("   SHA256: $linux_sha256")

# ========== 自动创建 GitHub Release 并上传 ==========
println("\n" * "="^60)
println("🚀 准备创建 GitHub Release")
println("="^60)

print("\n请输入新版本号（如 1.0.0）: ")
version = readline()

if isempty(version)
    println("❌ 版本号不能为空，退出")
    exit(1)
end

# 检查 gh CLI
if check_gh_cli()
    println("✅ 检测到 GitHub CLI (gh)")
    println("🔄 创建 Release V$(version)...")
    
    try
        create_release_with_gh(version)
        println("✅ Release 创建成功")
        
        # 上传文件
        files_to_upload = [windows_tar_gz, linux_tar_gz]
        println("\n📤 上传文件到 Release...")
        upload_with_gh(version, files_to_upload)
        
        println("\n✅ 所有文件上传完成！")
    catch e
        println("❌ 操作失败: $e")
        println("请手动创建 Release 并上传文件")
        exit(1)
    end
else
    println("⚠️ 未检测到 GitHub CLI (gh)")
    println("请安装 gh CLI: https://cli.github.com/")
    exit(1)
end

# ========== 更新 Artifacts.toml ==========
println("\n" * "="^60)
println("📝 更新 Artifacts.toml")
println("="^60)

# Windows
windows_platform = Platform("x86_64", "windows")
bind_artifact!(
    "Artifacts.toml",
    "ordermanager_lib",
    windows_artifact_dir;
    platform = windows_platform,
    lazy = false,
    force = true,
    download_info = [
        ("https://github.com/$(GITHUB_REPO)/releases/download/V$(version)/ordermanager_windows_x64.tar.gz",
         windows_sha256)
    ]
)
println("✅ Windows Artifacts.toml 已更新")

# Linux
linux_platform = Platform("x86_64", "linux")
bind_artifact!(
    "Artifacts.toml",
    "ordermanager_lib",
    linux_artifact_dir;
    force = true,
    platform = linux_platform,
    lazy = false,
    download_info = [
        ("https://github.com/$(GITHUB_REPO)/releases/download/V$(version)/ordermanager_linux_x64.tar.gz",
         linux_sha256)
    ]
)
println("✅ Linux Artifacts.toml 已更新")

println("\n" * "="^60)
println("🎉 打包流程全部完成！")
println("="^60)
println("\n下一步：")
println("运行: julia _build_pkg.jl 安装更新后的包")
println("="^60)
