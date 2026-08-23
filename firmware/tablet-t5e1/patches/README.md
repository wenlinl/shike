# 外置平板 v1 改造补丁脚本

2026-08-23 将 `app_xzd.c` / `app_xzd_net.c` 从 HW-v0（平板拍照）改造为 HW-v1（门外交互屏）时使用的锚点脚本，按文件名顺序执行（`apply_net_v1.py` → `apply_xzd_v1a.py` → `apply_xzd_v1b.py` → `clean_xzd_v1c.py` → `clean_xzd_v1d.py` → `apply_net_v2.py` → `apply_xzd_v1e.py`）。

脚本含绝对路径（`/Users/lwl/TuyaOpenIDE/projects/copy/source/embedded/`），锚点不匹配会报错退出、不破坏文件；已应用过的补丁再次执行会跳过（幂等）。改造说明见 `../外置平板-参考代码.md`。

`apply_net_v2.py` / `apply_xzd_v1e.py` 为第二批：汇总页接入真实 `GET /api/summary/pending`（进入时拉取）与 `POST /api/summary/confirm`（确认提交），替换演示数据。
