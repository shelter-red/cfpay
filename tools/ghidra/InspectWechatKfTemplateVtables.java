// Lists executable entries from ChatKfTemplateMsgItemView vtables.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

import java.util.List;

public class InspectWechatKfTemplateVtables extends GhidraScript {
    private static final List<String> TABLE_STARTS = List.of(
        "1896817a8",
        "189681938",
        "189681978",
        "1896819a8",
        "1896819c8",
        "1896819e8",
        "189681a08"
    );
    private static final int TABLE_BYTES = 0x190;

    @Override
    protected void run() throws Exception {
        for (String tableStart : TABLE_STARTS) {
            Address start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(tableStart);
            println("TABLE_BEGIN|table=" + start);
            int executableEntries = 0;
            for (int offset = 0; offset < TABLE_BYTES && !monitor.isCancelled(); offset += 8) {
                Address slot = start.add(offset);
                long raw = currentProgram.getMemory().getLong(slot);
                Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(raw);
                MemoryBlock block = currentProgram.getMemory().getBlock(target);
                if (block == null || !block.isExecute()) {
                    if (executableEntries > 0) break;
                    continue;
                }
                Function function = currentProgram.getListing().getFunctionAt(target);
                if (function == null) function = currentProgram.getListing().getFunctionContaining(target);
                println("TABLE_FUNCTION|table=" + start + "|offset=0x" + Integer.toHexString(offset) +
                    "|slot=" + slot + "|target=" + target + "|function=" +
                    (function == null ? "<none>" : function.getName(true)) + "|entry=" +
                    (function == null ? "<none>" : function.getEntryPoint()));
                executableEntries += 1;
            }
            println("TABLE_END|table=" + start + "|entries=" + executableEntries);
        }
    }
}
