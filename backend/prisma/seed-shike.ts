import { PrismaClient } from "@prisma/client";

const prisma = new PrismaClient();

async function main() {
  const members = [
    { name: "妈妈", role: "admin", avatar: "👩", responsibleCategory: "蔬菜", preferences: JSON.stringify(["清淡", "家常"]) },
    { name: "爸爸", role: "member", avatar: "👨", responsibleCategory: "肉类", preferences: JSON.stringify(["家常", "微辣"]) },
    { name: "爷爷", role: "member", avatar: "👴", responsibleCategory: "乳品", preferences: JSON.stringify(["清淡"]) },
    { name: "姐姐", role: "member", avatar: "👧", responsibleCategory: "零食", preferences: JSON.stringify(["清淡", "甜口"]) },
  ];
  for (const m of members) {
    await prisma.member.upsert({ where: { name: m.name }, update: m, create: m });
  }

  const containers = [
    { name: "冰箱", icon: "🧊", sort: 1, builtIn: true },
    { name: "零食柜", icon: "🍿", sort: 2, builtIn: true },
    { name: "药盒", icon: "💊", sort: 3, builtIn: true },
    { name: "调料柜", icon: "🧂", sort: 4, builtIn: true },
    { name: "主食柜", icon: "🍚", sort: 5, builtIn: true },
  ];
  for (const c of containers) {
    await prisma.container.upsert({ where: { name: c.name }, update: c, create: c });
  }

  const catalog = [
    { name: "西兰花", category: "蔬菜", emoji: "🥦", keepDays: 3, defaultExpiryDays: 3, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 78, vitaminC: 62, fiber: 45, calcium: 58 }) },
    { name: "牛奶", category: "乳品", emoji: "🥛", keepDays: 5, defaultExpiryDays: 6, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 88, vitaminC: 10, fiber: 0, calcium: 95 }) },
    { name: "鸡蛋", category: "蛋类", emoji: "🥚", keepDays: 10, defaultExpiryDays: 14, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 92, vitaminC: 0, fiber: 0, calcium: 40 }) },
    { name: "牛肉", category: "肉类", emoji: "🥩", keepDays: 3, defaultExpiryDays: 3, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 96, vitaminC: 0, fiber: 0, calcium: 12 }) },
    { name: "番茄", category: "蔬菜", emoji: "🍅", keepDays: 3, defaultExpiryDays: 4, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 20, vitaminC: 75, fiber: 30, calcium: 15 }) },
    { name: "生菜", category: "蔬菜", emoji: "🥬", keepDays: 2, defaultExpiryDays: 3, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 25, vitaminC: 45, fiber: 40, calcium: 30 }) },
    { name: "鸡胸肉", category: "肉类", emoji: "🍗", keepDays: 3, defaultExpiryDays: 3, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 95, vitaminC: 0, fiber: 0, calcium: 10 }) },
    { name: "酸奶", category: "乳品", emoji: "🥣", keepDays: 5, defaultExpiryDays: 7, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 65, vitaminC: 5, fiber: 0, calcium: 80 }) },
    { name: "苹果", category: "水果", emoji: "🍎", keepDays: 7, defaultExpiryDays: 10, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 10, vitaminC: 55, fiber: 50, calcium: 12 }) },
    { name: "面包", category: "零食", emoji: "🍞", keepDays: 4, defaultExpiryDays: 5, suggestedContainer: "零食柜", nutrition: JSON.stringify({ protein: 30, vitaminC: 0, fiber: 15, calcium: 20 }) },
    { name: "大米", category: "主食", emoji: "🍚", keepDays: 180, defaultExpiryDays: 180, suggestedContainer: "主食柜", nutrition: JSON.stringify({ protein: 35, vitaminC: 0, fiber: 10, calcium: 8 }) },
    { name: "黄瓜", category: "蔬菜", emoji: "🥒", keepDays: 5, defaultExpiryDays: 5, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 12, vitaminC: 30, fiber: 35, calcium: 22 }) },
    { name: "豆腐", category: "豆制品", emoji: "🥡", keepDays: 2, defaultExpiryDays: 3, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 70, vitaminC: 0, fiber: 20, calcium: 60 }) },
    { name: "燕麦片", category: "主食", emoji: "🌾", keepDays: 90, defaultExpiryDays: 90, suggestedContainer: "主食柜", nutrition: JSON.stringify({ protein: 55, vitaminC: 0, fiber: 85, calcium: 35 }) },
    { name: "土豆", category: "蔬菜", emoji: "🥔", keepDays: 10, defaultExpiryDays: 14, suggestedContainer: "冰箱", nutrition: JSON.stringify({ protein: 15, vitaminC: 45, fiber: 30, calcium: 18 }) },
  ];
  for (const c of catalog) {
    await prisma.foodCatalog.upsert({ where: { name: c.name }, update: c, create: c });
  }

  const recipes = [
    { name: "西兰花炒蛋", emoji: "🥦🍳", grad: "radial-gradient(circle at 34% 28%,#fffdf7,#d9e6bf 75%)", tag: "清淡", timeMin: 15, desc: "西兰花脆嫩，鸡蛋滑嫩，营养丰富", nutr: "蛋白质高·维生素丰富·低脂健康", ingredients: JSON.stringify([{ name: "西兰花", qty: "1 朵" }, { name: "鸡蛋", qty: "2 个" }, { name: "小葱", qty: "1 把" }]), steps: JSON.stringify(["西兰花切小朵，淡盐水焯 30 秒捞出", "鸡蛋加少许盐打散，热油炒至凝固盛出", "蒜末爆香，下西兰花大火翻炒 1 分钟", "倒入鸡蛋，加盐调味翻匀即可"]), tags: JSON.stringify(["清淡", "快手"]) },
    { name: "牛肉蔬菜汤", emoji: "🍲", grad: "radial-gradient(circle at 34% 28%,#fff8ec,#f0dcc0 78%)", tag: "家常", timeMin: 30, desc: "牛肉鲜美，蔬菜清甜，暖胃又营养", nutr: "高蛋白·铁质丰富·增强免疫力", ingredients: JSON.stringify([{ name: "牛肉", qty: "200g" }, { name: "番茄", qty: "1 个" }, { name: "胡萝卜", qty: "1 根" }]), steps: JSON.stringify(["牛肉切块冷水下锅焯去浮沫", "番茄切块与牛肉同煮 20 分钟", "加入胡萝卜块再煮 10 分钟", "盐和胡椒调味即可"]), tags: JSON.stringify(["家常", "汤"]) },
    { name: "奶香蔬菜浓汤", emoji: "🥣", grad: "radial-gradient(circle at 34% 28%,#fbf7ff,#e2dcf0 78%)", tag: "清淡", timeMin: 20, desc: "奶香浓郁，口感顺滑，适合全家", nutr: "钙质丰富·易消化·增强骨骼健康", ingredients: JSON.stringify([{ name: "牛奶", qty: "300ml" }, { name: "西兰花", qty: "半朵" }, { name: "土豆", qty: "1 个" }]), steps: JSON.stringify(["土豆蒸熟压成泥", "西兰花焯水后切碎", "牛奶小火加热，拌入土豆泥", "加入西兰花碎，搅匀调味"]), tags: JSON.stringify(["清淡", "汤"]) },
    { name: "番茄炒蛋", emoji: "🍅🍳", grad: "radial-gradient(circle at 34% 28%,#fff6ee,#f6d8c8 78%)", tag: "家常", timeMin: 10, desc: "酸甜开胃，色泽诱人，下饭首选", nutr: "维生素C·蛋白质·低脂健康", ingredients: JSON.stringify([{ name: "番茄", qty: "2 个" }, { name: "鸡蛋", qty: "2 个" }, { name: "小葱", qty: "1 把" }]), steps: JSON.stringify(["番茄切块，鸡蛋打散", "鸡蛋炒至凝固盛出", "番茄炒出汁后回锅鸡蛋", "加糖盐调味，撒葱花"]), tags: JSON.stringify(["家常", "快手"]) },
    { name: "牛奶燕麦粥", emoji: "🥛🌾", grad: "radial-gradient(circle at 34% 28%,#fffdf5,#e9e0c8 78%)", tag: "清淡", timeMin: 15, desc: "奶香丝滑，暖胃又饱腹", nutr: "钙质丰富·膳食纤维·易消化", ingredients: JSON.stringify([{ name: "牛奶", qty: "250ml" }, { name: "燕麦片", qty: "50g" }]), steps: JSON.stringify(["牛奶小火加热至微沸", "加入燕麦片搅拌 5 分钟", "关火焖 3 分钟", "依口味加蜂蜜"]), tags: JSON.stringify(["清淡", "早餐"]) },
    { name: "蒜蓉西兰花", emoji: "🥦", grad: "radial-gradient(circle at 34% 28%,#f7fff2,#cfe3b8 78%)", tag: "家常", timeMin: 12, desc: "清爽脆嫩，蒜香扑鼻", nutr: "维生素丰富·低脂·抗氧化", ingredients: JSON.stringify([{ name: "西兰花", qty: "1 朵" }, { name: "大蒜", qty: "3 瓣" }]), steps: JSON.stringify(["西兰花掰小朵焯水", "蒜末爆香", "下西兰花大火快炒", "盐调味出锅"]), tags: JSON.stringify(["家常"]) },
  ];
  for (const r of recipes) {
    await prisma.recipe.upsert({ where: { name: r.name }, update: r, create: r });
  }

  const notifs = [
    { type: "expiry", title: "2 种食材今天到期", desc: "西兰花、牛肉建议今日食用", view: "remind" },
    { type: "recipe", title: "今日推荐 · 西兰花炒蛋", desc: "使用库存食材即可完成", view: "recipe:0" },
    { type: "member", title: "妈妈录入了 2 种食材", desc: "番茄、黄瓜已同步到家庭库存", view: "inv" },
    { type: "shop", title: "购物清单有 1 项待买", desc: "小葱 · 已加入清单", view: "shop" },
  ];
  for (const n of notifs) {
    await prisma.notification.create({ data: n });
  }

  const shop = [
    { name: "牛奶", qty: 1, unit: "瓶", source: "expiry" },
    { name: "鸡蛋", qty: 6, unit: "个", source: "manual" },
    { name: "生菜", qty: 1, unit: "颗", source: "expiry" },
    { name: "小葱", qty: 1, unit: "把", source: "recipe", done: true, boughtAt: new Date() },
  ];
  for (const s of shop) {
    await prisma.shoppingListItem.create({ data: s });
  }

  console.log("shike seed done");
}

main()
  .catch((e) => {
    console.error(e);
    process.exit(1);
  })
  .finally(() => prisma.$disconnect());
