// Dumps and decompiles the generated micromsg.AddMsg virtual table.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.LinkedHashMap;
import java.util.Map;

public class InspectWechatAddMsgTables extends GhidraScript {
    private static final String VTABLE = "188fc0f38";

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        Address table = toAddr(VTABLE);
        Map<Address, Function> functions = new LinkedHashMap<>();
        println("TABLE_BEGIN|name=AddMsg.vtable|address=" + table);
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(table);
        while (references.hasNext()) {
            Reference reference = references.next();
            Function owner = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
            println("TABLE_REF|from=" + reference.getFromAddress() + "|type=" + reference.getReferenceType() +
                "|owner=" + describe(owner));
        }
        for (int slot = -4; slot <= 11; slot += 1) {
            Address address = table.add(slot * 8L);
            long value = memory.getLong(address);
            Address targetAddress = toAddr(Long.toUnsignedString(value, 16));
            MemoryBlock block = memory.getBlock(targetAddress);
            Function target = currentProgram.getListing().getFunctionAt(targetAddress);
            if (target == null) target = currentProgram.getListing().getFunctionContaining(targetAddress);
            println("SLOT|slot=" + slot + "|address=" + address + "|value=" + targetAddress +
                "|target=" + describe(target) + "|block=" + (block == null ? "<none>" : block.getName()));
            if (block != null && block.isExecute() && target != null) {
                functions.putIfAbsent(target.getEntryPoint(), target);
            }
        }
        println("TABLE_END|name=AddMsg.vtable");

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        int completed = 0;
        try {
            for (Function function : functions.values()) {
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
        println("SUMMARY|functions=" + functions.size() + "|completed=" + completed);
    }

    private String describe(Function function) {
        if (function == null) return "<none>";
        return function.getName() + "@" + function.getEntryPoint();
    }
}
