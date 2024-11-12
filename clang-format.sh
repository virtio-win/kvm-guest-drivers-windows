#!/bin/bash

declare -A styles=(
    ["Balloon"]="Microsoft"
    ["fwcfg"]="WebKit"
    ["fwcfg64"]="WebKit"
    ["ivshmem"]="LLVM"
    ["NetKVM"]="Microsoft"
    ["pciserial"]="Microsoft"
    ["pvpanic"]="GNU"
    ["viocrypt"]="LLVM"
    ["viofs"]="GNU"
    ["viogpu"]="LLVM"
    ["vioinput"]="Microsoft"
    ["viomem"]="Microsoft"
    ["viorng"]="Google"
    ["vioscsi"]="GNU"
    ["vioserial"]="GNU"
    ["viosock"]="LLVM"
    ["viostor"]="GNU"
    ["VirtIO"]="Google"
)

for dir in "${!styles[@]}"; do
    style="${styles[$dir]}"

    if [ -d "$dir" ]; then        
        find "$dir" -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -exec \
        clang-format -i --style="$style" {} \+
    else
        echo "Directory '$dir' not found!"
    fi
done