import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { chatJson } from "@/lib/ai";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

const NUT_KEYS = ["protein", "vitaminC", "fiber", "calcium"] as const;
const NUT_LABELS: Record<string, string> = {
  protein: "蛋白质",
  vitaminC: "维生素C",
  fiber: "膳食纤维",
  calcium: "钙",
};

/** 营养分析：基于当天消耗/取出/过期食材，聚合识别库营养数据 + AI 总结。 */
export async function GET(req: NextRequest) {
  const dateParam = req.nextUrl.searchParams.get("date");
  const start = dateParam
    ? new Date(dateParam + "T00:00:00")
    : new Date();
  start.setHours(0, 0, 0, 0);
  const end = new Date(start);
  end.setDate(end.getDate() + 1);

  const logs = await prisma.consumptionLog.findMany({
    where: { createdAt: { gte: start, lt: end } },
    orderBy: { createdAt: "desc" },
  });

  const counts: Record<string, { qty: number; reasons: string[] }> = {};
  for (const l of logs) {
    (counts[l.name] ??= { qty: 0, reasons: [] });
    counts[l.name].qty += l.quantity;
    if (!counts[l.name].reasons.includes(l.reason)) counts[l.name].reasons.push(l.reason);
  }
  const names = Object.keys(counts);
  const catalog = names.length
    ? await prisma.foodCatalog.findMany({ where: { name: { in: names } } })
    : [];
  const catMap = new Map(catalog.map((c) => [c.name, c]));

  const totals: Record<string, number> = { protein: 0, vitaminC: 0, fiber: 0, calcium: 0 };
  const items = names.map((n) => {
    const c = catMap.get(n);
    const nut = c?.nutrition ? JSON.parse(c.nutrition) : {};
    for (const k of NUT_KEYS) totals[k] += Number(nut[k] || 0) * counts[n].qty;
    return {
      name: n,
      quantity: counts[n].qty,
      reasons: counts[n].reasons,
      nutrition: nut,
    };
  });

  let analysis: { summary: string; suggestions: string[] } | null = null;
  try {
    analysis = await chatJson<{ summary: string; suggestions: string[] }>(
      "你是「食刻」的营养分析师。根据用户当天消耗/取出/过期食材的营养汇总，用中文给出 1 句总结和 2 条可执行建议，只输出 JSON {summary, suggestions}。",
      `今日食材：${items.map((i) => `${i.name}×${i.quantity}`).join("、") || "暂无消耗"}
营养汇总：${JSON.stringify(totals)}`,
      { temperature: 0.3, maxTokens: 400 },
    );
  } catch {
    analysis = null;
  }

  return xzdJson({
    code: 0,
    date: start.toISOString().slice(0, 10),
    items,
    totals,
    labels: NUT_LABELS,
    analysis,
  });
}

export { xzdOptions as OPTIONS };
