# WS63 OHOS App 子树同步操作文档

## 1. 文档目的

本文档用于指导另一台设备上的 agent，将现有的完整 `ws63_ohos` 工作区接入当前 GitHub 仓库，并只同步以下路径：

`applications/sample/wifi-iot/app`

当前远端仓库：

`https://github.com/samachenlu-oss/fbb_ws63-ecg.git`

## 2. 同步边界

本仓库不是完整 OHOS/WS63 SDK 仓库，只是一个代码同步层。

当前 Git 实际管理的内容只有：

- 仓库根目录的 `.gitignore`
- 仓库根目录的 `README.md`
- `applications/sample/wifi-iot/app/**`

以下内容都不在版本管理范围内：

- `device/**`
- `prebuilts/**`
- `out/**`
- `build/**`
- 其他 OHOS 基座目录

结论：

- 另一台设备必须先自行准备完整可构建的 `ws63_ohos` 工作区
- 不能指望单独 clone 本仓库后直接完成编译
- 本仓库只负责同步 `app` 目录下的代码

## 3. 适用场景

本文件覆盖 3 类场景：

1. 另一台设备已有完整 `ws63_ohos` 工作区，但还没有接入当前 Git 仓库
2. 另一台设备已经接入当前 Git 仓库，只需要日常拉取更新
3. 远端 `main` 被重写过历史，本地需要强制对齐

## 4. 操作总原则

- 不要在另一个设备上把整个工作区删除后再 clone
- 不要把 `device/`、`prebuilts/`、`out/` 等目录重新加入 Git
- 所有 Git 操作都在 `ws63_ohos` 根目录执行
- 如果本地 `applications/sample/wifi-iot/app` 有未确认价值的改动，先备份，再执行覆盖性操作
- 如果需要强制对齐远端，优先只清理 `app` 子树，不要误删整个工作区

## 5. 目录约定

以下命令假定目标工作区路径为：

```bash
/path/to/ws63_ohos
```

执行前先进入根目录：

```bash
cd /path/to/ws63_ohos
```

## 6. 场景 A：另一台设备第一次接入当前仓库

### 6.1 前置检查

确认完整工作区已经存在：

```bash
cd /path/to/ws63_ohos
test -d applications/sample/wifi-iot/app
```

如果返回非 0，说明工作区本身就不完整，不能继续。

### 6.2 备份当前 app 目录

如果另一台设备已有本地修改，先备份：

```bash
cd /path/to/ws63_ohos
cp -a applications/sample/wifi-iot/app \
  applications/sample/wifi-iot/app.backup_$(date +%Y%m%d_%H%M%S)
```

### 6.3 检查是否已有旧 Git 仓库

```bash
cd /path/to/ws63_ohos
test -d .git && git remote -v
```

处理原则：

- 如果没有 `.git`，直接执行 6.4
- 如果有 `.git`，且它已经指向 `https://github.com/samachenlu-oss/fbb_ws63-ecg.git`，跳到“场景 B”
- 如果有 `.git`，但它指向别的仓库，不要直接混用。先备份原 `.git`，再初始化新仓库

备份旧 `.git` 示例：

```bash
cd /path/to/ws63_ohos
mv .git .git.backup_$(date +%Y%m%d_%H%M%S)
```

### 6.4 初始化并接入远端

```bash
cd /path/to/ws63_ohos
git init -b main
git remote add origin https://github.com/samachenlu-oss/fbb_ws63-ecg.git
git fetch origin
git checkout -f -B main origin/main
git branch --set-upstream-to=origin/main main
```

说明：

- `checkout -f` 是覆盖式接管 `app` 子树
- 由于当前仓库只管理 `app` 路径，其他目录不会被 Git 接管

### 6.5 接入完成后的验证

```bash
cd /path/to/ws63_ohos
git status --short --branch
git ls-files
```

预期：

- `git status` 应显示 `main...origin/main`
- `git ls-files` 只应包含根目录元文件和 `applications/sample/wifi-iot/app/**`

## 7. 场景 B：另一台设备已经接入当前仓库，日常拉取更新

这是以后最常用的流程。

### 7.1 拉取前检查

```bash
cd /path/to/ws63_ohos
git status --short --branch
```

如果输出中出现你本地尚未提交的 `app` 改动，先决定：

- 需要保留：先提交或备份
- 不需要保留：可直接继续对齐远端

### 7.2 标准拉取

```bash
cd /path/to/ws63_ohos
git fetch origin
git pull --ff-only origin main
```

说明：

- `--ff-only` 可以防止 agent 在不明确的情况下制造无意义 merge commit

### 7.3 拉取后验证

```bash
cd /path/to/ws63_ohos
git status --short --branch
```

预期输出应接近：

```bash
## main...origin/main
```

## 8. 场景 C：远端 main 被重写，本地需要强制对齐

当前仓库已经发生过一次这种情况。未来如果再发生，另一台设备不能盲目 `git pull`。

### 8.1 识别条件

以下现象说明本地很可能不能直接快进：

- `git pull --ff-only` 失败
- 本地历史明显和远端不一致
- 已知远端执行过 `push --force-with-lease`

### 8.2 先备份本地 app

```bash
cd /path/to/ws63_ohos
cp -a applications/sample/wifi-iot/app \
  applications/sample/wifi-iot/app.backup_before_reset_$(date +%Y%m%d_%H%M%S)
```

### 8.3 强制对齐远端

```bash
cd /path/to/ws63_ohos
git fetch origin
git reset --hard origin/main
git clean -fd applications/sample/wifi-iot/app
```

说明：

- `reset --hard` 只会重置 Git 已跟踪内容
- `git clean -fd applications/sample/wifi-iot/app` 只清理 `app` 子树中的未跟踪文件
- 不要把 `git clean -fd` 直接用于整个仓库根目录

### 8.4 验证

```bash
cd /path/to/ws63_ohos
git status --short --branch
```

## 9. 从另一台设备提交代码回远端

如果另一台设备上也要改代码并推送，标准流程如下。

### 9.1 拉最新

```bash
cd /path/to/ws63_ohos
git fetch origin
git pull --ff-only origin main
```

### 9.2 检查改动

```bash
cd /path/to/ws63_ohos
git status --short
```

### 9.3 只提交 app 目录改动

```bash
cd /path/to/ws63_ohos
git add applications/sample/wifi-iot/app
git commit -m "your message"
git push origin main
```

### 9.4 禁止事项

- 不要执行 `git add .`
- 不要把 `device/` 重新纳入提交
- 不要提交 `out/`、编译产物、SDK 输出文件

## 10. 失败场景处理

### 10.1 `git pull --ff-only` 失败

原因通常只有两类：

- 本地有未提交改动
- 远端历史被重写

处理：

1. 先看 `git status --short`
2. 若本地改动有价值，先备份或提交
3. 若远端历史被重写，转到“场景 C”

### 10.2 `untracked working tree files would be overwritten`

说明本地已有同路径未跟踪文件阻挡 checkout。

处理：

1. 先备份 `applications/sample/wifi-iot/app`
2. 再使用：

```bash
git checkout -f -B main origin/main
```

### 10.3 `git push` 被拒绝

先执行：

```bash
git fetch origin
git pull --ff-only origin main
```

然后再推。

如果还是失败，再判断是否是：

- 认证问题
- 远端已有新提交
- 远端分支保护策略

## 11. 给 agent 的推荐执行模板

如果另一台设备是第一次接入，推荐直接按下面顺序执行：

```bash
cd /path/to/ws63_ohos
cp -a applications/sample/wifi-iot/app \
  applications/sample/wifi-iot/app.backup_$(date +%Y%m%d_%H%M%S)

test -d .git && mv .git .git.backup_$(date +%Y%m%d_%H%M%S)

git init -b main
git remote add origin https://github.com/samachenlu-oss/fbb_ws63-ecg.git
git fetch origin
git checkout -f -B main origin/main
git branch --set-upstream-to=origin/main main
git status --short --branch
```

如果另一台设备已经接入过，推荐直接按下面顺序执行：

```bash
cd /path/to/ws63_ohos
git fetch origin
git pull --ff-only origin main
git status --short --branch
```

如果已知远端历史被重写，推荐直接按下面顺序执行：

```bash
cd /path/to/ws63_ohos
cp -a applications/sample/wifi-iot/app \
  applications/sample/wifi-iot/app.backup_before_reset_$(date +%Y%m%d_%H%M%S)

git fetch origin
git reset --hard origin/main
git clean -fd applications/sample/wifi-iot/app
git status --short --branch
```

## 12. 最终判断标准

另一台设备同步完成后，满足以下条件才算成功：

1. `git status --short --branch` 显示本地 `main` 正常跟踪 `origin/main`
2. `git ls-files` 只覆盖根目录元文件和 `applications/sample/wifi-iot/app/**`
3. 编译仍依赖该设备本地完整的 OHOS/WS63 环境，而不是依赖 Git 仓库补全平台层
