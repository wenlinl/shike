import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function GET() {
  const [recipes, items] = await Promise.all([
    prisma.recipe.findMany({ orderBy: { createdAt: "asc" } }),
    prisma.foodItem.findMany({ select: { name: true } }),
  ]);
  const names = new Set(items.map((i) => i.name));
  const data = recipes.map((r) => {
    const ingredients = JSON.parse(r.ingredients || "[]") as Array<{ name: string; qty: string }>;
    const ing = ingredients.map((x) => ({
      name: x.name,
      qty: x.qty,
      status: names.has(x.name) ? "有" : "缺",
    }));
    return {
      id: r.id,
      name: r.name,
      emoji: r.emoji,
      grad: r.grad,
      tag: r.tag,
      timeMin: r.timeMin,
      desc: r.desc,
      nutr: r.nutr,
      ingredients: ing,
      steps: r.steps ? JSON.parse(r.steps) : [],
      tags: r.tags ? JSON.parse(r.tags) : [],
      missing: ing.filter((x) => x.status === "缺").map((x) => x.name),
    };
  });
  return xzdJson({ code: 0, recipes: data });
}

export { xzdOptions as OPTIONS };
