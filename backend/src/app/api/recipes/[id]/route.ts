import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

/** 收藏 / 做过：POST /api/recipes/[id]  body { action:"favorite", liked } 或 { action:"cooked" } */
export async function POST(req: NextRequest, ctx: { params: Promise<{ id: string }> }) {
  const { id } = await ctx.params;
  const body = (await req.json().catch(() => null)) as {
    action?: string;
    liked?: boolean;
  } | null;
  const recipe = await prisma.recipe.findUnique({ where: { id } });
  if (!recipe) return xzdJson({ code: 1, msg: "菜谱不存在" }, 404);

  if (body?.action === "favorite") {
    const liked = Boolean(body.liked);
    const pref = await prisma.recipePreference.upsert({
      where: { recipeId: id },
      update: { liked },
      create: { recipeId: id, liked },
    });
    return xzdJson({ code: 0, liked: pref.liked });
  }
  if (body?.action === "cooked") {
    const pref = await prisma.recipePreference.upsert({
      where: { recipeId: id },
      update: { timesCooked: { increment: 1 }, lastCookedAt: new Date() },
      create: { recipeId: id, timesCooked: 1, lastCookedAt: new Date() },
    });
    return xzdJson({ code: 0, timesCooked: pref.timesCooked });
  }
  return xzdJson({ code: 1, msg: "action 无效" });
}

export { xzdOptions as OPTIONS };
