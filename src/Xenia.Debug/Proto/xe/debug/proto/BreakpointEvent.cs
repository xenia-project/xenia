// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class BreakpointEvent : Table {
  public static BreakpointEvent GetRootAsBreakpointEvent(ByteBuffer _bb) { return GetRootAsBreakpointEvent(_bb, new BreakpointEvent()); }
  public static BreakpointEvent GetRootAsBreakpointEvent(ByteBuffer _bb, BreakpointEvent obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public BreakpointEvent __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public uint ThreadId { get { int o = __offset(4); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint BreakpointId { get { int o = __offset(6); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }

  public static int CreateBreakpointEvent(FlatBufferBuilder builder,
      uint thread_id = 0,
      uint breakpoint_id = 0) {
    builder.StartObject(2);
    BreakpointEvent.AddBreakpointId(builder, breakpoint_id);
    BreakpointEvent.AddThreadId(builder, thread_id);
    return BreakpointEvent.EndBreakpointEvent(builder);
  }

  public static void StartBreakpointEvent(FlatBufferBuilder builder) { builder.StartObject(2); }
  public static void AddThreadId(FlatBufferBuilder builder, uint threadId) { builder.AddUint(0, threadId, 0); }
  public static void AddBreakpointId(FlatBufferBuilder builder, uint breakpointId) { builder.AddUint(1, breakpointId, 0); }
  public static int EndBreakpointEvent(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
