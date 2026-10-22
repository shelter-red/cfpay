// Traces decoded network-object candidates before receipt session refresh and ViewModel binding.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.ArrayDeque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class TraceWechatDecodedNetworkObject extends GhidraScript {
    private static final int MAX_CALLER_DEPTH = 2;
    private static final int MAX_FUNCTIONS = 72;
    private static final int CONTEXT_INSTRUCTIONS = 5;
    private static final int POINTER_TABLE_RADIUS = 8;
    private static final List<String> SEEDS = List.of(
        "1848f0e70",
        "1848fd550",
        "184904ae0",
        "18490b260",
        "18490b5c0",
        "18490b920"
    );

    private record Pending(Function function, int depth, String reason) {}

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        ArrayDeque<Pending> pending = new ArrayDeque<>();
        Map<Address, Pending> selected = new LinkedHashMap<>();

        for (String seedText : SEEDS) {
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(seedText);
            Function function = listing.getFunctionAt(address);
            if (function == null) function = listing.getFunctionContaining(address);
            if (function == null) {
                println("SEED_MISSING|address=" + seedText);
                continue;
            }
            pending.addLast(new Pending(function, 0, "seed:" + seedText));
        }

        while (!pending.isEmpty() && selected.size() < MAX_FUNCTIONS && !monitor.isCancelled()) {
            Pending item = pending.removeFirst();
            Function function = item.function();
            Address entry = function.getEntryPoint();
            if (selected.containsKey(entry)) continue;
            selected.put(entry, item);
            println("SELECTED|entry=" + entry + "|name=" + function.getName(true) + "|depth=" +
                item.depth() + "|reason=" + item.reason());

            inspectCalls(function);
            inspectPointerReferences(function);

            if (item.depth() >= MAX_CALLER_DEPTH) continue;
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(entry);
            while (references.hasNext() && selected.size() + pending.size() < MAX_FUNCTIONS) {
                Reference reference = references.next();
                if (!reference.getReferenceType().isCall() && !reference.getReferenceType().isJump()) continue;
                Function caller = listing.getFunctionContaining(reference.getFromAddress());
                if (caller == null || caller.equals(function)) continue;
                println("CALLER_EDGE|from=" + caller.getEntryPoint() + "|site=" +
                    reference.getFromAddress() + "|to=" + entry + "|type=" + reference.getReferenceType());
                printInstructionContext(reference.getFromAddress());
                pending.addLast(new Pending(caller, item.depth() + 1, "caller-of:" + entry));
            }
        }

        decompileSelected(selected);
        println("SUMMARY|selected=" + selected.size() + "|max_depth=" + MAX_CALLER_DEPTH +
            "|cancelled=" + monitor.isCancelled());
    }

    private void inspectCalls(Function function) {
        Listing listing = currentProgram.getListing();
        InstructionIterator instructions = listing.getInstructions(function.getBody(), true);
        while (instructions.hasNext() && !monitor.isCancelled()) {
            Instruction instruction = instructions.next();
            if (instruction.getFlowType().isCall() || instruction.getFlowType().isJump()) {
                for (Address flow : instruction.getFlows()) {
                    Function target = listing.getFunctionAt(flow);
                    println("OUT_EDGE|from=" + function.getEntryPoint() + "|site=" +
                        instruction.getAddress() + "|to=" + flow + "|target=" +
                        (target == null ? "<none>" : target.getName(true)) + "|type=" +
                        instruction.getFlowType());
                }
            }
        }
    }

    private void inspectPointerReferences(Function function) {
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(function.getEntryPoint());
        while (references.hasNext()) {
            Reference reference = references.next();
            if (reference.getReferenceType().isCall() || reference.getReferenceType().isJump()) continue;
            Function owner = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
            println("POINTER_REF|target=" + function.getEntryPoint() + "|from=" +
                reference.getFromAddress() + "|type=" + reference.getReferenceType() + "|owner=" +
                (owner == null ? "<none>" : owner.getEntryPoint()));
            inspectPointerNeighborhood(reference.getFromAddress());
        }
    }

    private void inspectPointerNeighborhood(Address center) {
        Listing listing = currentProgram.getListing();
        Memory memory = currentProgram.getMemory();
        for (int slot = -POINTER_TABLE_RADIUS; slot <= POINTER_TABLE_RADIUS; slot += 1) {
            Address address;
            try {
                address = center.add(slot * 8L);
            } catch (Exception ignored) {
                continue;
            }
            long value;
            try {
                value = memory.getLong(address);
            } catch (Exception ignored) {
                continue;
            }
            Address candidate = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            Function target = listing.getFunctionAt(candidate);
            MemoryBlock block = memory.getBlock(candidate);
            println("POINTER_SLOT|center=" + center + "|slot=" + slot + "|address=" + address +
                "|value=" + candidate + "|target=" + (target == null ? "<none>" : target.getName(true)) +
                "|block=" + (block == null ? "<none>" : block.getName()));

            ReferenceIterator slotReferences = currentProgram.getReferenceManager().getReferencesTo(address);
            while (slotReferences.hasNext()) {
                Reference slotReference = slotReferences.next();
                Function owner = listing.getFunctionContaining(slotReference.getFromAddress());
                println("POINTER_SLOT_REF|slot_address=" + address + "|from=" +
                    slotReference.getFromAddress() + "|type=" + slotReference.getReferenceType() +
                    "|owner=" + (owner == null ? "<none>" : owner.getEntryPoint()));
            }
        }
    }

    private void printInstructionContext(Address callsite) {
        Listing listing = currentProgram.getListing();
        Instruction current = listing.getInstructionAt(callsite);
        if (current == null) return;
        Instruction start = current;
        for (int index = 0; index < CONTEXT_INSTRUCTIONS; index += 1) {
            Instruction previous = start.getPrevious();
            if (previous == null) break;
            start = previous;
        }
        Instruction cursor = start;
        for (int index = 0; cursor != null && index <= CONTEXT_INSTRUCTIONS * 2; index += 1) {
            println("CALL_CONTEXT|site=" + callsite + "|address=" + cursor.getAddress() +
                "|instruction=" + cursor);
            if (cursor.getAddress().equals(callsite) && index >= CONTEXT_INSTRUCTIONS) {
                Instruction next = cursor;
                for (int tail = 0; tail < CONTEXT_INSTRUCTIONS; tail += 1) {
                    next = next.getNext();
                    if (next == null) break;
                    println("CALL_CONTEXT|site=" + callsite + "|address=" + next.getAddress() +
                        "|instruction=" + next);
                }
                break;
            }
            cursor = cursor.getNext();
        }
    }

    private void decompileSelected(Map<Address, Pending> selected) {
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        try {
            for (Pending item : selected.values()) {
                if (monitor.isCancelled()) break;
                Function function = item.function();
                DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" +
                    function.getName(true) + "|depth=" + item.depth() + "|completed=" +
                    results.decompileCompleted());
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
}
