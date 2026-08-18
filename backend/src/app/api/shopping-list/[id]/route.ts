import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function PATCH(req: NextRequest, ctx: { params: Promise<{ id: string }> }) {
  const { id } = await ctx.params;
  const body = (await req.json().catch(() => null)) as {
    done?: boolean;
    qty?: number;
  } | null;
  try {
    const item = await prisma.shoppingListItem.update({
      where: { id },
      data: {
        done: body?.done !== undefined ? Boolean(body.done) : undefined,
        qty: body?.qty != null ? Math.max(1, Math.min(999, Number(body.qty))) : undefined,
        boughtAt: body?.done ? new Date() : body?.done === false ? null : undefined,
      },
    });
    return xzdJson({ code: 0, item });
  } catch {
    return xzdJson({ code: 1, msg: "记录不存在" }, 404);
  }
}

export async function DELETE(_req: NextRequest, ctx: { params: Promise<{ id: string }> }) {
  const { id } = await ctx.params;
  try {
    await prisma.shoppingListItem.delete({ where: { id } });
    return xzdJson({ code: 0 });
  } catch {
    return xzdJson({ code: 1, msg: "记录不存在" }, 404);
  }
}

export { xzdOptions as OPTIONS };
