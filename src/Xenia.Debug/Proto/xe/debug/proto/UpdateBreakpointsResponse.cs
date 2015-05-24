// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class UpdateBreakpointsResponse : Table {
  public static UpdateBreakpointsResponse GetRootAsUpdateBreakpointsResponse(ByteBuffer _bb) { return GetRootAsUpdateBreakpointsResponse(_bb, new UpdateBreakpointsResponse()); }
  public static UpdateBreakpointsResponse GetRootAsUpdateBreakpointsResponse(ByteBuffer _bb, UpdateBreakpointsResponse obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public UpdateBreakpointsResponse __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }


  public static void StartUpdateBreakpointsResponse(FlatBufferBuilder builder) { builder.StartObject(0); }
  public static int EndUpdateBreakpointsResponse(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
