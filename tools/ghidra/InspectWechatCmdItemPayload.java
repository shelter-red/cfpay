// Decompiles the CmdItem payload class constructor, parser and virtual accessors.
// @category CFPay
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;

import java.util.List;

public class InspectWechatCmdItemPayload extends GhidraScript {
    private static final List<String> FUNCTIONS =
        List.of("180679080", "18067ab70", "18067ba00", "18067ae00", "18067aaf0", "18067ab30", "18067ad50", "180681e10");

    @Override
    protected void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException("decompiler unavailable");
        try {
            Memory memory = currentProgram.getMemory();
            // 载荷类虚表之后通常紧跟类型名字符串，读取其后可读文本用于确认类型。
            for (String vtableText : List.of("188e3d058", "1893d3c78")) {
                Address vtable = addr(vtableText);
                StringBuilder builder = new StringBuilder();
                for (int offset = 0; offset < 0x60; offset += 1) {
                    byte value = memory.getByte(vtable.add(offset));
                    if (value >= 0x20 && value < 0x7f) builder.append((char) value);
                    else builder.append('.');
                }
                println("VTABLE_BYTES|" + vtableText + "|" + builder);
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
        } finally {
            decompiler.dispose();
        }
    }

    private Address addr(String hex) throws Exception {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hex);
    }
}
