// Decompiles AppReader construction callers and lists nearby class-table function pointers.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

import java.util.List;

public class InspectWechatAppReaderBindingCandidates extends GhidraScript {
    private static final List<String> ENTRIES = List.of(
        "1809ab800",
        "18208e670",
        "184646e50",
        "184644ae0",
        "184645fb0",
        "184644910",
        "1846477d0",
        "1808e4ef0",
        "1808e4180",
        "1808e4a50",
        "183d1fe50",
        "183d20600",
        "183d20780",
        "183d20800",
        "183d209a0",
        "183d20b20",
        "183d20f50",
        "183d21000",
        "183d21a40"
    );
    private static final List<String> TABLE_STARTS = List.of("189179800", "18992d200");
    private static final int TABLE_BYTES = 0x400;

    @Override
    protected void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        try {
            for (String entry : ENTRIES) {
                printSignature(entry);
                decompile(entry, decompiler);
            }
        } finally {
            decompiler.dispose();
        }

        for (String tableStart : TABLE_STARTS) {
            Address start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(tableStart);
            for (int offset = 0; offset < TABLE_BYTES && !monitor.isCancelled(); offset += 8) {
                Address slot = start.add(offset);
                long raw = currentProgram.getMemory().getLong(slot);
                Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(raw);
                MemoryBlock block = currentProgram.getMemory().getBlock(target);
                if (block == null || !block.isExecute()) continue;
                Function function = currentProgram.getListing().getFunctionAt(target);
                if (function == null) function = currentProgram.getListing().getFunctionContaining(target);
                println("TABLE_FUNCTION|table=" + start + "|slot=" + slot + "|target=" + target + "|function=" +
                    (function == null ? "<none>" : function.getName(true)) + "|entry=" +
                    (function == null ? "<none>" : function.getEntryPoint()));
            }
        }
    }

    private void printSignature(String requested) throws Exception {
        Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(requested);
        byte[] bytes = new byte[32];
        currentProgram.getMemory().getBytes(address, bytes);
        StringBuilder output = new StringBuilder();
        for (byte value : bytes) {
            if (output.length() > 0) output.append(' ');
            output.append(String.format("%02x", value & 0xff));
        }
        println("SIGNATURE|requested=" + requested + "|bytes=" + output);
    }

    private void decompile(String requested, DecompInterface decompiler) throws Exception {
        Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(requested);
        Function function = currentProgram.getListing().getFunctionAt(address);
        if (function == null) function = currentProgram.getListing().getFunctionContaining(address);
        if (function == null) {
            println("FUNCTION_MISSING|requested=" + requested);
            return;
        }
        DecompileResults results = decompiler.decompileFunction(function, 90, monitor);
        println("FUNCTION_BEGIN|requested=" + requested + "|entry=" + function.getEntryPoint() + "|name=" +
            function.getName(true) + "|completed=" + results.decompileCompleted());
        if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
            println(results.getDecompiledFunction().getC());
        } else {
            println("DECOMPILE_ERROR|" + results.getErrorMessage());
        }
        println("FUNCTION_END|entry=" + function.getEntryPoint());
    }
}
