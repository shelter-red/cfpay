// Inspects shared dispatch metadata and helpers used by Weixin payment models.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
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

public class InspectWechatPaymentDispatch extends GhidraScript {
    private static final String METADATA = "1897d2e30";
    private static final List<String> FUNCTIONS = List.of(
        "18019a500",
        "1802fb010"
    );

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        Memory memory = currentProgram.getMemory();
        ReferenceManager referenceManager = currentProgram.getReferenceManager();
        Address metadata = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(METADATA);

        ReferenceIterator references = referenceManager.getReferencesTo(metadata);
        int referenceCount = 0;
        while (references.hasNext()) {
            Reference reference = references.next();
            Function caller = listing.getFunctionContaining(reference.getFromAddress());
            MemoryBlock block = memory.getBlock(reference.getFromAddress());
            println(
                "METADATA_REF|target=" + metadata+
                "|from=" + reference.getFromAddress()+
                "|type=" + reference.getReferenceType()+
                "|block=" + (block == null ? "<none>" : block.getName())+
                "|caller=" + (caller == null ? "<none>" : caller.getName(true))+
                "|caller_entry=" + (caller == null ? "<none>" : caller.getEntryPoint())
            );
            referenceCount += 1;
        }
        println("METADATA_SUMMARY|target=" + metadata + "|references=" + referenceCount);

        for (long delta = -0x100; delta < 0x100; delta += 16) {
            Address lineAddress = metadata.add(delta);
            byte[] bytes = new byte[16];
            int count = memory.getBytes(lineAddress, bytes);
            println(
                "METADATA_BYTES|address=" + lineAddress+
                "|delta=" + delta+
                "|hex=" + hex(bytes, count)+
                "|ascii=" + ascii(bytes, count)
            );
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }

        try {
            for (String value : FUNCTIONS) {
                Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
                Function function = listing.getFunctionAt(address);
                if (function == null) function = listing.getFunctionContaining(address);
                if (function == null) {
                    println("FUNCTION_MISSING|requested=" + value);
                    continue;
                }

                DecompileResults results = decompiler.decompileFunction(function, 60, monitor);
                println(
                    "FUNCTION_BEGIN|requested=" + value+
                    "|entry=" + function.getEntryPoint()+
                    "|name=" + function.getName(true)+
                    "|completed=" + results.decompileCompleted()
                );
                if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    println(results.getDecompiledFunction().getC());
                } else {
                    println("DECOMPILE_ERROR|" + results.getErrorMessage());
                }
                println("FUNCTION_END|entry=" + function.getEntryPoint());
            }
        } finally {
            decompiler.dispose();
        }
    }

    private String hex(byte[] bytes, int count) {
        StringBuilder result = new StringBuilder();
        for (int index = 0; index < count; index++) {
            if (result.length() > 0) result.append(' ');
            result.append(String.format("%02x", bytes[index] & 0xff));
        }
        return result.toString();
    }

    private String ascii(byte[] bytes, int count) {
        StringBuilder result = new StringBuilder();
        for (int index = 0; index < count; index++) {
            int value = bytes[index] & 0xff;
            result.append(value >= 0x20 && value <= 0x7e ? (char) value : '.');
        }
        return result.toString();
    }
}
