// Dumps vtables used by decoded network-object callback wrappers.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.List;

public class DumpWechatDecodedCallbackVtables extends GhidraScript {
    private static final List<String> TABLES = List.of(
        "1896de5d8",
        "1896de9e8",
        "1896de058",
        "1896de228",
        "1896de308",
        "1896de3c8",
        "1896de488",
        "1896de658",
        "1896de738",
        "1896de7d8",
        "1896de898"
    );
    private static final int FIRST_SLOT = -4;
    private static final int LAST_SLOT = 12;

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        for (String tableText : TABLES) {
            Address table = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(tableText);
            println("TABLE_BEGIN|address=" + table);
            ReferenceIterator tableReferences = currentProgram.getReferenceManager().getReferencesTo(table);
            while (tableReferences.hasNext()) {
                Reference reference = tableReferences.next();
                Function owner = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
                println("TABLE_REF|table=" + table + "|from=" + reference.getFromAddress() + "|type=" +
                    reference.getReferenceType() + "|owner=" +
                    (owner == null ? "<none>" : owner.getEntryPoint()));
            }
            for (int slot = FIRST_SLOT; slot <= LAST_SLOT; slot += 1) {
                Address address = table.add(slot * 8L);
                long value;
                try {
                    value = memory.getLong(address);
                } catch (Exception exception) {
                    println("SLOT_ERROR|table=" + table + "|slot=" + slot + "|address=" + address);
                    continue;
                }
                Address targetAddress = currentProgram.getAddressFactory().getDefaultAddressSpace()
                    .getAddress(value);
                Function target = currentProgram.getListing().getFunctionAt(targetAddress);
                MemoryBlock block = memory.getBlock(targetAddress);
                println("SLOT|table=" + table + "|slot=" + slot + "|address=" + address + "|value=" +
                    targetAddress + "|target=" + (target == null ? "<none>" : target.getName(true)) +
                    "|block=" + (block == null ? "<none>" : block.getName()));
            }
            println("TABLE_END|address=" + table);
        }
    }
}
