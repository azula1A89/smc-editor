# SoulsNpc：SMC C++ 交互示例

本示例展示用 `smc_cpp` 从 [SoulsNpc.sm](SoulsNpc.sm) 生成 C++ 状态机，再由
`SoulsNpc` owner 提供动作、guard 条件和交互事件。它是确定性的命令行模拟，
不会实现实时战斗、计时器或游戏画面。动作只打印效果，不会自动发送事件或
修改 guard 条件。

## 构建、生成与测试

需要 CMake 3.20 和 C++20 编译器。在本目录运行：

```sh
# 独立构建生成器，不启用生成器自己的测试。
cmake -S ../.. -B build/smc -DBUILD_TESTING=OFF
cmake --build build/smc --target smc_cpp

# 仅校验状态机；不会写文件。
./build/smc/smc_cpp -c++ --check SoulsNpc.sm

# 构建示例：配置阶段传入生成器的绝对路径。
cmake -S . -B build/demo -DSMC_EXECUTABLE="$(pwd)/build/smc/smc_cpp"
cmake --build build/demo
ctest --test-dir build/demo --output-on-failure
./build/demo/souls_npc
```

示例构建调用 `smc_cpp -c++ -d build/demo SoulsNpc.sm`，生成
`build/demo/SoulsNpc.hpp` 和 `build/demo/SoulsNpc.cpp`；构建还需要
`../../include/headers/c++` 中的 runtime。`.sm` 或生成器可执行文件变化时，
构建会重新生成。生成文件不是手写源文件，也不应提交。

`SoulsNpc.h` 包含生成的头并持有 `SoulsNpcContext`；`npc.cpp` 实现 `.sm`
中使用的 owner 动作及 guard 查询。`main.cpp` 调用 `enterStartState()` 执行
初始 Entry，然后向生成的 context 发送事件。状态变化及动作均显示在终端。

## 操作

- `help` 列出命令和所有事件；`status` 显示当前状态、状态栈深度和 guard。
- `event <事件名>` 发送事件，例如 `event ThinkTimer`；`Sensed` 需带参数，
  如 `event Sensed true`。当前 `.sm` 保留该参数作为签名示例，暂不使用其值。
- `set <boss|token|lowhp|far> <true|false>` 设置 guard。默认均为 `false`；
  `lowhp` 和 `far` 只是条件开关，不是生命值或距离模拟。即使动作输出
  “release token”或“heal”，开关也不会被自动修改。
- `quit` 或输入结束（EOF）退出。命令和事件区分大小写。无效输入或当前状态
  未定义的事件会报告错误，进程继续运行；`nil` 转换及 Dead 的 Default
  是状态机有意忽略事件，不属于错误。

例如，依次输入：

```text
event Sensed true
event TargetSpotted
event InAttackRange
set token true
event ThinkTimer
event MovePicked
event WindupDone
event StrikeDone
event RecoveryDone
event BeyondLeash
event AtSpawn
```

这条路径从 Idle 进入 Chase，push 到 `CombatMap::Strafe`（栈深度 1），经
Windup、Strike、Recovery 回到 Strafe，再 pop 到 Returning（栈深度 0）并
回到 Idle。可在 `ThinkTimer` 之前再执行 `set lowhp true`，使 `MovePicked`
走 Retreat；也可保持 `token false` 并设置 `far true` 来进入 GapCloser。
在 Strafe 中设置 `boss true` 后发送 `HpBelowPhaseThreshold` 可演示 pop
后进入阶段切换；`HpZero` 和 `Stagger` 也能演示 combat 到主状态机的 pop。
