// Configures a focused Ghidra analysis profile for locating Weixin payment UI references.
// @category CFPay

import ghidra.app.script.GhidraScript;

import java.util.LinkedHashMap;
import java.util.Map;

public class ConfigureWechatFastAnalysis extends GhidraScript {
    @Override
    protected void run() throws Exception {
        Map<String, String> options = new LinkedHashMap<>();

        options.put("ASCII Strings", "true");
        options.put("Disassemble Entry Points", "true");
        options.put("Reference", "true");
        options.put("Data Reference", "true");
        options.put("Subroutine References", "true");
        options.put("Scalar Operand References", "true");
        options.put("Demangler Microsoft", "true");

        options.put("Apply Data Archives", "false");
        options.put("Call Convention ID", "false");
        options.put("Create Address Tables", "false");
        options.put("Decompiler Parameter ID", "false");
        options.put("Decompiler Switch Analysis", "false");
        options.put("Embedded Media", "false");
        options.put("Function ID", "false");
        options.put("Non-Returning Functions - Discovered", "false");
        options.put("PDB Universal", "false");
        options.put("Shared Return Calls", "false");
        options.put("Stack", "false");
        options.put("Windows x86 PE Exception Handling", "false");
        options.put("Windows x86 PE RTTI Analyzer", "false");
        options.put("Windows x86 Thread Environment Block (TEB) Analyzer", "false");
        options.put("WindowsResourceReference", "false");
        options.put("x86 Constant Reference Analyzer", "false");

        setAnalysisOptions(currentProgram, options);
        for (Map.Entry<String, String> entry : options.entrySet()) {
            println("CONFIG|name=" + entry.getKey() + "|value=" + entry.getValue());
        }
        println("SUMMARY|configured=" + options.size());
    }
}
