import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function POST(req: NextRequest) {
  const body = (await req.json().catch(() => null)) as {
    deviceId?: string;
    name?: string;
    firmwareVersion?: string;
    tempC?: number;
    event?: string;
    msg?: string;
  } | null;
  const deviceId = (body?.deviceId || "xzd-t5e1-001").slice(0, 64);

  const device = await prisma.device.upsert({
    where: { deviceId },
    update: {
      lastSeenAt: new Date(),
      firmwareVersion: body?.firmwareVersion || undefined,
      name: body?.name || undefined,
    },
    create: {
      deviceId,
      name: body?.name || deviceId,
      firmwareVersion: body?.firmwareVersion || null,
      lastSeenAt: new Date(),
    },
  });

  if (typeof body?.tempC === "number") {
    await prisma.temperatureLog.create({
      data: { deviceId, tempC: body.tempC, readAt: new Date() },
    });
  }
  if (body?.event) {
    await prisma.deviceLog.create({
      data: { deviceId, event: body.event.slice(0, 40), msg: body.msg?.slice(0, 200) || null },
    });
  }

  const stockTotal = await prisma.foodItem.count({ where: { deviceId } });
  return xzdJson({ code: 0, deviceId, stockTotal, lastSeenAt: device.lastSeenAt });
}

export { xzdOptions as OPTIONS };
