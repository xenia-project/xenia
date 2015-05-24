// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class AttachResponse : Table {
  public static AttachResponse GetRootAsAttachResponse(ByteBuffer _bb) { return GetRootAsAttachResponse(_bb, new AttachResponse()); }
  public static AttachResponse GetRootAsAttachResponse(ByteBuffer _bb, AttachResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public AttachResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public string MemoryFile { get { int o = __offset(4); return o != 0 ? __string(o + bb_pos) : null; } }
  public string FunctionsFile { get { int o = __offset(6); return o != 0 ? __string(o + bb_pos) : null; } }
  public string FunctionsTraceFile { get { int o = __offset(8); return o != 0 ? __string(o + bb_pos) : null; } }

  public static int CreateAttachResponse(FlatBufferBuilder builder,
      int memory_file = 0,
      int functions_file = 0,
      int functions_trace_file = 0) {
    builder.StartObject(3);
    AttachResponse.AddFunctionsTraceFile(builder, functions_trace_file);
    AttachResponse.AddFunctionsFile(builder, functions_file);
    AttachResponse.AddMemoryFile(builder, memory_file);
    return AttachResponse.EndAttachResponse(builder);
  }

  public static void StartAttachResponse(FlatBufferBuilder builder) { builder.StartObject(3); }
  public static void AddMemoryFile(FlatBufferBuilder builder, int memoryFileOffset) { builder.AddOffset(0, memoryFileOffset, 0); }
  public static void AddFunctionsFile(FlatBufferBuilder builder, int functionsFileOffset) { builder.AddOffset(1, functionsFileOffset, 0); }
  public static void AddFunctionsTraceFile(FlatBufferBuilder builder, int functionsTraceFileOffset) { builder.AddOffset(2, functionsTraceFileOffset, 0); }
  public static int EndAttachResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
