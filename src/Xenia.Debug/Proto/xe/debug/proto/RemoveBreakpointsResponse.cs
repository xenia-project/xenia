// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class RemoveBreakpointsResponse : Table {
  public static RemoveBreakpointsResponse GetRootAsRemoveBreakpointsResponse(ByteBuffer _bb) { return GetRootAsRemoveBreakpointsResponse(_bb, new RemoveBreakpointsResponse()); }
  public static RemoveBreakpointsResponse GetRootAsRemoveBreakpointsResponse(ByteBuffer _bb, RemoveBreakpointsResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public RemoveBreakpointsResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartRemoveBreakpointsResponse(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndRemoveBreakpointsResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
