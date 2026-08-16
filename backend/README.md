# 食刻 后端（参考代码）

已部署：https://tuvmkt.com（Next.js + Prisma + SQLite，Docker）

- API：/api/scan（硬件上传识别+库存）、/api/items、/api/reminders、/api/members、/api/shopping-list、/api/notifications、/api/recipes、/api/consume、/api/catalog、/api/device/heartbeat、/api/device/state
- 数据库：SQLite 15 张表（schema 见 prisma/schema.prisma）
- 本目录为后端参考代码（schema / 种子 / API 路由）；完整可部署工程保留在本机与服务器，线上地址 https://tuvmkt.com
