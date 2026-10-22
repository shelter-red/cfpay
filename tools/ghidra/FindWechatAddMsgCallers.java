// Finds direct and indexed references to the AddMsg lifecycle functions.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import ghidra.app.util.PseudoInstruction;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.LinkedHashMap;
import java.util.Map;

public class FindWechatAddMsgCallers extends GhidraScript {
    private static final String[] TARGETS = {
        "181072a30", // constructor
        "181072ab0", // protobuf parser
        "181073980", // destructor
        "188fc0f38"  // vtable
    };
    private static final int MAX_FUNCTIONS = 48;

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        Memory memory = currentProgram.getMemory();
        PseudoDisassembler disassembler = new PseudoDisassembler(currentProgram);
        Map<Address, Function> functions = new LinkedHashMap<>();

        for (String targetText : TARGETS) {
            Address target = toAddr(targetText);
            println("TARGET_BEGIN|address=" + target + "|requested=" + targetText);
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
            int known = 0;
            while (references.hasNext()) {
                Reference reference = references.next();
                Function owner = listing.getFunctionContaining(reference.getFromAddress());
                println("KNOWN_REF|target=" + target + "|from=" + reference.getFromAddress() +
                    "|type=" + reference.getReferenceType() + "|owner=" + describe(owner));
                if (owner != null) functions.putIfAbsent(owner.getEntryPoint(), owner);
                known += 1;
            }
            println("KNOWN_SUMMARY|target=" + target + "|references=" + known);
            int direct = 0;
            for (MemoryBlock block : memory.getBlocks()) {
                if (!block.isInitialized() || !block.isExecute() || block.getSize() < 5) continue;
                byte[] bytes = new byte[(int)Math.min(block.getSize(), 1024 * 1024) + 4];
                for (long offset = 0; offset < block.getSize() - 4; offset += 1024 * 1024) {
                    int length = (int)Math.min(1024 * 1024, block.getSize() - offset);
                    if (bytes.length < length + 4) bytes = new byte[length + 4];
                    Address chunk = block.getStart().add(offset);
                    int read = memory.getBytes(chunk, bytes);
                    for (int index = 0; index + 4 < read; index += 1) {
                        int opcode = bytes[index] & 0xff;
                        if (opcode != 0xe8 && opcode != 0xe9) continue;
                        int displacement = (bytes[index + 1] & 0xff) |
                            ((bytes[index + 2] & 0xff) << 8) |
                            ((bytes[index + 3] & 0xff) << 16) |
                            (bytes[index + 4] << 24);
                        Address instructionAddress = chunk.add(index);
                        if (instructionAddress.getOffset() + 5L + displacement != target.getOffset()) continue;
                        try {
                            PseudoInstruction instruction = disassembler.disassemble(instructionAddress);
                            if (instruction == null || instruction.getLength() != 5) continue;
                            String mnemonic = instruction.getMnemonicString();
                            if (!"CALL".equals(mnemonic) && !"JMP".equals(mnemonic)) continue;
                            Function owner = listing.getFunctionContaining(instructionAddress);
                            println("DIRECT_BRANCH|target=" + target + "|instruction=" + instructionAddress +
                                "|assembly=" + clean(instruction.toString()) + "|owner=" + describe(owner));
                            if (owner != null) functions.putIfAbsent(owner.getEntryPoint(), owner);
                            direct += 1;
                        } catch (Exception ignored) {
                        }
                    }
                }
            }
            println("DIRECT_SUMMARY|target=" + target + "|branches=" + direct);
            println("TARGET_END|address=" + target);
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        int completed = 0;
        try {
            for (Function function : functions.values()) {
                if (completed >= MAX_FUNCTIONS) break;
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

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
