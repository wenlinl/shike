import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function PATCH(req: NextRequest, ctx: { params: Promise<{ id: string }> }) {
  const { id } = await ctx.params;
  const body = (await req.json().catch(() => null)) as { done?: boolean } | null;
  try {
    const item = await prisma.shoppingListItem.update({
      where: { id },
      data: { done: Boolean(body?.done), boughtAt: body?.done ? new Date() : null },
    });
    return xzdJson({ code: 0, item });
  } catch {
    return xzdJson({ code: 1, msg: "记录不存在" }, 404);
  }
}

export { xzdOptions as OPTIONS };
