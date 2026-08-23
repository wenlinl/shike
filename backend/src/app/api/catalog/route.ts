import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function GET() {
  const list = await prisma.foodCatalog.findMany({ orderBy: { name: "asc" } });
  return xzdJson({
    code: 0,
    catalog: list.map((c) => ({
      id: c.id,
      name: c.name,
      category: c.category,
      emoji: c.emoji,
      keepDays: c.keepDays,
      defaultExpiryDays: c.defaultExpiryDays,
      suggestedContainer: c.suggestedContainer,
      nutrition: c.nutrition ? JSON.parse(c.nutrition) : null,
      source: c.source,
    })),
  });
}

export { xzdOptions as OPTIONS };
