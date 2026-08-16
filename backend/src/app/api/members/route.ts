import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function GET() {
  const members = await prisma.member.findMany({ orderBy: { createdAt: "asc" } });
  const start = new Date();
  start.setDate(1);
  start.setHours(0, 0, 0, 0);
  const logs = await prisma.scanLog.groupBy({
    by: ["memberId"],
    where: { createdAt: { gte: start } },
    _count: { _all: true },
  });
  const countMap = new Map(logs.map((l) => [l.memberId, l._count._all]));
  const data = members.map((m) => ({
    id: m.id,
    name: m.name,
    role: m.role,
    avatar: m.avatar || "👤",
    responsibleCategory: m.responsibleCategory,
    preferences: m.preferences ? JSON.parse(m.preferences) : [],
    monthlyScans: countMap.get(m.id) || 0,
  }));
  return xzdJson({ code: 0, members: data });
}

export { xzdOptions as OPTIONS };
