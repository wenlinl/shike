import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function GET() {
  const list = await prisma.notification.findMany({
    orderBy: { createdAt: "desc" },
    take: 50,
  });
  return xzdJson({
    code: 0,
    notifications: list.map((n) => ({
      id: n.id,
      type: n.type,
      title: n.title,
      desc: n.desc,
      unread: n.unread,
      view: n.view,
      createdAt: n.createdAt,
    })),
  });
}

export { xzdOptions as OPTIONS };
