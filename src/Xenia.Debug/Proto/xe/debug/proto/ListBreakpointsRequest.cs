// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class ListBreakpointsRequest : Table {
  public static ListBreakpointsRequest GetRootAsListBreakpointsRequest(ByteBuffer _bb) { return GetRootAsListBreakpointsRequest(_bb, new ListBreakpointsRequest()); }
  public static ListBreakpointsRequest GetRootAsListBreakpointsRequest(ByteBuffer _bb, ListBreakpointsRequest obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public ListBreakpointsRequest __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartListBreakpointsRequest(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndListBreakpointsRequest(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
