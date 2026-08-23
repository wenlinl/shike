import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

function startOfDay(d: Date) {
  return new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate()));
}

/** 临期 / 过期提醒列表（网页大屏、H5、App 推送使用）。 */
export async function GET() {
  const rows = await prisma.foodItem.findMany({});
  const today = startOfDay(new Date());
  const soon: Array<Record<string, unknown>> = [];
  const expired: Array<Record<string, unknown>> = [];

  for (const it of rows) {
    const daysLeft = it.expiryDate
      ? Math.round(
          (startOfDay(it.expiryDate).getTime() - today.getTime()) / 86400000,
        )
      : it.daysLeft;
    const base = {
      id: it.id,
      name: it.name,
      container: it.container,
      quantity: it.quantity,
      expiryDate: it.expiryDate ? it.expiryDate.toISOString().slice(0, 10) : null,
      daysLeft,
    };
    if (daysLeft === -1) continue; // 未知到期不提醒
    if (daysLeft < 0) expired.push(base);
    else if (daysLeft <= 3) soon.push(base);
  }

  soon.sort((a, b) => (a.daysLeft as number) - (b.daysLeft as number));
  expired.sort((a, b) => (a.daysLeft as number) - (b.daysLeft as number));
  return xzdJson({ code: 0, soon, expired });
}

export { xzdOptions as OPTIONS };
