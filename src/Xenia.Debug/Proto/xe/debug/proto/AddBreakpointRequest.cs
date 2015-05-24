// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class AddBreakpointRequest : Table {
  public static AddBreakpointRequest GetRootAsAddBreakpointRequest(ByteBuffer _bb) { return GetRootAsAddBreakpointRequest(_bb, new AddBreakpointRequest()); }
  public static AddBreakpointRequest GetRootAsAddBreakpointRequest(ByteBuffer _bb, AddBreakpointRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public AddBreakpointRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartAddBreakpointRequest(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndAddBreakpointRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
