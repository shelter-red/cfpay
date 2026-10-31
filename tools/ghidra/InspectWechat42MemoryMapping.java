// Reports 4.2 section mapping and references to known parser entry points.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;

public class InspectWechat42MemoryMapping extends GhidraScript {
    private static final long[] TARGET_VAS = {0x1810b0230L, 0x182fcc400L, 0x1806a2ef0L};
    private static final long[] TARGET_RVAS = {0x10b0230, 0x2fcc400, 0x6a2ef0};

    @Override
    protected void run() throws Exception {
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            println("BLOCK|name=" + block.getName() + "|start=" + block.getStart() + "|end=" +
                block.getEnd() + "|size=0x" + Long.toHexString(block.getSize()));
        }
        for (int index = 0; index < TARGET_VAS.length; index += 1) {
            printHits("VA_" + Long.toHexString(TARGET_VAS[index]), littleEndian(TARGET_VAS[index], 8));
            printHits("RVA_" + Long.toHexString(TARGET_RVAS[index]), littleEndian(TARGET_RVAS[index], 4));
        }
        printHits("TYPE_CmdItem", "micromsg.CmdItem".getBytes("US-ASCII"));
        printHits("TYPE_NewSyncResponse", "micromsg.NewSyncResponse".getBytes("US-ASCII"));
    }

    private void printHits(String label, byte[] pattern) throws Exception {
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isInitialized()) continue;
            Address cursor = block.getStart();
            int count = 0;
            while (cursor != null && cursor.compareTo(block.getEnd()) <= 0 && count < 100) {
                Address match = currentProgram.getMemory().findBytes(cursor, block.getEnd(), pattern, null, true, monitor);
                if (match == null) break;
                println("HIT|kind=" + label + "|address=" + match + "|block=" + block.getName());
                cursor = match.add(1);
                count += 1;
            }
        }
    }

    private byte[] littleEndian(long value, int size) {
        byte[] bytes = new byte[size];
        for (int index = 0; index < size; index += 1) bytes[index] = (byte)(value >>> (index * 8));
        return bytes;
    }
}
