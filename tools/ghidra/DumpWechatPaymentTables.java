// Dumps function-table neighborhoods containing Weixin payment display callbacks.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.util.List;

public class DumpWechatPaymentTables extends GhidraScript {
    private static final List<String> SLOTS = List.of(
        "1897d1988",
        "1897d2348",
        "1897d25c8"
    );

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        Memory memory = currentProgram.getMemory();
        ReferenceManager referenceManager = currentProgram.getReferenceManager();

        for (String value : SLOTS) {
            Address slot = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            println("TABLE_BEGIN|slot=" + slot);

            ReferenceIterator references = referenceManager.getReferencesTo(slot);
            while (references.hasNext()) {
                Reference reference = references.next();
                Function caller = listing.getFunctionContaining(reference.getFromAddress());
                println(
                    "SLOT_REF|slot=" + slot+
                    "|from=" + reference.getFromAddress()+
                    "|type=" + reference.getReferenceType()+
                    "|caller=" + (caller == null ? "<none>" : caller.getName(true))+
                    "|caller_entry=" + (caller == null ? "<none>" : caller.getEntryPoint())
                );
            }

            for (long delta = -0x80; delta <= 0x80; delta += 8) {
                Address cell = slot.add(delta);
                long raw;
                try {
                    raw = memory.getLong(cell);
                } catch (Exception exception) {
                    println("CELL_ERROR|address=" + cell + "|message=" + clean(exception.getMessage()));
                    continue;
                }
                Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(raw);
                Function function = target == null ? null : listing.getFunctionAt(target);
                MemoryBlock block = target == null ? null : memory.getBlock(target);
                String text = target == null ? "" : readAscii(memory, block, target);
                println(
                    "CELL|address=" + cell+
                    "|delta=" + delta+
                    "|raw=0x" + Long.toUnsignedString(raw, 16)+
                    "|target=" + (target == null ? "<none>" : target)+
                    "|block=" + (block == null ? "<none>" : block.getName())+
                    "|function=" + (function == null ? "<none>" : function.getName(true))+
                    "|text=" + clean(text)
                );
            }
            println("TABLE_END|slot=" + slot);
        }
    }

    private String readAscii(Memory memory, MemoryBlock block, Address address) {
        if (block == null || block.isExecute()) return "";
        StringBuilder result = new StringBuilder();
        try {
            for (int index = 0; index < 96; index++) {
                int value = memory.getByte(address.add(index)) & 0xff;
                if (value == 0) break;
                if (value < 0x20 || value > 0x7e) return "";
                result.append((char) value);
            }
        } catch (Exception ignored) {
            return "";
        }
        return result.length() >= 4 ? result.toString() : "";
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
