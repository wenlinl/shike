import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

/** 识别库查「建议保鲜天数」（H5 扫描结果补充用）。 */
export async function GET(req: NextRequest) {
  const name = (req.nextUrl.searchParams.get("name") || "").trim().slice(0, 40);
  if (!name) return xzdJson({ code: 1, msg: "name 必填" });
  const c = await prisma.foodCatalog.findUnique({ where: { name } });
  const daysLeft = c?.keepDays ?? c?.defaultExpiryDays ?? null;
  return xzdJson({ code: 0, name, keepDays: daysLeft, daysLeft });
}

export { xzdOptions as OPTIONS };
