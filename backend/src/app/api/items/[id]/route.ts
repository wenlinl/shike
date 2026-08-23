import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

const CONTAINERS = ["冰箱", "零食柜", "药盒", "调料柜", "主食柜"];

/** 手动修改容器分类（网页 / App 调用）。 */
export async function PATCH(req: NextRequest, ctx: { params: Promise<{ id: string }> }) {
  const { id } = await ctx.params;
  const body = (await req.json().catch(() => null)) as { container?: string } | null;
  const container = body?.container;
  if (!container || !CONTAINERS.includes(container)) {
    return xzdJson({ code: 1, msg: "container 无效" });
  }
  try {
    const item = await prisma.foodItem.update({
      where: { id },
      data: { container },
    });
    return xzdJson({ code: 0, item });
  } catch {
    return xzdJson({ code: 1, msg: "记录不存在" }, 404);
  }
}

export { xzdOptions as OPTIONS };
