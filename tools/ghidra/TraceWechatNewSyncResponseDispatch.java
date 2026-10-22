// Traces the NewSync response protobuf object into command dispatch.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.LinkedHashMap;
import java.util.Map;

public class TraceWechatNewSyncResponseDispatch extends GhidraScript {
    private static final String[] ENTRIES = {
        "18106fa20",
        "18106fa70",
        "18106faa0",
        "18106fb10",
        "1810700b0",
        "1810701c0",
        "1810703a0",
        "181070420",
        "182e7c6e0",
        "182e7caf0",
        "182e7d0e0",
        "182e7d340",
        "182e7d480",
        "182e29330",
        "182e2fbb0",
        "182e30aa0",
        "183a93ef0",
        "181072a30",
        "181072ab0",
    };

    @Override
    protected void run() throws Exception {
        Map<Address, Function> selected = new LinkedHashMap<>();
        for (String entry : ENTRIES) {
            Function function = currentProgram.getFunctionManager().getFunctionContaining(toAddr(entry));
            if (function == null) {
                println("FUNCTION_MISSING|entry=" + entry);
                continue;
            }
            selected.putIfAbsent(function.getEntryPoint(), function);
            printCallers(function);
            printCallees(function);
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        int completed = 0;
        try {
            for (Function function : selected.values()) {
                DecompileResults results = decompiler.decompileFunction(function, 180, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" + function.getName() +
                    "|completed=" + results.decompileCompleted());
                if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    println(results.getDecompiledFunction().getC());
                    completed += 1;
                } else {
                    println("DECOMPILE_ERROR|" + results.getErrorMessage());
                }
                println("FUNCTION_END|entry=" + function.getEntryPoint());
            }
        } finally {
            decompiler.dispose();
        }
        println("SUMMARY|selected=" + selected.size() + "|completed=" + completed);
    }

    private void printCallers(Function function) {
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(function.getEntryPoint());
        while (references.hasNext()) {
            Reference reference = references.next();
            Function caller = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
            println("CALLER|target=" + describe(function) + "|from=" + reference.getFromAddress() +
                "|type=" + reference.getReferenceType() + "|function=" + describe(caller));
        }
    }

    private void printCallees(Function function) {
        Instruction instruction = currentProgram.getListing().getInstructionAt(function.getBody().getMinAddress());
        while (instruction != null && function.getBody().contains(instruction.getAddress())) {
            for (Reference reference : instruction.getReferencesFrom()) {
                if (!reference.getReferenceType().isCall()) continue;
                Function callee = currentProgram.getFunctionManager().getFunctionContaining(reference.getToAddress());
                println("CALLEE|source=" + describe(function) + "|at=" + instruction.getAddress() +
                    "|target=" + reference.getToAddress() + "|function=" + describe(callee));
            }
            instruction = instruction.getNext();
        }
    }

    private String describe(Function function) {
        if (function == null) return "<none>";
        return function.getName() + "@" + function.getEntryPoint();
    }
}
