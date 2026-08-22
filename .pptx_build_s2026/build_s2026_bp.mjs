import fs from "node:fs/promises";
import { Presentation, PresentationFile } from "@oai/artifact-tool";

const OUT = process.env.FINAL_PPTX;
const M = "/Users/lwl/Documents/ChatGPT/HsHH/media";

const INK = "#141414";
const MUTED = "#5A6472";
const PANEL = "#EDEDED";
const RULE = "#B8BCC4";
const ACCENT = "#3D8DFF";
const SOFT = "#6DCBF4";
const WHITE = "#FFFFFF";

async function bytes(path) {
  const buf = await fs.readFile(path);
  return new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength);
}

function mime(path) {
  return /\.jpe?g$/i.test(path) ? "image/jpeg" : "image/png";
}

export async function build() {
  const p = Presentation.create({ slideSize: { width: 1280, height: 720 } });

  function text(slide, str, x, y, w, h, o = {}) {
    const t = slide.shapes.add({
      geometry: "textbox",
      position: { left: x, top: y, width: w, height: h },
      fill: "none",
      line: { style: "solid", fill: "none", width: 0 },
    });
    t.text = str;
    t.text.style = {
      fontSize: o.size ?? 18,
      bold: o.bold ?? false,
      color: o.color ?? INK,
      alignment: o.align ?? "left",
      verticalAlignment: o.vAlign ?? "top",
    };
    return t;
  }

  function panel(slide, x, y, w, h, fill = PANEL, radius = "rounded-2xl") {
    return slide.shapes.add({
      geometry: "roundRect",
      position: { left: x, top: y, width: w, height: h },
      fill,
      line: { style: "solid", fill: "none", width: 0 },
      borderRadius: radius,
    });
  }

  async function image(slide, path, x, y, w, h, fit = "contain", geometry = "roundRect", radius = "rounded-2xl") {
    const blob = await bytes(path);
    return slide.images.add({
      blob,
      contentType: mime(path),
      alt: path.split("/").pop(),
      fit,
      position: { left: x, top: y, width: w, height: h },
      geometry,
      borderRadius: radius,
    });
  }

  function titleBlock(slide, kicker, title) {
    text(slide, kicker, 72, 52, 1000, 28, { size: 15, bold: true, color: ACCENT });
    text(slide, title, 72, 88, 1136, 88, { size: 38, bold: true, color: INK });
    panel(slide, 72, 184, 1136, 2, RULE, "rounded-none");
  }

  function notes(slide, lines) {
    slide.speakerNotes.textFrame.setText(lines);
    slide.speakerNotes.setVisible(false);
  }

  function badge(slide, label, x, y, w, h, fill) {
    panel(slide, x, y, w, h, fill, "rounded-xl");
    text(slide, label, x, y + 6, w, h - 12, { size: 26, bold: true, color: WHITE, align: "center", vAlign: "middle" });
  }

  /* ---------------- Slide 1 · Cover ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    await image(s, `${M}/产品宣传图/01-产品实景图-冰箱门展示.png`, 640, 0, 640, 720, "cover");
    text(s, "S创2026 · 商业计划书", 72, 96, 520, 30, { size: 16, bold: true, color: ACCENT });
    text(s, "食刻 ShiKe", 72, 148, 520, 120, { size: 60, bold: true, color: INK });
    text(s, "贴在冰箱上的 AI 食品扫描台", 72, 286, 520, 44, { size: 28, bold: true, color: INK });
    text(s, "对存量电器进行智能化+AI改造", 72, 350, 520, 36, { size: 20, bold: true, color: ACCENT });
    text(s, "食刻在场，不浪费每一份食材", 72, 414, 520, 34, { size: 20, color: MUTED });
    text(s, "扫 → 认 → 记 → 显", 72, 486, 520, 30, { size: 18, bold: true, color: MUTED });
    notes(s, [
      "开场：一台贴在冰箱上的 AI 食品扫描台——扫一下，就知道它是什么、能放多久、该怎么办。",
      "核心理念：对存量电器进行智能化+AI改造，不换冰箱，以最小成本体验智能家居。",
    ]);
  }

  /* ---------------- Slide 2 · Problem ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "THE PROBLEM", "食品浪费，从“放进去就忘”开始");
    const metrics = [
      ["1/3", "全球每年被浪费的食物"],
      ["1亿吨+", "中国每年食物浪费量"],
      ["3亿台+", "中国家庭冰箱保有量"],
    ];
    metrics.forEach(([num, label], i) => {
      const x = 72 + i * 392;
      panel(s, x, 220, 352, 210, PANEL);
      text(s, num, x, 250, 352, 96, { size: 60, bold: true, color: ACCENT, align: "center", vAlign: "middle" });
      text(s, label, x, 362, 352, 40, { size: 20, color: MUTED, align: "center" });
    });
    panel(s, 72, 476, 1136, 128, "#F2F6FD");
    text(s, "临期遗忘、重复购买、过期扔掉——冰箱是家里最容易被遗忘的库存系统。", 104, 504, 1072, 72, { size: 24, bold: true, color: INK, vAlign: "middle" });
    notes(s, [
      "数据来源：项目商业化分析报告与公开市场数据（全球每年约 1/3 食物被浪费、中国年浪费超 1 亿吨、家庭冰箱保有量超 3 亿台）。",
      "核心痛点：不是不会做饭，而是“放进去就忘”。",
    ]);
  }

  /* ---------------- Slide 3 · Solution / Flow ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "THE SOLUTION", "扫一下，就知道它是什么、能放多久、该怎么办");
    const steps = [
      ["扫", "食品放到镜头前，拍照上传"],
      ["认", "云端视觉 AI 实时识别品名与保质期"],
      ["记", "库存自动 ±1，自动记账"],
      ["显", "屏幕 / H5 / 大屏主动提醒"],
    ];
    steps.forEach(([name, desc], i) => {
      const x = 72 + i * 292;
      panel(s, x, 220, 248, 240, PANEL);
      text(s, `0${i + 1}`, x + 20, 234, 60, 28, { size: 16, bold: true, color: SOFT });
      text(s, name, x, 268, 248, 72, { size: 44, bold: true, color: ACCENT, align: "center", vAlign: "middle" });
      text(s, desc, x, 352, 248, 96, { size: 18, color: MUTED, align: "center" });
      if (i < 3) text(s, "→", x + 246, 290, 48, 80, { size: 40, bold: true, color: RULE, align: "center", vAlign: "middle" });
    });
    text(s, "端到端可演示产品已跑通：硬件已烧录 · 后端已上线 · H5/大屏可用 · 3D 外壳完成", 72, 520, 1136, 44, { size: 20, bold: true, color: INK, align: "center" });
    notes(s, [
      "核心链路：扫（拍照）→ 认（云端视觉 AI）→ 记（库存 ±1）→ 显（屏幕 + H5 + 大屏）。",
      "已作为端到端可演示产品跑通，不是概念原型。",
    ]);
  }

  /* ---------------- Slide 4 · Product Concept ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "PRODUCT CONCEPT", "对存量电器进行智能化+AI改造");
    const bullets = [
      ["不换冰箱", "直接贴/吸在现有冰箱上，让现有家电变聪明"],
      ["零学习成本", "扫一下，自动识别、自动记账"],
      ["最低成本体验智能家居", "硬件成本约百元级，人人都用得起"],
      ["家庭共享", "多位成员实时同步，一起管好冰箱"],
    ];
    bullets.forEach(([t, d], i) => {
      const y = 226 + i * 100;
      text(s, `0${i + 1}`, 72, y, 64, 40, { size: 26, bold: true, color: SOFT });
      text(s, t, 148, y - 6, 480, 40, { size: 24, bold: true, color: INK });
      text(s, d, 148, y + 40, 960, 40, { size: 17, color: MUTED });
    });
    panel(s, 72, 600, 1136, 72, "#F2F6FD");
    text(s, "产品形态：硬件扫描台（贴装） + 云端 AI 识别 + 手机 H5 / 大屏多端", 104, 616, 1072, 40, { size: 20, bold: true, color: INK, align: "center", vAlign: "middle" });
    notes(s, [
      "为什么是改造：存量冰箱数量庞大、换机周期长，改造比替换更易普及。",
      "产品形态：硬件扫描台 + 云端 AI 识别 + 手机 H5 / 大屏，不做整机替换。",
    ]);
  }

  /* ---------------- Slide 5 · Features ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "PRODUCT", "十个功能，把食材管理变成一件小事");
    const feats = [
      ["扫描入库", "AI 实时识别品名与保质期"],
      ["临期提醒", "到期主动提醒"],
      ["库存管理", "家庭库存一目了然"],
      ["菜谱推荐", "按库存推荐今日菜谱"],
      ["营养分析", "评估饮食均衡与营养摄入"],
      ["购物清单", "自动生成补货清单"],
      ["家庭共享", "多位成员实时同步"],
      ["浪费统计", "月度过期/浪费数据汇总"],
      ["过期处理建议", "临期食材给出处理建议"],
      ["多端显示", "屏幕 + 手机 H5 + 大屏"],
    ];
    feats.forEach(([t, d], i) => {
      const col = i < 5 ? 0 : 1;
      const row = i % 5;
      const x = 72 + col * 584;
      const y = 214 + row * 92;
      text(s, `• ${t}`, x, y, 520, 34, { size: 20, bold: true, color: INK });
      text(s, d, x + 22, y + 38, 520, 34, { size: 16, color: MUTED });
    });
    notes(s, [
      "十项功能覆盖：录入、提醒、库存、菜谱、营养、购物、共享、统计、处理建议、多端。",
      "对外演讲时挑前 5 项讲即可，其余作为支撑。",
    ]);
  }

  /* ---------------- Slide 6 · APP Demo ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "DEMO · 软件端", "库存、菜谱、营养、扫描都在手边");
    badge(s, "软件端", 1016, 100, 192, 64, ACCENT);
    const apps = [
      ["01-首页.png", "首页"],
      ["02-我的库存.png", "我的库存"],
      ["04-营养分析.jpg", "营养分析"],
      ["05-菜谱推荐.png", "菜谱推荐"],
      ["06-扫描识别.jpg", "扫描识别"],
    ];
    apps.forEach(([file, label], i) => {
      const x = 72 + i * 216;
      panel(s, x, 206, 184, 380, PANEL);
      image(s, `${M}/软件示意图/手机APP/${file}`, x + 10, 216, 164, 352, "contain").catch(() => {});
      text(s, label, x, 606, 184, 36, { size: 18, bold: true, color: INK, align: "center" });
    });
    notes(s, [
      "Demo 截图来源：media/软件示意图/手机APP/（食刻 H5 实际页面）。",
      "首页总览、库存、营养分析、菜谱推荐、扫描识别五个核心页面。",
    ]);
  }

  /* ---------------- Slide 7 · Hardware Demo ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "DEMO · 硬件端", "待机、倒计时、识别、结果上屏全流程");
    badge(s, "硬件端", 1016, 100, 192, 64, "#F97316");
    const hw = [
      ["01-待机页.png", "待机页"],
      ["02-拍照倒计时.png", "拍照倒计时"],
      ["04-识别结果-放入.png", "识别结果 · 放入"],
      ["10-硬件界面-总览.png", "全流程总览"],
    ];
    hw.forEach(([file, label], i) => {
      const x = 72 + i * 292;
      panel(s, x, 206, 248, 340, PANEL);
      image(s, `${M}/软件示意图/硬件设备界面/${file}`, x + 12, 214, 224, 316, "contain").catch(() => {});
      text(s, label, x, 566, 344, 36, { size: 18, bold: true, color: INK, align: "center" });
    });
    text(s, "T5-E1 单板：屏 / 触控 / 摄像头 / 音频全板载，识别结果即时上屏", 72, 632, 1136, 40, { size: 18, color: MUTED, align: "center" });
    notes(s, [
      "Demo 截图来源：media/软件示意图/硬件设备界面/（T5-E1 屏幕实际界面）。",
      "待机常显临期提醒；按下扫描 → 3 秒倒计时 → 识别 → 结果上屏。",
    ]);
  }

  /* ---------------- Slide 9 · Marketing Assets ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "BRAND · 宣传物料", "对外宣传物料：宣传长图");
    const posters = [
      ["宣传长图-上.png", "宣传长图 · 上"],
      ["宣传长图-下.png", "宣传长图 · 下"],
    ];
    const SPLIT = "/Users/lwl/Documents/ChatGPT/HsHH/.pptx_build_s2026";
    posters.forEach(([file, label], i) => {
      const x = 72 + i * 596;
      panel(s, x, 196, 540, 480, PANEL);
      image(s, `${SPLIT}/${file}`, x + 16, 204, 508, 452, "contain").catch(() => {});
      text(s, label, x, 668, 540, 32, { size: 18, bold: true, color: INK, align: "center" });
    });
    notes(s, [
      "宣传长图按上下两段展示，完整呈现全部内容；原图在 media/产品宣传图/06-宣传长图-高清.png。",
    ]);
  }

  /* ---------------- Slide 10 · Target Users ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "AUDIENCE", "从家庭到办公空间，共创“不浪费”场景");
    panel(s, 72, 216, 548, 380, PANEL);
    text(s, "核心人群", 104, 246, 480, 40, { size: 26, bold: true, color: ACCENT });
    text(s, "注重健康与性价比的中国家庭：\n• 多口之家（老人/儿童同住）\n• 大城市的独居单身人士\n• 厨房新手与健身人群", 104, 306, 480, 240, { size: 20, color: INK });
    panel(s, 660, 216, 548, 380, PANEL);
    text(s, "共创与延展场景", 692, 246, 480, 40, { size: 26, bold: true, color: ACCENT });
    text(s, "• 办公空间共享冰箱 / 零食柜管理\n• 食堂 · 餐饮 · 药房合规盘点\n• 社区与真实家庭用户共创\n• 出海日本（食品浪费意识最强）", 692, 306, 480, 240, { size: 20, color: INK });
    notes(s, [
      "核心人群之外，办公空间共享冰箱是重要的共创试点方向。",
      "日本市场：食品浪费社会共识最强、包装日期体系规范、暂无外挂扫描台竞品。",
    ]);
  }

  /* ---------------- Slide 11 · Market ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "MARKET", "为什么是现在：存量电器改造的机会窗口");
    const points = [
      ["存量巨大", "中国冰箱保有量超 3 亿台，改造比替换更易普及"],
      ["趋势明确", "智能家居普及，独居与小型家庭增多"],
      ["需求升级", "健康与营养管理意识增强"],
      ["现方案不普惠", "整机冰箱贵、手动 App 难坚持"],
    ];
    points.forEach(([t, d], i) => {
      const y = 220 + i * 104;
      text(s, t, 72, y, 200, 44, { size: 24, bold: true, color: ACCENT });
      text(s, d, 288, y, 920, 72, { size: 20, color: INK });
    });
    panel(s, 72, 622, 1136, 64, "#F2F6FD");
    text(s, "食刻以“改造 + 零学习成本 + 家庭共享”切入，不与巨头正面竞争。", 104, 638, 1072, 36, { size: 20, bold: true, color: INK, align: "center" });
    notes(s, [
      "市场判断来源：项目商业化分析报告（海尔/美的/三星 AI 冰箱已内置类似功能，免费 App 已完成市场教育）。",
      "切入楔子：存量冰箱改造 + 零学习成本 + 家庭共享。",
    ]);
  }

  /* ---------------- Slide 12 · Business Model ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "BUSINESS MODEL", "双线商业模式：C 端订阅 + B 端 SaaS");
    const models = [
      ["C 端", "硬件 + 增值订阅", "AI 识别 / 营养分析 / 多家庭共享等订阅服务"],
      ["B 端", "合规盘点 / 空间共创 SaaS", "食堂 / 餐饮 / 药房盘点；办公空间共享冰箱"],
      ["通道", "技术合作", "向家电厂商与空间运营方开放识别技术合作"],
    ];
    models.forEach(([tag, t, d], i) => {
      const x = 72 + i * 392;
      panel(s, x, 220, 352, 320, PANEL);
      text(s, tag, x, 252, 352, 40, { size: 22, bold: true, color: ACCENT, align: "center" });
      text(s, t, x, 300, 352, 44, { size: 24, bold: true, color: INK, align: "center" });
      text(s, d, x + 28, 360, 296, 140, { size: 17, color: MUTED, align: "center" });
    });
    text(s, "定价细节将在 100 人真实用户测试后确定。", 72, 600, 1136, 40, { size: 18, color: MUTED, align: "center" });
    notes(s, [
      "商业模式建议来自商业化分析：C 端硬件+订阅、B 端轻资产 SaaS 先行验证付费意愿。",
      "保留向家电厂商标识技术授权的退出通道。",
    ]);
  }

  /* ---------------- Slide 13 · Progress ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "PROGRESS", "已完成端到端可演示原型");
    const done = [
      "已有可演示原型机",
      "硬件烧录完成（T5-E1 单板）",
      "云端后端上线，AI 识别可用",
      "H5 手机端与大屏 Web 可用",
      "3D 打印外壳完成",
    ];
    done.forEach((d, i) => {
      const y = 210 + i * 64;
      text(s, "✓", 72, y, 56, 44, { size: 26, bold: true, color: ACCENT });
      text(s, d, 144, y + 2, 800, 44, { size: 20, color: INK });
    });
    panel(s, 960, 210, 248, 260, "#E7F0FE");
    panel(s, 960, 210, 6, 260, ACCENT, "rounded-none");
    text(s, "当前阶段", 988, 244, 192, 36, { size: 20, bold: true, color: ACCENT });
    text(s, "端到端原型已跑通", 988, 300, 192, 36, { size: 18, color: INK });
    text(s, "商业化验证阶段", 988, 340, 192, 40, { size: 20, bold: true, color: INK });
    panel(s, 72, 548, 1136, 120, "#F2F6FD");
    panel(s, 104, 576, 132, 44, ACCENT, "rounded-xl");
    text(s, "下一步", 104, 584, 132, 32, { size: 20, bold: true, color: WHITE, align: "center" });
    text(s, "100人真实用户测试 · 识别率目标 ≥90% · 验证临期提醒与留存 · 同步办公空间试点", 264, 590, 916, 40, { size: 20, bold: true, color: INK, vAlign: "middle" });
    notes(s, [
      "商业化准备度：硬件/固件完成度高；视觉识别准确率与日期提取是当前最大短板，需先提升到 ≥90%。",
      "30 天计划：100 人真实用户测试后，再决定众筹或 B 端样板客户路线。",
    ]);
  }

  /* ---------------- Slide 14 · Social Value ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "IMPACT", "减少浪费，也是更健康、更普惠的生活");
    const vals = [
      ["减少浪费", "对应 SDG 12 负责任的消费与生产\n让“不浪费”成为日常动作"],
      ["健康饮食", "营养分析 + 菜谱推荐，吃得更健康、更简单"],
      ["可负担的智能家居", "百元级硬件成本，让智能生活普惠可及"],
      ["女性创业", "4 位年轻女性，用技术解决日常社会问题"],
    ];
    vals.forEach(([t, d], i) => {
      const x = 72 + i * 292;
      panel(s, x, 220, 248, 360, PANEL);
      text(s, t, x + 20, 256, 208, 72, { size: 24, bold: true, color: ACCENT, align: "center", vAlign: "middle" });
      text(s, d, x + 24, 340, 200, 200, { size: 18, color: MUTED, align: "center", vAlign: "middle" });
    });
    notes(s, [
      "社会价值贯穿叙事：减少浪费、健康饮食、普惠智能家居、女性创业。",
      "SDG 12：负责任的消费和生产。",
    ]);
  }

  /* ---------------- Slide 15 · Team ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "TEAM", "4 位年轻女性创业者，从原型走到今天");
    text(s, "我们在早期创业活动中相识组队，因共同的“冰箱食物浪费”痛点走到一起。", 72, 240, 1136, 72, { size: 26, bold: true, color: INK });
    text(s, "团队成员职业背景各不相同、优势互补；团队小而高效，覆盖硬件、云端、软件与产品叙事，崇尚快速验证与真实反馈——从原型做到今天可演示、可落地的完整产品。", 72, 340, 1136, 120, { size: 22, color: MUTED });
    const chips = ["早期创业活动相识组队", "端到端产品已跑通", "快速验证 · 真实反馈", "女性视角更懂家庭饮食"];
    chips.forEach((c, i) => {
      const x = 72 + i * 292;
      panel(s, x, 500, 248, 72, PANEL);
      text(s, c, x, 522, 248, 32, { size: 18, bold: true, color: INK, align: "center" });
    });
    notes(s, [
      "团队背景：4 位年轻女性创业者，职业背景各不相同、优势互补，早期创业活动相识组队。",
    ]);
  }

  /* ---------------- Slide 16 · Roadmap ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    titleBlock(s, "ROADMAP", "下一步：从原型走向规模化");
    const plans = [
      ["一键补货", "联动生鲜电商/商超，库存告急一键下单"],
      ["B 端盘点与空间共创", "食堂/餐饮/药房/办公空间"],
      ["磁吸门版 v2", "BOM ¥70–90，贴门内/门外两用"],
      ["门内连续扫描", "批量采购一次过件、关门汇总确认"],
      ["边缘识别 + 离线兜底", "已知食品本地秒级识别，断网也能记账"],
      ["出海日本首发", "满足当地合规，再拓展欧美"],
    ];
    plans.forEach(([t, d], i) => {
      const col = i < 3 ? 0 : 1;
      const row = i % 3;
      const x = 72 + col * 584;
      const y = 216 + row * 136;
      panel(s, x, y, 544, 108, PANEL);
      text(s, t, x + 24, y + 20, 496, 40, { size: 22, bold: true, color: INK });
      text(s, d, x + 24, y + 64, 496, 36, { size: 17, color: MUTED });
    });
    notes(s, [
      "路线图与 README 未来迭代方向一致：硬件 v2、连续扫描、边缘识别、离线、B 端、出海。",
    ]);
  }

  /* ---------------- Slide 17 · Closing ---------------- */
  {
    const s = p.slides.add();
    s.background.fill = WHITE;
    text(s, "S创2026", 72, 90, 400, 30, { size: 16, bold: true, color: ACCENT });
    text(s, "食刻在场，不浪费每一份食材", 72, 210, 1136, 96, { size: 52, bold: true, color: INK, align: "center" });
    text(s, "贴在冰箱上的 AI 食品扫描台 · 对存量电器进行智能化+AI改造", 72, 330, 1136, 44, { size: 24, color: MUTED, align: "center" });
    text(s, "感谢聆听 · 期待与 S创2026 共创", 72, 470, 1136, 48, { size: 24, bold: true, color: ACCENT, align: "center" });
    notes(s, [
      "结束语：期待与 S创2026 一起，把“减少浪费”变成更多人触手可及的日常。",
    ]);
  }

  const pptx = await PresentationFile.exportPptx(p);
  await pptx.save(OUT);
  console.log("saved:", OUT);
}

build().catch((e) => {
  console.error(e);
  process.exit(1);
});
