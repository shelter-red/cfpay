// Dumps the function-table neighborhood containing the current nickname callback.
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

public class DumpWechatAccountTables extends GhidraScript {
    private static final String SLOT = "188eb95f0";
    private static final List<String> TABLE_ADDRESSES = List.of(
        "188eb9590",
        "188eb9598",
        "188eb9680"
    );

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        Memory memory = currentProgram.getMemory();
        ReferenceManager referenceManager = currentProgram.getReferenceManager();
        Address slot = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(SLOT);

        println("TABLE_BEGIN|slot=" + slot);
        for (String value : TABLE_ADDRESSES) {
            Address tableAddress = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            ReferenceIterator references = referenceManager.getReferencesTo(tableAddress);
            while (references.hasNext()) {
                Reference reference = references.next();
                Function caller = listing.getFunctionContaining(reference.getFromAddress());
                println(
                    "TABLE_REF|target=" + tableAddress +
                    "|from=" + reference.getFromAddress() +
                    "|type=" + reference.getReferenceType() +
                    "|caller=" + (caller == null ? "<none>" : caller.getName(true)) +
                    "|caller_entry=" + (caller == null ? "<none>" : caller.getEntryPoint())
                );
            }
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
            println(
                "CELL|address=" + cell +
                "|delta=" + delta +
                "|raw=0x" + Long.toUnsignedString(raw, 16) +
                "|target=" + (target == null ? "<none>" : target) +
                "|block=" + (block == null ? "<none>" : block.getName()) +
                "|function=" + (function == null ? "<none>" : function.getName(true))
            );
        }
        println("TABLE_END|slot=" + slot);
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
