# Metal Debug Agent Usage Guide

## Overview

The Metal Debug Analyzer Agent is a specialized AI assistant designed to analyze Xenia Metal backend debug sessions and identify rendering issues. This guide shows how to use it effectively.

## Quick Start

### 1. Generate a Debug Session

```bash
# Run the comprehensive debug script
./debug_metal.sh

# This creates a session in metal_debug/session_YYYYMMDD_HHMMSS/
```

### 2. Prepare Analysis Package

```bash
# Analyze the latest session
./analyze_metal_session.sh

# Or analyze a specific session
./analyze_metal_session.sh metal_debug/session_20250808_132403
```

### 3. Use with Claude

Copy the agent instructions and analysis package:
```bash
cat metal_debug_analyzer_agent.md metal_debug/session_*/analysis_package.md | pbcopy
```

Then paste to Claude with a request like:
- "Please analyze this Metal debug session and identify why the output is black"
- "What's preventing textures from rendering in this session?"
- "Prioritize fixes for this rendering issue"

## Understanding the Agent's Analysis

The agent will provide:

### 1. Summary Assessment
- Output status (black/corrupted/partial/working)
- Primary issue identification
- Severity rating

### 2. Detailed Findings
- Specific patterns found in logs
- File locations and line numbers
- Impact on rendering pipeline
- Recommended fixes

### 3. Root Cause Analysis
- Chain of issues leading to the problem
- System interactions
- Comparison with expected behavior

### 4. Prioritized Fix List
- Specific files and functions to modify
- Code changes needed
- Order of implementation

## Common Scenarios

### Scenario 1: Black Output Investigation

```bash
# Generate debug session
./debug_metal.sh

# Check for black output
./analyze_metal_session.sh

# Ask the agent
"The output PNG is completely black. Please identify the root cause and provide fixes."
```

The agent will check:
- Render target configuration
- Texture binding pipeline
- Shader compilation
- Draw call execution
- Clear color settings

### Scenario 2: Texture Binding Issues

```bash
# Focus on texture problems
grep "texture" metal_debug/session_*/logs/full_output.log > texture_issues.txt

# Ask the agent
"Textures are found but not appearing. Here's the texture binding log. What's broken?"
```

The agent will analyze:
- Fetch constant parsing
- Descriptor heap population
- Texture upload calls
- Resource binding in shaders

### Scenario 3: GPU Capture Failures

```bash
# Check capture status
ls metal_debug/session_*/captures/

# Ask the agent
"GPU captures aren't being generated. How do I fix programmatic capture?"
```

The agent will examine:
- BeginProgrammaticCapture calls
- EndProgrammaticCapture placement
- Command buffer lifecycle
- Capture manager initialization

## Advanced Usage

### Custom Analysis Queries

You can ask specific questions:

1. **Performance Issues**
   ```
   "Analyze why we have 800+ draw calls but no visible output"
   ```

2. **Shader Problems**
   ```
   "Check if the vertex shader is properly passing data to the pixel shader"
   ```

3. **Memory Issues**
   ```
   "Verify EDRAM addresses and render target memory allocation"
   ```

4. **Pipeline State**
   ```
   "Examine blend state, depth testing, and stencil configuration"
   ```

### Comparative Analysis

Compare multiple sessions:
```bash
# Generate analysis for multiple sessions
for session in metal_debug/session_*; do
    ./analyze_metal_session.sh "$session"
done

# Ask the agent
"Compare these sessions and identify what changed between them"
```

### Iterative Debugging

After implementing fixes:
```bash
# Rebuild and test
./xb build --config Checked
./debug_metal.sh

# Analyze changes
./analyze_metal_session.sh

# Ask the agent
"I implemented the texture binding fix. This is the new output. What's the next issue?"
```

## Key Patterns the Agent Recognizes

### Critical Patterns
- `RB_COLOR_INFO[0-3] = 0x00000000` - No render targets
- `Prepared 0 textures` - Texture binding failure
- `ERROR:` - Compilation or runtime errors
- `GPU captures taken: 0` - Capture not working

### Warning Patterns
- `Uniforms are all zeros` - Constant buffer issues
- `WARNING - Untiled texture data is all black` - Data problems
- `No texture binding for heap slot` - Missing resources
- `Skipping framebuffer fetch sentinel` - Special markers

### Success Patterns
- `CREATED AND BOUND REAL color target` - RT working
- `Drew indexed primitives` - Successful draws
- `Captured 1280x720 texture` - Capture successful
- `MSC res-heap: setTexture` - Resource bound

## Integration with Development Workflow

### 1. Pre-commit Testing
```bash
# Before committing Metal backend changes
./debug_metal.sh
./analyze_metal_session.sh
# Review agent's analysis for regressions
```

### 2. Issue Reporting
Include agent analysis in bug reports:
```bash
# Generate comprehensive report
./debug_metal.sh
./analyze_metal_session.sh
cat metal_debug/session_*/analysis_package.md > issue_report.md
```

### 3. Code Review
Use agent to verify fixes:
```
"I changed the texture upload path in metal_command_processor.mm.
Here's the before/after debug output. Did this fix the issue?"
```

## Tips for Best Results

1. **Provide Context**: Tell the agent what you've already tried
2. **Be Specific**: Ask about particular subsystems or errors
3. **Include Logs**: Share relevant log excerpts for detailed analysis
4. **Iterative Approach**: Fix issues in the order suggested by the agent
5. **Validate Fixes**: Always run a new debug session after changes

## Example Agent Responses

### Good Query
"The trace dump shows 'Found valid texture' but textures aren't bound to descriptor heaps. 
The output is black. What's the exact issue in the texture upload path?"

### Agent Response Structure
```
## Metal Debug Session Analysis

### Summary
- Output Status: Black
- Primary Issue: Texture upload path disconnected
- Severity: Critical

### Key Findings
1. Texture Binding Pipeline Broken
   - Pattern: "Found valid texture at fetch constant 0"
   - Pattern: "Prepared 0 textures for argument buffer"
   - Location: metal_command_processor.mm:2387
   - Impact: Textures found but never uploaded to GPU

### Root Cause
The texture is located via fetch constants but the upload path
(UploadTexture2D) isn't being called before descriptor heap population.

### Recommended Fix
File: src/xenia/gpu/metal/metal_command_processor.mm
Function: IssueDraw()
Line: ~2387
Change: Add texture upload call after finding texture via fetch constant
```

## Troubleshooting the Agent

If the agent seems confused:
1. Ensure you provided the full agent instructions (metal_debug_analyzer_agent.md)
2. Include specific error messages or patterns
3. Focus on one issue at a time
4. Provide before/after comparisons when testing fixes

## Summary

The Metal Debug Analyzer Agent accelerates debugging by:
- Systematically analyzing complex debug output
- Identifying root causes from symptoms
- Providing specific, actionable fixes
- Prioritizing issues by impact
- Validating your fixes

Use it throughout development to catch issues early and understand the Metal backend's behavior.