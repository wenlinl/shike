import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function GET() {
  const logs = await prisma.consumptionLog.findMany({
    orderBy: { createdAt: "desc" },
    take: 200,
  });
  const summary = { consumed: 0, wasted: 0, expired: 0, total: 0 };
  for (const l of logs) {
    summary.total += l.quantity;
    if (l.reason === "consumed") summary.consumed += l.quantity;
    else if (l.reason === "wasted") summary.wasted += l.quantity;
    else if (l.reason === "expired") summary.expired += l.quantity;
  }
  return xzdJson({
    code: 0,
    logs: logs.map((l) => ({
      id: l.id,
      name: l.name,
      reason: l.reason,
      quantity: l.quantity,
      note: l.note,
      createdAt: l.createdAt,
    })),
    summary,
  });
}

export async function POST(req: NextRequest) {
  const body = (await req.json().catch(() => null)) as {
    name?: string;
    reason?: string;
    quantity?: number;
    note?: string;
    memberId?: string;
  } | null;
  const name = (body?.name || "").trim();
  const reason = ["consumed", "wasted", "expired"].includes(body?.reason || "")
    ? body!.reason!
    : "consumed";
  if (!name) return xzdJson({ code: 1, msg: "name 必填" });
  const qty = Math.max(1, Number(body?.quantity) || 1);

  const item = await prisma.foodItem.findFirst({
    where: { name: { contains: name } },
    orderBy: { createdAt: "asc" },
  });
  if (item) {
    const next = item.quantity - qty;
    if (next <= 0) {
      await prisma.foodItem.delete({ where: { id: item.id } });
    } else {
      await prisma.foodItem.update({ where: { id: item.id }, data: { quantity: next } });
    }
  }

  const log = await prisma.consumptionLog.create({
    data: { name, reason, quantity: qty, note: body?.note || null, memberId: body?.memberId || null },
  });
  return xzdJson({ code: 0, log, remaining: item ? Math.max(0, item.quantity - qty) : 0 });
}

export { xzdOptions as OPTIONS };
