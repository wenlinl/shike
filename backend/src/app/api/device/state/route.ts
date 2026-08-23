import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

function startOfDay(d: Date) {
  return new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate()));
}

export async function GET(req: NextRequest) {
  const deviceId = (req.nextUrl.searchParams.get("deviceId") || "xzd-t5e1-001").slice(0, 64);
  const device = await prisma.device.findUnique({ where: { deviceId } });
  const rows = await prisma.foodItem.findMany({ where: { deviceId } });
  const today = startOfDay(new Date());
  const soon: Array<Record<string, unknown>> = [];
  const expired: Array<Record<string, unknown>> = [];
  for (const it of rows) {
    const daysLeft = it.expiryDate
      ? Math.round((startOfDay(it.expiryDate).getTime() - today.getTime()) / 86400000)
      : it.daysLeft;
    const base = { id: it.id, name: it.name, container: it.container, quantity: it.quantity, expiryDate: it.expiryDate?.toISOString().slice(0, 10) || null, daysLeft };
    if (daysLeft === -1) continue; // 未知到期不提醒
    if (daysLeft < 0) expired.push(base);
    else if (daysLeft <= 3) soon.push(base);
  }
  soon.sort((a, b) => (a.daysLeft as number) - (b.daysLeft as number));
  expired.sort((a, b) => (a.daysLeft as number) - (b.daysLeft as number));
  const lastResult = await prisma.scanLog.findFirst({
    where: { deviceId },
    orderBy: { createdAt: "desc" },
  });
  return xzdJson({
    code: 0,
    device: device ? { deviceId, name: device.name, lastSeenAt: device.lastSeenAt } : null,
    stockTotal: rows.reduce((s, r) => s + r.quantity, 0),
    soon,
    expired,
    lastResult: lastResult
      ? { name: lastResult.name, action: lastResult.action, expiryDate: lastResult.expiryDate?.toISOString().slice(0, 10) || null, daysLeft: lastResult.daysLeft, suggestedContainer: lastResult.container }
      : null,
  });
}

export { xzdOptions as OPTIONS };
