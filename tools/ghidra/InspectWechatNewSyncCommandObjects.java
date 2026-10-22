// Dumps NewSync command/payload vtables and decompiles the command parsing chain.
// @category CFPay
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;

import java.util.List;

public class InspectWechatNewSyncCommandObjects extends GhidraScript {
    private static final List<String> VTABLES = List.of("1893d3c78", "188e3d058", "188fc0d48");
    private static final List<String> FUNCTIONS = List.of("182e7d0e0", "182e7caf0", "18106fb10", "182e7d340", "182e7d480");
    private static final int SLOT_COUNT = 14;

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("decompiler unavailable");
        }
        try {
            for (String vtableText : VTABLES) {
                Address vtable = addr(vtableText);
                println("VTABLE_BEGIN|address=" + vtable);
                for (int slot = 0; slot < SLOT_COUNT; slot += 1) {
                    long target = memory.getLong(vtable.add(slot * 8L));
                    Address targetAddress = addr(Long.toHexString(target));
                    Function function = currentProgram.getListing().getFunctionAt(targetAddress);
                    if (function == null) function = currentProgram.getListing().getFunctionContaining(targetAddress);
                    println("VTABLE_SLOT|vtable=" + vtableText + "|slot=" + slot + "|offset=0x" +
                        Integer.toHexString(slot * 8) + "|target=" + targetAddress + "|function=" +
                        (function == null ? "<none>" : function.getName(true)));
                }
                println("VTABLE_END|address=" + vtable);
            }

            for (String entryText : FUNCTIONS) {
                Address entry = addr(entryText);
                Function function = currentProgram.getListing().getFunctionAt(entry);
                if (function == null) function = currentProgram.getListing().getFunctionContaining(entry);
                if (function == null) {
                    println("FUNCTION_MISSING|" + entryText);
                    continue;
                }
                DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
                println("FUNCTION_BEGIN|entry=" + entryText + "|name=" + function.getName(true) +
                    "|completed=" + results.decompileCompleted());
                if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    println(results.getDecompiledFunction().getC());
                } else {
                    println("DECOMPILE_ERROR|" + results.getErrorMessage());
                }
                println("FUNCTION_END|" + entryText);
            }

            // 虚表前四个方法单独反编译，用于找出取载荷指针/长度的访问器。
            for (String vtableText : List.of("1893d3c78", "188e3d058")) {
                Address vtable = addr(vtableText);
                for (int slot = 0; slot < 6; slot += 1) {
                    long target = memory.getLong(vtable.add(slot * 8L));
                    Address targetAddress = addr(Long.toHexString(target));
                    Function function = currentProgram.getListing().getFunctionAt(targetAddress);
                    if (function == null) function = currentProgram.getListing().getFunctionContaining(targetAddress);
                    if (function == null) continue;
                    DecompileResults results = decompiler.decompileFunction(function, 90, monitor);
                    println("METHOD_BEGIN|vtable=" + vtableText + "|slot=" + slot + "|entry=" + targetAddress +
                        "|name=" + function.getName(true) + "|completed=" + results.decompileCompleted());
                    if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                        println(results.getDecompiledFunction().getC());
                    }
                    println("METHOD_END|vtable=" + vtableText + "|slot=" + slot);
                }
            }
        } finally {
            decompiler.dispose();
        }
    }

    private Address addr(String hex) throws Exception {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hex);
    }
}
