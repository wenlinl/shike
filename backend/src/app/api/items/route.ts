import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

function startOfDay(d: Date) {
  return new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate()));
}

/** 库存列表：按容器筛选，按剩余天数升序（软件端大屏 / H5 / 设备共用）。 */
export async function GET(req: NextRequest) {
  const container = req.nextUrl.searchParams.get("container");
  const where =
    container && container !== "全部" && container !== "" ? { container } : {};
  const [rows, members] = await Promise.all([
    prisma.foodItem.findMany({
      where,
      orderBy: [{ daysLeft: "asc" }, { scannedAt: "desc" }],
    }),
    prisma.member.findMany({ select: { id: true, name: true, avatar: true } }),
  ]);
  const memberMap = new Map(members.map((m) => [m.id, m]));
  const today = startOfDay(new Date());
  const items = rows.map((it) => {
    const daysLeft = it.expiryDate
      ? Math.round((startOfDay(it.expiryDate).getTime() - today.getTime()) / 86400000)
      : it.daysLeft;
    const member = it.memberId ? memberMap.get(it.memberId) : null;
    return {
      id: it.id,
      name: it.name,
      category: it.category,
      emoji: it.emoji,
      container: it.container,
      quantity: it.quantity,
      recordMethod: it.recordMethod,
      keepDays: it.keepDays,
      storage: it.storage,
      label: it.label,
      recordedBy: member?.name || null,
      recordedByAvatar: member?.avatar || null,
      expiryDate: it.expiryDate ? it.expiryDate.toISOString().slice(0, 10) : null,
      scannedAt: it.scannedAt.toISOString(),
      daysLeft,
      confidence: it.confidence,
      note: it.note,
      status: daysLeft === -1 ? "unknown" : daysLeft < 0 ? "expired" : daysLeft <= 3 ? "soon" : "normal",
    };
  });
  return xzdJson({ code: 0, items });
}

export { xzdOptions as OPTIONS };
