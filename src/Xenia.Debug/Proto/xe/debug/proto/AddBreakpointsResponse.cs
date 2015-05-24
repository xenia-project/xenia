// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class AddBreakpointsResponse : Table {
  public static AddBreakpointsResponse GetRootAsAddBreakpointsResponse(ByteBuffer _bb) { return GetRootAsAddBreakpointsResponse(_bb, new AddBreakpointsResponse()); }
  public static AddBreakpointsResponse GetRootAsAddBreakpointsResponse(ByteBuffer _bb, AddBreakpointsResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public AddBreakpointsResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartAddBreakpointsResponse(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndAddBreakpointsResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
