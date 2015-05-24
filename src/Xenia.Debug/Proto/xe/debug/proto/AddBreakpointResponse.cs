// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class AddBreakpointResponse : Table {
  public static AddBreakpointResponse GetRootAsAddBreakpointResponse(ByteBuffer _bb) { return GetRootAsAddBreakpointResponse(_bb, new AddBreakpointResponse()); }
  public static AddBreakpointResponse GetRootAsAddBreakpointResponse(ByteBuffer _bb, AddBreakpointResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public AddBreakpointResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartAddBreakpointResponse(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndAddBreakpointResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
