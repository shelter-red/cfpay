import { ref } from "vue";
import { toast } from "vue-sonner";
import { api, errorMessage } from "@/lib/api";
import { decodeQrFile } from "@/lib/qr";
import type { Provider, QrCodeItem } from "@/lib/types";

/** 单个渠道的收款码管理：读取、上传（本机解析二维码内容）、删除。 */
export function useQrCode(provider: Provider) {
  const item = ref<QrCodeItem | null>(null);
  const loaded = ref(false);
  const busy = ref(false);
  const failure = ref("");
  const label = provider === "wxpay" ? "微信" : "支付宝";

  async function reload(): Promise<void> {
    try {
      const data = await api<{ items: QrCodeItem[] }>("/admin-api/qr-codes");
      item.value = data.items.find((entry) => entry.provider === provider) ?? null;
      failure.value = "";
    } catch (error) {
      failure.value = errorMessage(error);
    } finally {
      loaded.value = true;
    }
  }

  async function upload(file: File | null): Promise<void> {
    if (!file) {
      toast.error("请先选择二维码图片");
      return;
    }
    busy.value = true;
    try {
      const payload = await decodeQrFile(file);
      await api(`/admin-api/qr-codes/${provider}`, { method: "PUT", body: JSON.stringify({ payload }) });
      await reload();
      toast.success(`${label}收款码已保存`);
    } catch (error) {
      toast.error(errorMessage(error));
    } finally {
      busy.value = false;
    }
  }

  async function remove(): Promise<void> {
    if (!window.confirm(`确认删除${label}收款码？删除后该支付渠道将无法展示收款码。`)) return;
    busy.value = true;
    try {
      await api(`/admin-api/qr-codes/${provider}`, { method: "DELETE" });
      await reload();
      toast.success(`${label}收款码已删除`);
    } catch (error) {
      toast.error(errorMessage(error));
    } finally {
      busy.value = false;
    }
  }

  return { item, loaded, busy, failure, label, reload, upload, remove };
}
