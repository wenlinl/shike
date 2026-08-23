# 食刻 · 软件端差距分析（vs 产品文档 v3）

> 日期：2026-08-18 ｜ 对照：食刻-H5手机端.html（Aug 16 13:05） vs 产品文档 v3（Aug 17 22:17）
> 说明：本文为 **SW-v0 快照**（2026-08-18）；对照对象 v3 已归档（[产品文档-v3](../archive/产品文档-v3-冰箱零食柜-硬件软件设计方案.md)），SW-v1 全接口接通进行中（见 [产品总览](../01-产品/产品总览.md)）。

## 1. 结论

- **页面覆盖无差距**：5 个 Tab + 12 个弹层子页面全部已实现，与产品文档 §1.1 / §5 完全一致；
- **数据链路有差距**：目前只有「扫描」「库存（items）」接了真实接口，其余页面仍是本地演示数据；产品文档 §5 标注的"接口已就绪"属实，但**页面尚未逐个接通**；
- **发现 1 个接口 Bug**：H5 调用 `/api/shelf-life`，后端没有该接口（HTTP 400），导致扫描后「建议保鲜天数」永远回退默认值。

## 2. 页面 vs 数据源现状

| Tab / 子页面 | 功能 | 当前数据源 | 后端接口 | 状态 |
|---|---|---|---|---|
| 首页 | 冰箱总览 / 保鲜提醒 / 今日推荐 / 清单预览 / 家庭共享 / 快捷入口 | INV（items 同步后部分真实）+ SHOP/MEMBERS/RECIPES mock | /api/items、/api/reminders、/api/recipes | ⚠️ 部分真实（items 真实，其余 mock） |
| 扫描 | 取景 → 识别 → 确认录入 | ✅ 真实 | POST /api/scan | ✅ 真实（但 shelf-life 补充失效，见 §3） |
| 库存 | 搜索 / 筛选 / 排序 / 卡片 / 临期横幅 | syncInventory → /api/items | GET /api/items | ✅ 真实（失败回退演示数据） |
| 菜谱 | 可用食材 / 今日推荐 / 换一批 / 偏好 / 收藏 | 本地 RECIPES / RECIPE_FOR / FAVS / PREF_SEL | GET /api/recipes | ❌ 未接（后端已算好「有 / 缺」） |
| 我的 | 成员 / 清单 / 收藏 / 通知 / 营养 / 履历 | 全部本地 mock | /api/members、/api/shopping-list、/api/notifications、/api/catalog | ❌ 未接 |
| 子页面-购物清单 | 勾选 / 数量 / 一键加购 | 本地 SHOP | GET /api/shopping-list + PATCH [id] | ❌ 未接 |
| 子页面-家庭成员 | 角色 / 录入次数 / 负责品类 | 本地 MEMBERS | GET /api/members | ❌ 未接 |
| 子页面-通知中心 | 未读 / 跳转 | 本地 NOTIFS | GET /api/notifications + PATCH [id] | ❌ 未接 |
| 子页面-营养档案 | 蛋白质 / 维C / 膳食纤维 / 钙 | 本地 NUTR | /api/catalog（nutrition 字段） | ❌ 未接 |
| 子页面-食材履历 / 追溯 | 来源 / 批次 / 冷链 / 检测 | 本地 TRACE / LOG | FoodTrace 表已建，接口未建 | ❌ 后端缺接口 |
| 子页面-快速录入 | 手动录入食材 | 仅改本地 INV | 后端无 manual 写入接口 | ❌ 后端缺接口 |
| 子页面-收藏 / 偏好 | 收藏菜谱 / 口味 chips | 本地 FAVS / PREF_SEL | RecipePreference 表已建，接口未建 | ❌ 后端缺接口 |
| WebDemo 大屏 | 库存 / 提醒 / 菜谱 / 统计 | /api/items + 演示数据 | /api/items、/api/reminders、/api/recipes | ⚠️ 仅 items 真实 |

## 3. 接口 Bug（优先修）

- H5 `fetchShelfLife()` 调 `GET /api/shelf-life?name=xxx` → 后端无此路由（HTTP 400）→ 「建议保鲜 X 天」永远用兜底值；
- 修复方案（二选一，推荐前者）：后端新增 `GET /api/shelf-life?name=`（查 FoodCatalog.keepDays）；或 H5 改调已有的 `GET /api/catalog` 后按 name 匹配。

## 4. 建议的第二步执行顺序

1. **后端补 `/api/shelf-life`**（一行查 catalog，扫描页立即变真实）；
2. **菜谱页接 `/api/recipes`**（有 / 缺已由后端计算，替换本地 RECIPES/RECIPE_FOR）；
3. **我的 → 购物清单 / 成员 / 通知中心** 分别接 `/api/shopping-list`、`/api/members`、`/api/notifications`（后端均已就绪）；
4. **营养档案** 接 `/api/catalog` 聚合 nutrition；
5. **快速录入**：后端新增 manual 录入接口（POST /api/items 或 /api/scan 支持无图手动），H5 quickSave 改调；
6. **食材履历 / 收藏偏好**：后端补 FoodTrace / RecipePreference 读写接口（产品文档 §6 已标注"待接"，可后置）。
