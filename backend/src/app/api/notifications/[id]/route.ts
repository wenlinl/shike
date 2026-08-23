import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function PATCH(req: NextRequest, ctx: { params: Promise<{ id: string }> }) {
  const { id } = await ctx.params;
  try {
    const n = await prisma.notification.update({
      where: { id },
      data: { unread: false },
    });
    return xzdJson({ code: 0, notification: n });
  } catch {
    return xzdJson({ code: 1, msg: "记录不存在" }, 404);
  }
}

export { xzdOptions as OPTIONS };
