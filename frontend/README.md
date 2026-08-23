# 食刻 · 前端 / 界面资源索引

> 定位：前端演示页与设备界面参考的**单一事实源**；截图由界面 HTML 生成，统一存放于 `tablet-ui/previews/`，避免多副本漂移。

```text
frontend/
├── demos/       # 对外演示页（H5 手机端 / 硬件交互 Demo）
└── tablet-ui/   # 外置平板（T5-E1）v1 界面参考（给烧录 AI）+ previews 截图
```

## demos/（对外演示）

| 文件 | 说明 |
|---|---|
| [食刻-H5手机端.html](demos/食刻-H5手机端.html) | 手机端 H5：首页 / 扫描 / 库存 / 菜谱 / 我的 |
| [食刻-硬件交互Demo.html](demos/食刻-硬件交互Demo.html) | 硬件交互模拟（HW-v1 门外交互屏流程） |

> 网页端仅 H5 一个界面（无独立大屏版）；旧「食刻-WebDemo.html」已归档至 [archive/](../archive/食刻-WebDemo.html)（仅本地）。

## tablet-ui/（设备界面参考）

- 8 个界面 HTML（240×320，与硬件屏幕等大），每个文件下方附实现说明；
- `index-总览.html` 一览页；`previews/` 为各界面 PNG 截图；
- 用途与口径见 [tablet-ui/README.md](tablet-ui/README.md)。
