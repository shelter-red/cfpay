import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class FindWechatLoginQrMessageCallers extends GhidraScript {
    private static final List<String> TARGETS = List.of(
        "180fa7c50",
        "180fab040",
        "180fac540",
        "180fac6c0",
        "180fac820",
        "180fae820",
        "180fb09e0",
        "180fb2210"
    );

    @Override
    protected void run() throws Exception {
        ReferenceManager references = currentProgram.getReferenceManager();
        Listing listing = currentProgram.getListing();
        Set<Function> callers = new HashSet<>();
        for (String value : TARGETS) {
            Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            Function targetFunction = listing.getFunctionAt(target);
            println("TARGET|address=" + target + "|function=" +
                (targetFunction == null ? "<none>" : targetFunction.getName(true)));
            ReferenceIterator iterator = references.getReferencesTo(target);
            int count = 0;
            while (iterator.hasNext()) {
                Reference reference = iterator.next();
                Function caller = listing.getFunctionContaining(reference.getFromAddress());
                println("REF|target=" + target + "|from=" + reference.getFromAddress() +
                    "|type=" + reference.getReferenceType() + "|caller=" +
                    (caller == null ? "<none>" : caller.getName(true)) + "|entry=" +
                    (caller == null ? "<none>" : caller.getEntryPoint()));
                if (caller != null && caller != targetFunction) callers.add(caller);
                count += 1;
            }
            println("SUMMARY|target=" + target + "|references=" + count);
            scanRelativeCalls(value, targetFunction, callers);
        }
        decompile(callers);
    }

    private void scanRelativeCalls(String target, Function targetFunction, Set<Function> callers) throws Exception {
        long targetOffset = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(target).getOffset();
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        if (text == null) return;
        int count = 0;
        for (long offset = text.getStart().getOffset(); offset + 5 <= text.getEnd().getOffset(); offset += 1) {
            Address from = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(offset);
            if ((currentProgram.getMemory().getByte(from) & 0xff) != 0xe8) continue;
            int relative = currentProgram.getMemory().getInt(from.add(1));
            if (offset + 5L + relative != targetOffset) continue;
            Function caller = currentProgram.getListing().getFunctionContaining(from);
            println("CALL|target=" + target + "|from=" + from + "|caller=" +
                (caller == null ? "<none>" : caller.getName(true)) + "|entry=" +
                (caller == null ? "<none>" : caller.getEntryPoint()));
            if (caller != null && caller != targetFunction) callers.add(caller);
            count += 1;
        }
        println("CALL_SUMMARY|target=" + target + "|calls=" + count);
    }

    private void decompile(Set<Function> callers) throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(false);
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException("decompiler unavailable");
        try {
            for (Function function : callers) {
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" + function.getName(true) +
                    "|completed=" + result.decompileCompleted());
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) println(result.getDecompiledFunction().getC());
                println("FUNCTION_END|entry=" + function.getEntryPoint());
            }
        } finally {
            decompiler.dispose();
        }
    }
}
