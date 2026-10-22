// Finds RIP-relative references to Weixin payment callback table address points.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import ghidra.app.util.PseudoInstruction;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class FindWechatPaymentTableRefs extends GhidraScript {
    private static final int CHUNK_SIZE = 1024 * 1024;
    private static final int MAX_TRAILING_BYTES = 4;
    private static final int MAX_BYTES_BEFORE_DISPLACEMENT = 11;
    private static final List<String> TARGETS = List.of(
        "1897d1978", "1897d1980", "1897d1988",
        "1897d2338", "1897d2340", "1897d2348",
        "1897d25b8", "1897d25c0", "1897d25c8"
    );

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        PseudoDisassembler disassembler = new PseudoDisassembler(currentProgram);
        Map<Long, Address> targets = new HashMap<>();
        Set<String> reported = new HashSet<>();
        int candidates = 0;
        int decoded = 0;

        for (String value : TARGETS) {
            Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            targets.put(target.getOffset(), target);
        }

        for (MemoryBlock block : memory.getBlocks()) {
            if (!block.isInitialized() || !block.isExecute() || block.getSize() < 4) continue;
            println("BLOCK_BEGIN|name=" + block.getName() + "|start=" + block.getStart() + "|size=" + block.getSize());

            long positionsRemaining = block.getSize() - 3;
            for (long chunkOffset = 0; chunkOffset < positionsRemaining && !monitor.isCancelled(); chunkOffset += CHUNK_SIZE) {
                int positionCount = (int) Math.min(CHUNK_SIZE, positionsRemaining - chunkOffset);
                byte[] bytes = new byte[positionCount + 3];
                Address chunkStart = block.getStart().add(chunkOffset);
                int bytesRead = memory.getBytes(chunkStart, bytes);
                int availablePositions = Math.min(positionCount, bytesRead - 3);

                for (int index = 0; index < availablePositions; index++) {
                    int displacement =
                        (bytes[index] & 0xff) |
                        ((bytes[index + 1] & 0xff) << 8) |
                        ((bytes[index + 2] & 0xff) << 16) |
                        (bytes[index + 3] << 24);
                    long displacementAddress = chunkStart.getOffset() + index;
                    long baseTarget = displacementAddress + 4L + displacement;

                    for (int trailingBytes = 0; trailingBytes <= MAX_TRAILING_BYTES; trailingBytes++) {
                        Address target = targets.get(baseTarget + trailingBytes);
                        if (target == null) continue;
                        candidates += 1;
                        if (decodeCandidate(
                            disassembler,
                            listing,
                            memory,
                            block,
                            displacementAddress,
                            trailingBytes,
                            target,
                            reported
                        )) {
                            decoded += 1;
                        }
                    }
                }
            }
            println("BLOCK_END|name=" + block.getName());
        }

        println(
            "SUMMARY|targets=" + targets.size() +
            "|candidates=" + candidates+
            "|decoded=" + decoded+
            "|unique=" + reported.size()+
            "|cancelled=" + monitor.isCancelled()
        );
    }

    private boolean decodeCandidate(
        PseudoDisassembler disassembler,
        Listing listing,
        Memory memory,
        MemoryBlock block,
        long displacementAddress,
        int trailingBytes,
        Address target,
        Set<String> reported
    ) {
        Address displacement = block.getStart().getAddressSpace().getAddress(displacementAddress);
        Address expectedEnd = displacement.add(3L + trailingBytes);
        PseudoInstruction bestInstruction = null;

        for (int bytesBefore = 0; bytesBefore <= MAX_BYTES_BEFORE_DISPLACEMENT; bytesBefore++) {
            Address start;
            try {
                start = displacement.subtract(bytesBefore);
            } catch (Exception exception) {
                continue;
            }
            if (!block.contains(start)) continue;

            try {
                PseudoInstruction instruction = disassembler.disassemble(start);
                if (instruction == null || !instruction.getMaxAddress().equals(expectedEnd)) continue;
                if (!instruction.toString().contains(target.toString())) continue;
                if (bestInstruction == null || instruction.getLength() > bestInstruction.getLength()) {
                    bestInstruction = instruction;
                }
            } catch (Exception ignored) {
                // Candidate starts are intentionally speculative.
            }
        }

        if (bestInstruction != null) {
            Address start = bestInstruction.getMinAddress();
            String key = start + "->" + target;
            if (reported.add(key)) {
                Function function = listing.getFunctionContaining(start);
                try {
                    println(
                        "RIP_REF|target=" + target+
                        "|instruction=" + start+
                        "|end=" + expectedEnd+
                        "|bytes=" + hex(bestInstruction.getBytes())+
                        "|assembly=" + clean(bestInstruction.toString())+
                        "|function=" + (function == null ? "<none>" : function.getName(true))+
                        "|function_entry=" + (function == null ? "<none>" : function.getEntryPoint())
                    );
                } catch (Exception exception) {
                    println("RIP_REF_ERROR|target=" + target + "|instruction=" + start + "|message=" + clean(exception.getMessage()));
                }
            }
            return true;
        }

        String key = "raw:" + displacement + ":" + trailingBytes + "->" + target;
        if (reported.add(key)) {
            println(
                "RAW_CANDIDATE|target=" + target+
                "|displacement=" + displacement+
                "|trailing_bytes=" + trailingBytes+
                "|bytes=" + nearbyBytes(memory, displacement)
            );
        }
        return false;
    }

    private String nearbyBytes(Memory memory, Address displacement) {
        try {
            Address start = displacement.subtract(11);
            byte[] bytes = new byte[19];
            int count = memory.getBytes(start, bytes);
            if (count < bytes.length) {
                byte[] partial = new byte[Math.max(count, 0)];
                System.arraycopy(bytes, 0, partial, 0, partial.length);
                return hex(partial);
            }
            return hex(bytes);
        } catch (Exception exception) {
            return "<unavailable>";
        }
    }

    private String hex(byte[] bytes) {
        StringBuilder result = new StringBuilder();
        for (byte value : bytes) {
            if (result.length() > 0) result.append(' ');
            result.append(String.format("%02x", value & 0xff));
        }
        return result.toString();
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
