// Maps the live post-decode return-address chain to call sites and containing functions.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.util.LinkedHashMap;
import java.util.Map;

public class DecompileWechatPostDecodeCallChain extends GhidraScript {
    private static final long[] RETURN_RVAS = {
        0x0000d23fL,
        0x00057772L,
        0x0194834bL,
        0x019746d0L,
        0x001d1ed4L,
        0x001d1c70L,
        0x00f4cbd0L,
        0x003892f8L,
        0x00389289L,
        0x0063b583L,
        0x01618218L,
        0x01617d02L,
        0x006978a3L,
        0x0062e75fL,
        0x0062e60bL,
        0x0062e20fL,
    };

    @Override
    protected void run() throws Exception {
        Address imageBase = currentProgram.getImageBase();
        Map<Address, Function> functions = new LinkedHashMap<>();
        for (int index = 0; index < RETURN_RVAS.length; index += 1) {
            Address returnAddress = imageBase.add(RETURN_RVAS[index]);
            Function function = currentProgram.getListing().getFunctionContaining(returnAddress);
            Instruction previous = currentProgram.getListing().getInstructionBefore(returnAddress);
            println("FRAME|index=" + index + "|rva=0x" + Long.toHexString(RETURN_RVAS[index]) +
                "|return=" + returnAddress + "|function=" + describe(function) +
                "|previous=" + describe(previous));
            if (function != null) functions.putIfAbsent(function.getEntryPoint(), function);
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        try {
            for (Function function : functions.values()) {
                DecompileResults results = decompiler.decompileFunction(function, 180, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" + function.getName() +
                    "|completed=" + results.decompileCompleted());
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
        println("SUMMARY|frames=" + RETURN_RVAS.length + "|functions=" + functions.size());
    }

    private String describe(Function function) {
        if (function == null) return "<none>";
        return function.getName() + "@" + function.getEntryPoint();
    }

    private String describe(Instruction instruction) {
        if (instruction == null) return "<none>";
        return instruction.getAddress() + ":" + instruction;
    }
}
