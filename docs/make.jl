using OrderManager
using Documenter

DocMeta.setdocmeta!(OrderManager, :DocTestSetup, :(using OrderManager); recursive=true)

makedocs(;
    modules=[OrderManager],
    authors="linan <linanisyugioh@163.com>",
    sitename="OrderManager.jl",
    format=Documenter.HTML(;
        canonical="https://linanisyugioh.github.io/OrderManager.jl",
        edit_link="master",
        assets=String[],
    ),
    pages=[
        "Home" => "index.md",
    ],
)

deploydocs(;
    repo="github.com/linanisyugioh/OrderManager.jl",
    devbranch="master",
)
