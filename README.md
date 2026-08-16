# 食刻 ShiKe · 冰箱食品保质期管理

> Slogan：**食刻在场，不浪费每一份食材**
> 项目：HsHH 2026 女性 AI 硬件黑客松

食刻是一台放在冰箱 / 零食柜旁的智能扫描台：把食品拿到镜头前扫一下，云端 AI 认出它是什么、能放多久，库存自动记账；屏幕和手机端随时告诉你谁快过期、能做什么菜。

## 在线演示

| 入口 | 链接 | 说明 |
|---|---|---|
| H5 手机端 | https://tuvmkt.com/shike-h5.html | 首页 / 扫描 / 库存 / 菜谱 / 我的 |
| 大屏版 | https://tuvmkt.com/shike-web.html | 库存 / 提醒 / 菜谱 / 统计 |

> 演示依赖：浏览器 + 摄像头权限 + 联网（识别走云端 https://tuvmkt.com/api/scan）；首页 / 库存 / 菜谱 / 我的为演示数据（接口已就绪），扫描页为真实 AI 识别。

## 核心链路

**扫（按按钮拍照）→ 认（云端视觉 AI）→ 记（库存 ±1）→ 显（T5 屏幕 + H5 / 大屏）**

## 仓库结构

```text
.
├── shike-h5.html          # 移动端 H5（iPhone 16 适配，底部 5 Tab）
├── shike-web.html         # 大屏版 Web Demo
├── Demo操作说明.md         # 现场演示步骤与依赖说明
├── docs/                  # 产品文档 / 后端对接说明 / 硬件描述 / 固件需求
└── backend/               # 后端接口参考代码（Prisma + Next.js API，已部署 tuvmkt.com）
```

## 硬件

- 主控：T5-E1（SPARKLEIOT T5AI DEV）单板（ST7789 触控屏 + GC2145 摄像头 + 板载音频）
- 固件：TuyaOpen 工程 `copy`（app_xzd.c：放入 / 取出 → 3 秒倒计时 → 拍照 → 上传 /api/scan）
- 后端接口与数据库参考代码见 `backend/`；已部署 https://tuvmkt.com（完整可部署工程保留在本机与服务器）

## 说明

- 视觉识别：火山方舟 doubao-seed-2-1-turbo-260628（关闭思考，3–5 秒），低置信不入库；
- 数据库：SQLite 15 张表（FoodItem / ScanLog / Member / Recipe / ShoppingListItem 等）；
- 未实现项（温度、湿度、称重、语音、场景推送、原生 App 等）见 docs/产品文档.md §6。
