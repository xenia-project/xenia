// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class RemoveBreakpointResponse : Table {
  public static RemoveBreakpointResponse GetRootAsRemoveBreakpointResponse(ByteBuffer _bb) { return GetRootAsRemoveBreakpointResponse(_bb, new RemoveBreakpointResponse()); }
  public static RemoveBreakpointResponse GetRootAsRemoveBreakpointResponse(ByteBuffer _bb, RemoveBreakpointResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public RemoveBreakpointResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartRemoveBreakpointResponse(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndRemoveBreakpointResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
