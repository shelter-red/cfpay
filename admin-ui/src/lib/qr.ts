import jsQR from "jsqr";

const MAX_QR_IMAGE_BYTES = 10 * 1024 * 1024;
const MAX_DECODE_EDGE = 2048;

/**
 * 收款码原图只在浏览器内解析：优先用原生 BarcodeDetector，回退到 jsQR。
 * 解析出的二维码内容上传给服务端，服务端重新生成标准 SVG，原图不出本机。
 */
export async function decodeQrFile(file: File): Promise<string> {
  if (!file.type.startsWith("image/")) throw new Error("请选择 PNG 或 JPEG 格式的二维码图片");
  if (file.size > MAX_QR_IMAGE_BYTES) throw new Error("图片不能超过 10 MiB");

  const bitmap = await createImageBitmap(file);
  try {
    if ("BarcodeDetector" in globalThis) {
      try {
        const Detector = (globalThis as unknown as {
          BarcodeDetector: new (options: { formats: string[] }) => {
            detect: (image: ImageBitmap) => Promise<Array<{ rawValue?: string }>>;
          };
        }).BarcodeDetector;
        const results = await new Detector({ formats: ["qr_code"] }).detect(bitmap);
        const value = results[0]?.rawValue;
        if (value) return value;
      } catch {
        // 原生识别失败时继续走 jsQR
      }
    }

    const scale = Math.min(1, MAX_DECODE_EDGE / Math.max(bitmap.width, bitmap.height));
    const canvas = document.createElement("canvas");
    canvas.width = Math.max(1, Math.round(bitmap.width * scale));
    canvas.height = Math.max(1, Math.round(bitmap.height * scale));
    const context = canvas.getContext("2d", { willReadFrequently: true });
    if (!context) throw new Error("当前浏览器不支持解析图片，请更换浏览器");
    context.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
    const image = context.getImageData(0, 0, canvas.width, canvas.height);
    const result = jsQR(image.data, image.width, image.height, { inversionAttempts: "attemptBoth" });
    if (result?.data) return result.data;
    throw new Error("没有识别到二维码，请使用清晰、完整、无遮挡的原图");
  } finally {
    bitmap.close?.();
  }
}
